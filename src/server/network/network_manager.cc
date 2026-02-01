#include "network/network_manager.h"

#include <asio/post.hpp>

#include "monitor/metrics.h"

namespace {

constexpr int64_t kHeartbeatCheckIntervalMs = 30000;
constexpr int64_t kHeartbeatTimeoutMs = 90000;

}  // namespace

namespace mir2::network {

NetworkManager::NetworkManager(asio::io_context& io_context)
    : io_context_(io_context), server_(io_context) {
  server_.SetConnectHandler([this](const std::shared_ptr<TcpConnection>& connection) {
    AddConnection(connection);
  });
}

bool NetworkManager::Start(const std::string& bind_ip, uint16_t port, int max_connections) {
  return server_.Start(bind_ip, port, max_connections);
}

void NetworkManager::Stop() {
  server_.Stop();
  StopAll();
}

void NetworkManager::RegisterHandler(uint16_t msg_id, MessageHandler handler) {
  dispatcher_.RegisterHandler(msg_id, std::move(handler));
}

void NetworkManager::Send(uint64_t connection_id, uint16_t msg_id, const std::vector<uint8_t>& payload) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = sessions_.find(connection_id);
  if (it != sessions_.end() && it->second) {
    it->second->Send(msg_id, payload);
  }
}

void NetworkManager::Broadcast(uint16_t msg_id, const std::vector<uint8_t>& payload) {
  std::lock_guard<std::mutex> lock(mutex_);
  for (auto& [_, session] : sessions_) {
    if (session) {
      session->Send(msg_id, payload);
    }
  }
}

void NetworkManager::BroadcastIf(uint16_t msg_id, const std::vector<uint8_t>& payload,
                                 SessionFilter filter) {
  std::lock_guard<std::mutex> lock(mutex_);
  for (auto& [_, session] : sessions_) {
    if (session && (!filter || filter(session))) {
      session->Send(msg_id, payload);
    }
  }
}

std::shared_ptr<TcpSession> NetworkManager::GetSession(uint64_t session_id) const {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = sessions_.find(session_id);
  if (it != sessions_.end()) {
    return it->second;
  }
  return nullptr;
}

std::vector<std::shared_ptr<TcpSession>> NetworkManager::GetAllSessions() const {
  std::vector<std::shared_ptr<TcpSession>> result;
  std::lock_guard<std::mutex> lock(mutex_);
  result.reserve(sessions_.size());
  for (const auto& [_, session] : sessions_) {
    if (session) {
      result.push_back(session);
    }
  }
  return result;
}

size_t NetworkManager::GetConnectionCount() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return sessions_.size();
}

void NetworkManager::Tick() {
  const int64_t now_ms = TcpSession::NowMs();
  if (now_ms - last_heartbeat_check_ms_ < kHeartbeatCheckIntervalMs) {
    return;
  }

  last_heartbeat_check_ms_ = now_ms;

  asio::post(io_context_, [this]() {
    const int64_t now_ms = TcpSession::NowMs();
    std::vector<std::shared_ptr<TcpSession>> expired_sessions;
    size_t timeout_count = 0;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      for (auto& [_, session] : sessions_) {
        if (!session) {
          continue;
        }
        const int64_t last_heartbeat_ms = session->GetLastHeartbeatMs();
        if (now_ms < last_heartbeat_ms ||
            now_ms - last_heartbeat_ms >= kHeartbeatTimeoutMs) {
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

void NetworkManager::AddConnection(const std::shared_ptr<TcpConnection>& connection) {
  if (!connection) {
    return;
  }

  connection->SetDisconnectHandler([this](uint64_t connection_id) {
    RemoveConnection(connection_id);
  });

  {
    std::lock_guard<std::mutex> lock(mutex_);
    connections_[connection->GetConnectionId()] = connection;
  }

  OnConnectionOpened(connection);
}

void NetworkManager::RemoveConnection(uint64_t connection_id) {
  std::shared_ptr<TcpConnection> connection;
  {
    std::lock_guard<std::mutex> lock(mutex_);
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
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = connections_.find(connection_id);
  if (it != connections_.end()) {
    return it->second;
  }
  return nullptr;
}

void NetworkManager::BroadcastRaw(const std::vector<uint8_t>& bytes) {
  std::vector<std::shared_ptr<TcpConnection>> connections;
  {
    std::lock_guard<std::mutex> lock(mutex_);
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
    std::lock_guard<std::mutex> lock(mutex_);
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
    std::lock_guard<std::mutex> lock(mutex_);
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
    std::lock_guard<std::mutex> lock(mutex_);
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
    std::lock_guard<std::mutex> lock(mutex_);
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
  std::lock_guard<std::mutex> lock(mutex_);
  sessions_.erase(session->GetSessionId());
  monitor::Metrics::Instance().SetConnections(static_cast<int64_t>(sessions_.size()));
}


}  // namespace mir2::network
