#include "network/dual_channel_manager.h"

#include <cassert>
#include <utility>
#include <flatbuffers/flatbuffers.h>

#include "common/enums.h"
#include "common/time_utils.h"
#include "network/handlers/kcp_upgrade_handler.h"
#include "network/network_manager.h"
#include "system_generated.h"

namespace mir2::network {

namespace {
constexpr uint16_t kMsgIdKcpHeartbeat =
    static_cast<uint16_t>(mir2::common::MsgId::kKcpHeartbeat);
constexpr uint16_t kMsgIdKcpHeartbeatAck =
    static_cast<uint16_t>(mir2::common::MsgId::kKcpHeartbeatAck);
constexpr uint16_t kMsgIdKcpUpgradeRequest =
    static_cast<uint16_t>(mir2::common::MsgId::kKcpUpgradeRequest);

class NetworkManagerAdapter : public INetworkManager {
 public:
  explicit NetworkManagerAdapter(asio::io_context& io_context)
      : manager_(io_context) {}

  bool Start(const std::string& bind_ip, uint16_t port, int max_connections) override {
    return manager_.Start(bind_ip, port, max_connections);
  }

  void Stop() override { manager_.Stop(); }

  void RegisterHandler(uint16_t msg_id, MessageHandler handler) override {
    manager_.RegisterHandler(msg_id, std::move(handler));
  }

  void Send(uint64_t connection_id, uint16_t msg_id,
            const std::vector<uint8_t>& payload) override {
    manager_.Send(connection_id, msg_id, payload);
  }

  std::shared_ptr<TcpSession> GetSession(uint64_t session_id) const override {
    return manager_.GetSession(session_id);
  }

  std::vector<std::shared_ptr<TcpSession>> GetAllSessions() const override {
    return manager_.GetAllSessions();
  }

  size_t GetConnectionCount() const override { return manager_.GetConnectionCount(); }

  void Tick() override { manager_.Tick(); }

 private:
  NetworkManager manager_;
};
}  // namespace

DualChannelManager::DualChannelManager(asio::io_context& io_context,
                                       mir2::common::KcpConfig kcp_config)
    : io_context_(io_context),
      tcp_manager_(std::make_unique<NetworkManagerAdapter>(io_context)),
      kcp_server_(std::make_unique<KcpServer>(io_context, kcp_config)) {
  kcp_server_->SetMessageHandler(
      [this](const std::shared_ptr<KcpSession>& session, const Packet& packet) {
        HandleKcpMessage(session, packet);
      });
}

DualChannelManager::DualChannelManager(asio::io_context& io_context,
                                       std::unique_ptr<INetworkManager> tcp_manager,
                                       std::unique_ptr<IKcpServer> kcp_server)
    : io_context_(io_context),
      tcp_manager_(std::move(tcp_manager)),
      kcp_server_(std::move(kcp_server)) {
  assert(tcp_manager_);
  assert(kcp_server_);
  kcp_server_->SetMessageHandler(
      [this](const std::shared_ptr<KcpSession>& session, const Packet& packet) {
        HandleKcpMessage(session, packet);
      });
}

DualChannelManager::~DualChannelManager() = default;

bool DualChannelManager::Start(const std::string& bind_ip,
                               uint16_t tcp_port,
                               uint16_t udp_port,
                               int max_connections) {
  if (!tcp_manager_->Start(bind_ip, tcp_port, max_connections)) {
    return false;
  }

  const bool kcp_started = kcp_server_->Start(bind_ip, udp_port);
  udp_port_ = kcp_started ? udp_port : 0;
  kcp_upgrade_handler_ = std::make_unique<KcpUpgradeHandler>(*this, udp_port_);
  tcp_manager_->RegisterHandler(
      kMsgIdKcpUpgradeRequest,
      [this](const std::shared_ptr<TcpSession>& session,
             const std::vector<uint8_t>& payload) {
        if (kcp_upgrade_handler_) {
          kcp_upgrade_handler_->HandleKcpUpgradeRequest(session, payload);
        }
      });

  return true;
}

void DualChannelManager::Stop() {
  kcp_server_->Stop();
  tcp_manager_->Stop();
  kcp_upgrade_handler_.reset();
  udp_port_ = 0;
  std::lock_guard<std::mutex> lock(mutex_);
  kcp_sessions_.clear();
}

void DualChannelManager::RegisterHandler(uint16_t msg_id, MessageHandler handler) {
  MessageHandler handler_copy = handler;
  tcp_manager_->RegisterHandler(msg_id, std::move(handler));
  kcp_dispatcher_.RegisterHandler(msg_id, std::move(handler_copy));
}

void DualChannelManager::SetRoute(uint16_t msg_id, mir2::common::ChannelType channel) {
  router_.SetRoute(msg_id, channel);
}

void DualChannelManager::SetDefaultChannel(mir2::common::ChannelType channel) {
  router_.SetDefaultChannel(channel);
}

void DualChannelManager::Send(uint64_t session_id,
                              uint16_t msg_id,
                              const std::vector<uint8_t>& payload) {
  const auto kcp_session = GetKcpSession(session_id);
  const bool kcp_available = kcp_session && kcp_session->HasRemoteEndpoint();
  const auto channel = router_.GetChannelWithFallback(msg_id, kcp_available);

  if (channel == mir2::common::ChannelType::kKcp && kcp_available) {
    kcp_session->Send(msg_id, payload);
    return;
  }

  tcp_manager_->Send(session_id, msg_id, payload);
}

void DualChannelManager::Broadcast(uint16_t msg_id, const std::vector<uint8_t>& payload) {
  auto sessions = tcp_manager_->GetAllSessions();
  for (const auto& session : sessions) {
    if (session) {
      Send(session->GetSessionId(), msg_id, payload);
    }
  }
}

void DualChannelManager::BroadcastIf(uint16_t msg_id,
                                     const std::vector<uint8_t>& payload,
                                     SessionFilter filter) {
  auto sessions = tcp_manager_->GetAllSessions();
  for (const auto& session : sessions) {
    if (!session) {
      continue;
    }
    if (filter && !filter(session)) {
      continue;
    }
    Send(session->GetSessionId(), msg_id, payload);
  }
}

std::shared_ptr<TcpSession> DualChannelManager::GetSession(uint64_t session_id) const {
  return tcp_manager_->GetSession(session_id);
}

std::vector<std::shared_ptr<TcpSession>> DualChannelManager::GetAllSessions() const {
  return tcp_manager_->GetAllSessions();
}

size_t DualChannelManager::GetConnectionCount() const {
  return tcp_manager_->GetConnectionCount();
}

void DualChannelManager::Tick() {
  tcp_manager_->Tick();
  CleanupKcpSessions();
}

bool DualChannelManager::BindKcpSession(uint64_t session_id,
                                        const std::shared_ptr<KcpSession>& kcp_session) {
  if (!kcp_session) {
    return false;
  }

  auto tcp_session = tcp_manager_->GetSession(session_id);
  if (!tcp_session) {
    return false;
  }

  kcp_session->BindTcpSession(tcp_session);
  if (!kcp_server_->AddSession(kcp_session)) {
    auto existing = kcp_server_->GetSession(kcp_session->GetConvId());
    if (existing != kcp_session) {
      return false;
    }
  }

  std::lock_guard<std::mutex> lock(mutex_);
  kcp_sessions_[session_id] = KcpBinding{ kcp_session, kcp_session->GetConvId() };
  return true;
}

void DualChannelManager::UnbindKcpSession(uint64_t session_id) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = kcp_sessions_.find(session_id);
  if (it == kcp_sessions_.end()) {
    return;
  }
  kcp_server_->RemoveSession(it->second.conv_id);
  kcp_sessions_.erase(it);
}

std::shared_ptr<KcpSession> DualChannelManager::GetKcpSession(uint64_t session_id) const {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = kcp_sessions_.find(session_id);
  if (it == kcp_sessions_.end()) {
    return nullptr;
  }
  return it->second.session.lock();
}

void DualChannelManager::HandleKcpMessage(const std::shared_ptr<KcpSession>& session,
                                          const Packet& packet) {
  if (!session) {
    return;
  }

  if (packet.msg_id == kMsgIdKcpHeartbeat) {
    uint32_t timestamp = static_cast<uint32_t>(mir2::common::now_ms());
    if (!packet.payload.empty()) {
      flatbuffers::Verifier verifier(packet.payload.data(), packet.payload.size());
      if (verifier.VerifyBuffer<mir2::proto::KcpHeartbeat>(nullptr)) {
        const auto* heartbeat =
            flatbuffers::GetRoot<mir2::proto::KcpHeartbeat>(packet.payload.data());
        if (heartbeat) {
          timestamp = heartbeat->timestamp();
        }
      }
    }

    flatbuffers::FlatBufferBuilder builder;
    const auto ack = mir2::proto::CreateKcpHeartbeatAck(builder, timestamp);
    builder.Finish(ack);
    const uint8_t* data = builder.GetBufferPointer();
    std::vector<uint8_t> payload(data, data + builder.GetSize());
    session->Send(kMsgIdKcpHeartbeatAck, payload);
    return;
  }

  auto tcp_session = session->GetTcpSession();
  if (!tcp_session) {
    return;
  }

  kcp_dispatcher_.Dispatch(tcp_session, packet.msg_id, packet.payload);
}

void DualChannelManager::CleanupKcpSessions() {
  std::vector<std::pair<uint64_t, KcpBinding>> snapshot;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    snapshot.reserve(kcp_sessions_.size());
    for (const auto& entry : kcp_sessions_) {
      snapshot.push_back(entry);
    }
  }

  std::vector<uint64_t> expired;
  std::vector<uint32_t> convs_to_remove;
  for (const auto& entry : snapshot) {
    const uint64_t session_id = entry.first;
    const auto& binding = entry.second;
    auto kcp_session = binding.session.lock();
    auto tcp_session = tcp_manager_->GetSession(session_id);
    if (!kcp_session || !tcp_session ||
        tcp_session->GetState() != TcpSession::SessionState::kActive) {
      expired.push_back(session_id);
      convs_to_remove.push_back(binding.conv_id);
    }
  }

  if (!expired.empty()) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (uint64_t session_id : expired) {
      kcp_sessions_.erase(session_id);
    }
  }

  for (uint32_t conv_id : convs_to_remove) {
    kcp_server_->RemoveSession(conv_id);
  }
}

}  // namespace mir2::network
