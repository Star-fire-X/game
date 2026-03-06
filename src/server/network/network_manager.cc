#include "network/network_manager.h"

#include <algorithm>
#include <asio/post.hpp>

#include <shared_mutex>

#include "monitor/metrics.h"

namespace mir2::network {

NetworkManager::NetworkManager(asio::io_context& io_context)
    : io_context_(io_context), server_(io_context) {
  server_.SetConnectHandler([this](const std::shared_ptr<TcpConnection>& connection) {
    AddConnection(connection);
  });
}

bool NetworkManager::Start(const std::string& bind_ip, uint16_t port, int max_connections) {
  max_connections_.store(std::max(max_connections, 0), std::memory_order_release);
  return server_.Start(bind_ip, port, max_connections);
}

bool NetworkManager::StartUnix(const std::string& socket_path, int max_connections) {
  max_connections_.store(std::max(max_connections, 0), std::memory_order_release);
  return server_.StartUnix(socket_path, max_connections);
}

void NetworkManager::Stop() {
  server_.Stop();
  StopAll();
}

void NetworkManager::RegisterHandler(uint16_t msg_id, MessageHandler handler) {
  dispatcher_.RegisterHandler(msg_id, std::move(handler));
}

void NetworkManager::Send(uint64_t connection_id, uint16_t msg_id, const std::vector<uint8_t>& payload) {
  std::shared_ptr<TcpSession> session;
  {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    auto it = sessions_.find(connection_id);
    if (it != sessions_.end()) {
      session = it->second;
    }
  }
  if (session) {
    session->Send(msg_id, payload);
  }
}

void NetworkManager::Broadcast(uint16_t msg_id, const std::vector<uint8_t>& payload) {
  auto sessions = GetAllSessions();
  for (auto& session : sessions) {
    if (session) {
      session->Send(msg_id, payload);
    }
  }
}

void NetworkManager::BroadcastIf(uint16_t msg_id, const std::vector<uint8_t>& payload,
                                 SessionFilter filter) {
  auto sessions = GetAllSessions();
  for (auto& session : sessions) {
    if (session && (!filter || filter(session))) {
      session->Send(msg_id, payload);
    }
  }
}

std::shared_ptr<TcpSession> NetworkManager::GetSession(uint64_t session_id) const {
  std::shared_lock<std::shared_mutex> lock(mutex_);
  auto it = sessions_.find(session_id);
  if (it != sessions_.end()) {
    return it->second;
  }
  return nullptr;
}

std::vector<std::shared_ptr<TcpSession>> NetworkManager::GetAllSessions() const {
  std::vector<std::shared_ptr<TcpSession>> result;
  std::shared_lock<std::shared_mutex> lock(mutex_);
  result.reserve(sessions_.size());
  for (const auto& [_, session] : sessions_) {
    if (session) {
      result.push_back(session);
    }
  }
  return result;
}

size_t NetworkManager::GetConnectionCount() const {
  std::shared_lock<std::shared_mutex> lock(mutex_);
  return sessions_.size();
}

void NetworkManager::Tick() {
  const int64_t now_ms = TcpSession::NowMs();
  const int64_t heartbeat_check_interval_ms =
      heartbeat_check_interval_ms_.load(std::memory_order_acquire);
  const int64_t heartbeat_timeout_ms =
      heartbeat_timeout_ms_.load(std::memory_order_acquire);
  if (heartbeat_check_interval_ms <= 0 || heartbeat_timeout_ms <= 0) {
    return;
  }
  if (now_ms - last_heartbeat_check_ms_ < heartbeat_check_interval_ms) {
    return;
  }

  last_heartbeat_check_ms_ = now_ms;

  asio::post(io_context_, [this, heartbeat_timeout_ms]() {
    const int64_t now_ms = TcpSession::NowMs();
    std::vector<std::shared_ptr<TcpSession>> expired_sessions;
    size_t timeout_count = 0;
    {
      std::shared_lock<std::shared_mutex> lock(mutex_);
      for (auto& [_, session] : sessions_) {
        if (!session) {
          continue;
        }
        if (session->IsServiceSession()) {
          continue;
        }
        const int64_t last_heartbeat_ms = session->GetLastHeartbeatMs();
        if (now_ms < last_heartbeat_ms ||
            now_ms - last_heartbeat_ms >= heartbeat_timeout_ms) {
          expired_sessions.push_back(session);
          ++timeout_count;
        }
      }
    }

    for (size_t i = 0; i < timeout_count; ++i) {
      monitor::Metrics::Instance().IncrementHeartbeatTimeouts();
    }
    for (const auto& session : expired_sessions) {
      if (session) {
        session->Close();
      }
    }
  });
}

void NetworkManager::SetIdlePolicy(int64_t check_interval_ms, int64_t timeout_ms) {
  heartbeat_check_interval_ms_.store(
      std::max<int64_t>(check_interval_ms, 1), std::memory_order_release);
  heartbeat_timeout_ms_.store(std::max<int64_t>(timeout_ms, 1), std::memory_order_release);
}

void NetworkManager::SetAcceptedConnectionWriteQueueSize(size_t max_write_queue_size) {
  server_.SetAcceptedConnectionWriteQueueSize(std::max<size_t>(max_write_queue_size, 1));
}

void NetworkManager::SetAcceptedConnectionWriteBatchOptions(
    TcpConnection::WriteBatchOptions options) {
  options.flush_interval_us = std::max<int64_t>(options.flush_interval_us, 0);
  server_.SetAcceptedConnectionWriteBatchOptions(options);
}

void NetworkManager::SetAcceptedConnectionLowCopySendEnabled(bool enabled) {
  server_.SetAcceptedConnectionLowCopySendEnabled(enabled);
}

void NetworkManager::SetMaxConnectionsForTest(int max_connections) {
  max_connections_.store(std::max(max_connections, 0), std::memory_order_release);
}

void NetworkManager::AddConnectionForTest(
    const std::shared_ptr<TcpConnection>& connection) {
  AddConnection(connection);
}

size_t NetworkManager::GetTrackedConnectionCountForTest() const {
  std::shared_lock<std::shared_mutex> lock(mutex_);
  return connections_.size();
}

void NetworkManager::AddConnection(const std::shared_ptr<TcpConnection>& connection) {
  if (!connection) {
    return;
  }

  connection->SetDisconnectHandler([this](uint64_t connection_id) {
    RemoveConnection(connection_id);
  });

  {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    const int max_connections = max_connections_.load(std::memory_order_acquire);
    if (max_connections > 0 &&
        connections_.size() >= static_cast<size_t>(max_connections)) {
      monitor::Metrics::Instance().IncrementError("connection_limit");
      connection->Close();
      return;
    }
    connections_[connection->GetConnectionId()] = connection;
  }

  OnConnectionOpened(connection);
}

void NetworkManager::RemoveConnection(uint64_t connection_id) {
  std::shared_ptr<TcpConnection> connection;
  {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    auto it = connections_.find(connection_id);
    if (it == connections_.end()) {
      return;
    }
    connection = it->second;
    connections_.erase(it);
  }

  OnConnectionClosed(connection);
}

std::shared_ptr<TcpConnection> NetworkManager::FindConnection(uint64_t connection_id) const {
  std::shared_lock<std::shared_mutex> lock(mutex_);
  auto it = connections_.find(connection_id);
  if (it != connections_.end()) {
    return it->second;
  }
  return nullptr;
}

void NetworkManager::BroadcastRaw(const std::vector<uint8_t>& bytes) {
  std::vector<std::shared_ptr<TcpConnection>> connections;
  {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    connections.reserve(connections_.size());
    for (const auto& [_, connection] : connections_) {
      if (connection) {
        connections.push_back(connection);
      }
    }
  }

  for (const auto& connection : connections) {
    if (connection) {
      connection->SendRaw(bytes);
    }
  }
}

void NetworkManager::Close(uint64_t connection_id) {
  std::shared_ptr<TcpConnection> connection;
  {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    auto it = connections_.find(connection_id);
    if (it != connections_.end()) {
      connection = it->second;
    }
  }

  if (connection) {
    connection->Close();
  }
}

void NetworkManager::StopAll() {
  std::vector<std::shared_ptr<TcpConnection>> connections;
  {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    connections.reserve(connections_.size());
    for (const auto& [_, connection] : connections_) {
      if (connection) {
        connections.push_back(connection);
      }
    }
  }

  for (const auto& connection : connections) {
    if (connection) {
      connection->Close();
    }
  }
}

void NetworkManager::OnConnectionOpened(const std::shared_ptr<TcpConnection>& connection) {
  if (!connection) {
    return;
  }

  auto session = std::make_shared<TcpSession>(connection);
  std::weak_ptr<TcpSession> weak_session = session;
  connection->SetReadHandler([weak_session](const uint8_t* data, size_t size) {
    if (auto locked = weak_session.lock()) {
      locked->HandleBytes(data, size);
    }
  });

  OnSessionConnected(session);
}

void NetworkManager::OnConnectionClosed(const std::shared_ptr<TcpConnection>& connection) {
  if (!connection) {
    return;
  }

  std::shared_ptr<TcpSession> session;
  {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    auto it = sessions_.find(connection->GetConnectionId());
    if (it != sessions_.end()) {
      session = it->second;
    }
  }

  if (session) {
    session->HandleDisconnect(connection->GetConnectionId());
    OnSessionDisconnected(session);
  }
}

void NetworkManager::OnSessionConnected(const std::shared_ptr<TcpSession>& session) {
  if (!session) {
    return;
  }

  {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    sessions_[session->GetSessionId()] = session;
    monitor::Metrics::Instance().SetConnections(static_cast<int64_t>(sessions_.size()));
  }

  session->SetMessageHandler([this](const std::shared_ptr<TcpSession>& session,
                                    const Packet& packet) {
    if (!session) {
      return;
    }
    dispatcher_.Dispatch(session, packet.msg_id, packet.payload);
  });

  session->Start();
}

void NetworkManager::OnSessionDisconnected(const std::shared_ptr<TcpSession>& session) {
  if (!session) {
    return;
  }
  std::unique_lock<std::shared_mutex> lock(mutex_);
  sessions_.erase(session->GetSessionId());
  monitor::Metrics::Instance().SetConnections(static_cast<int64_t>(sessions_.size()));
}


}  // namespace mir2::network
