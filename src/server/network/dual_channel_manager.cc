#include "network/dual_channel_manager.h"

#include <algorithm>
#include <cassert>
#include <utility>
#include <flatbuffers/flatbuffers.h>

#include "common/enums.h"
#include "common/time_utils.h"
#include "monitor/metrics.h"
#include "network/control_message_parser.h"
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
constexpr const char* kMetricKcpBindingCount = "network.kcp.binding_count";
constexpr const char* kMetricKcpStaleBindingRemovedTotal =
    "network.kcp.stale_binding_removed_total";
constexpr const char* kMetricKcpFallbackTotal = "network.send.kcp_fallback_total";

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

  void SetAcceptedConnectionWriteQueueSize(size_t max_write_queue_size) {
    manager_.SetAcceptedConnectionWriteQueueSize(max_write_queue_size);
  }

  void SetAcceptedConnectionWriteBatchOptions(size_t max_batch_bytes,
                                              int64_t flush_interval_us) {
    manager_.SetAcceptedConnectionWriteBatchOptions(
        TcpConnection::WriteBatchOptions{
            .max_batch_bytes = max_batch_bytes,
            .flush_interval_us = flush_interval_us});
  }

  void SetAcceptedConnectionLowCopySendEnabled(bool enabled) {
    manager_.SetAcceptedConnectionLowCopySendEnabled(enabled);
  }

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
  monitor::Metrics::Instance().SetGauge(kMetricKcpBindingCount, 0);
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
  {
    std::lock_guard<std::mutex> lock(mutex_);
    kcp_sessions_.clear();
    PublishKcpBindingCountLocked();
  }
  last_kcp_cleanup_ms_.store(0, std::memory_order_release);
}

void DualChannelManager::RegisterHandler(uint16_t msg_id, MessageHandler handler) {
  if (!handler) {
    return;
  }
  RegisterChannelAwareHandler(
      msg_id,
      [handler = std::move(handler)](const std::shared_ptr<TcpSession>& session,
                                     mir2::common::ChannelType,
                                     const std::vector<uint8_t>& payload) {
        handler(session, payload);
      });
}

void DualChannelManager::RegisterChannelAwareHandler(
    uint16_t msg_id, ChannelMessageHandler handler) {
  if (!handler) {
    return;
  }

  auto shared_handler =
      std::make_shared<ChannelMessageHandler>(std::move(handler));
  tcp_manager_->RegisterHandler(
      msg_id,
      [shared_handler](const std::shared_ptr<TcpSession>& session,
                       const std::vector<uint8_t>& payload) {
        (*shared_handler)(session, mir2::common::ChannelType::kTcp, payload);
      });
  kcp_dispatcher_.RegisterHandler(
      msg_id,
      [shared_handler](const std::shared_ptr<TcpSession>& session,
                       const std::vector<uint8_t>& payload) {
        (*shared_handler)(session, mir2::common::ChannelType::kKcp, payload);
      });
}

void DualChannelManager::SetRoute(uint16_t msg_id, mir2::common::ChannelType channel) {
  router_.SetRoute(msg_id, channel);
}

void DualChannelManager::SetDefaultChannel(mir2::common::ChannelType channel) {
  router_.SetDefaultChannel(channel);
}

void DualChannelManager::SetAcceptedConnectionWriteQueueSize(size_t max_write_queue_size) {
  auto* adapter = dynamic_cast<NetworkManagerAdapter*>(tcp_manager_.get());
  if (!adapter) {
    return;
  }
  adapter->SetAcceptedConnectionWriteQueueSize(max_write_queue_size);
}

void DualChannelManager::SetAcceptedConnectionWriteBatchOptions(
    size_t max_batch_bytes,
    int64_t flush_interval_us) {
  auto* adapter = dynamic_cast<NetworkManagerAdapter*>(tcp_manager_.get());
  if (!adapter) {
    return;
  }
  adapter->SetAcceptedConnectionWriteBatchOptions(
      max_batch_bytes, std::max<int64_t>(flush_interval_us, 0));
}

void DualChannelManager::SetAcceptedConnectionLowCopySendEnabled(bool enabled) {
  auto* adapter = dynamic_cast<NetworkManagerAdapter*>(tcp_manager_.get());
  if (!adapter) {
    return;
  }
  adapter->SetAcceptedConnectionLowCopySendEnabled(enabled);
}

void DualChannelManager::SetKcpCleanupIntervalMs(int64_t interval_ms) {
  kcp_cleanup_interval_ms_ = std::max<int64_t>(interval_ms, 1);
  last_kcp_cleanup_ms_.store(0, std::memory_order_release);
}

void DualChannelManager::Send(uint64_t session_id,
                              uint16_t msg_id,
                              const std::vector<uint8_t>& payload) {
  const bool prefer_kcp = router_.GetChannel(msg_id) == mir2::common::ChannelType::kKcp;
  auto kcp_session = GetKcpSession(session_id);
  bool kcp_available = kcp_session && kcp_session->HasRemoteEndpoint();
  if (kcp_available) {
    auto tcp_session = tcp_manager_->GetSession(session_id);
    if (!tcp_session ||
        tcp_session->GetState() == TcpSession::SessionState::kClosed) {
      RemoveKcpBinding(session_id, /*stale=*/true);
      kcp_session.reset();
      kcp_available = false;
    }
  }
  const auto channel = router_.GetChannelWithFallback(msg_id, kcp_available);

  if (channel == mir2::common::ChannelType::kKcp && kcp_available) {
    kcp_session->Send(msg_id, payload);
    return;
  }

  if (prefer_kcp && !kcp_available) {
    monitor::Metrics::Instance().IncrementCounter(kMetricKcpFallbackTotal);
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
  const int64_t now_ms = static_cast<int64_t>(mir2::common::now_ms());
  const int64_t last_cleanup_ms =
      last_kcp_cleanup_ms_.load(std::memory_order_acquire);
  const int64_t cleanup_interval_ms = std::max<int64_t>(kcp_cleanup_interval_ms_, 1);
  if (last_cleanup_ms != 0 && now_ms >= last_cleanup_ms &&
      now_ms - last_cleanup_ms < cleanup_interval_ms) {
    return;
  }
  last_kcp_cleanup_ms_.store(now_ms, std::memory_order_release);
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

  const uint32_t new_conv_id = kcp_session->GetConvId();
  uint32_t old_conv_id = 0;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = kcp_sessions_.find(session_id);
    if (it != kcp_sessions_.end()) {
      old_conv_id = it->second.conv_id;
    }
    kcp_sessions_[session_id] = KcpBinding{kcp_session, new_conv_id};
    PublishKcpBindingCountLocked();
  }

  if (old_conv_id != 0 && old_conv_id != new_conv_id) {
    kcp_server_->RemoveSession(old_conv_id);
  }
  return true;
}

bool DualChannelManager::TryBindKcpSessionIfAbsent(
    uint64_t session_id,
    const std::shared_ptr<KcpSession>& kcp_session) {
  if (!kcp_session) {
    return false;
  }

  auto tcp_session = tcp_manager_->GetSession(session_id);
  if (!tcp_session) {
    return false;
  }

  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (kcp_sessions_.find(session_id) != kcp_sessions_.end()) {
      return false;
    }
  }

  kcp_session->BindTcpSession(tcp_session);
  if (!kcp_server_->AddSession(kcp_session)) {
    auto existing = kcp_server_->GetSession(kcp_session->GetConvId());
    if (existing != kcp_session) {
      return false;
    }
  }

  bool inserted = false;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    auto [it, ok] = kcp_sessions_.emplace(
        session_id, KcpBinding{kcp_session, kcp_session->GetConvId()});
    if (ok) {
      inserted = true;
      PublishKcpBindingCountLocked();
    }
  }

  if (!inserted) {
    kcp_server_->RemoveSession(kcp_session->GetConvId());
    return false;
  }
  return true;
}

void DualChannelManager::UnbindKcpSession(uint64_t session_id) {
  (void)RemoveKcpBinding(session_id, /*stale=*/false);
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
    (void)ControlMessageParser::ParseKcpHeartbeatTimestamp(
        packet.payload, timestamp, &timestamp);

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

bool DualChannelManager::RemoveKcpBinding(uint64_t session_id, bool stale) {
  uint32_t conv_id = 0;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = kcp_sessions_.find(session_id);
    if (it == kcp_sessions_.end()) {
      return false;
    }
    conv_id = it->second.conv_id;
    kcp_sessions_.erase(it);
    PublishKcpBindingCountLocked();
  }
  if (conv_id != 0) {
    kcp_server_->RemoveSession(conv_id);
  }
  if (stale) {
    monitor::Metrics::Instance().IncrementCounter(kMetricKcpStaleBindingRemovedTotal);
  }
  return true;
}

void DualChannelManager::PublishKcpBindingCountLocked() const {
  monitor::Metrics::Instance().SetGauge(
      kMetricKcpBindingCount, static_cast<double>(kcp_sessions_.size()));
}

void DualChannelManager::CleanupKcpSessions() {
  std::vector<uint64_t> expired;
  std::vector<uint64_t> active;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    expired.reserve(kcp_sessions_.size());
    active.reserve(kcp_sessions_.size());
    for (const auto& [session_id, binding] : kcp_sessions_) {
      if (binding.session.expired()) {
        expired.push_back(session_id);
      } else {
        active.push_back(session_id);
      }
    }
  }

  for (uint64_t session_id : expired) {
    RemoveKcpBinding(session_id, /*stale=*/true);
  }

  std::vector<uint64_t> tcp_gone;
  tcp_gone.reserve(active.size());
  for (uint64_t session_id : active) {
    auto tcp_session = tcp_manager_->GetSession(session_id);
    if (!tcp_session ||
        tcp_session->GetState() == TcpSession::SessionState::kClosed) {
      tcp_gone.push_back(session_id);
    }
  }

  for (uint64_t session_id : tcp_gone) {
    RemoveKcpBinding(session_id, /*stale=*/true);
  }
}

}  // namespace mir2::network
