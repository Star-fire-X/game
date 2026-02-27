#include "gateway/gateway_server.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <utility>
#include <flatbuffers/flatbuffers.h>

#include <asio/post.hpp>
#include <asio/steady_timer.hpp>

#include "common/enums.h"
#include "server/common/error_codes.h"
#include "common/internal_message_helper.h"
#include "common/protocol/message_codec.h"
#include "common/protocol/universal_forward_msg_ids.h"
#include "game_generated.h"
#include "login_generated.h"
#include "network/tcp_session.h"
#include "config/config_manager.h"
#include "log/logger.h"
#include "monitor/metrics.h"
#include "system_generated.h"

namespace mir2::gateway {

namespace {
constexpr float kStaleRouteCleanupIntervalSec = 30.0f;
constexpr uint32_t kDefaultBackpressurePauseMs = 100;
constexpr uint32_t kMaxBackpressurePauseMs = 2000;
constexpr size_t kMaxForwardPayloadBytes = 64 * 1024;
constexpr int64_t kDisconnectRetryInitialBackoffMs = 500;
constexpr int64_t kDisconnectRetryMaxBackoffMs = 30000;
constexpr int64_t kDisconnectRetryTtlMs = 300000;
constexpr size_t kDisconnectRetryMaxQueueSize = 100000;

int64_t ComputeDisconnectBackoffMs(uint32_t retry_count) {
  uint32_t capped_retry = retry_count;
  if (capped_retry > 8) {
    capped_retry = 8;
  }
  const int64_t delay = kDisconnectRetryInitialBackoffMs << capped_retry;
  return std::min(delay, kDisconnectRetryMaxBackoffMs);
}

struct ConnectionSnapshot {
  uint64_t client_id = 0;
  uint32_t player_id = 0;
  uint64_t account_id = 0;
  std::string ip_address;
  uint64_t connected_at_ms = 0;
  uint64_t last_active_ms = 0;
};

std::vector<uint8_t> BuildLoginRateLimitedPayload() {
  common::LoginResponse response;
  response.code = static_cast<mir2::proto::ErrorCode>(
      static_cast<uint16_t>(common::ErrorCode::RATE_LIMITED));
  response.account_id = 0;
  response.session_token.clear();

  common::MessageCodecStatus status = common::MessageCodecStatus::kOk;
  auto payload = common::EncodeLoginResponse(response, &status);
  if (status == common::MessageCodecStatus::kOk) {
    return payload;
  }

  response.code = mir2::proto::ErrorCode::ERR_UNKNOWN;
  return common::EncodeLoginResponse(response, nullptr);
}

std::string ToLowerAscii(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
  return value;
}

bool IsLoopbackHost(const std::string& host) {
  const std::string normalized = ToLowerAscii(host);
  return normalized == "127.0.0.1" || normalized == "::1" || normalized == "localhost";
}

void UpdateSessionContextFromLogicMessage(
    const std::shared_ptr<network::TcpSession>& session,
    uint16_t msg_id,
    const std::vector<uint8_t>& payload) {
  if (!session || payload.empty()) {
    return;
  }

  if (msg_id == static_cast<uint16_t>(common::MsgId::kLoginRsp)) {
    common::LoginResponse response;
    const auto status = common::DecodeLoginResponse(msg_id, payload, &response);
    if (status == common::MessageCodecStatus::kOk && response.account_id != 0) {
      session->SetAccountId(response.account_id);
    }
    return;
  }

  if (msg_id == static_cast<uint16_t>(common::MsgId::kSelectRoleRsp)) {
    flatbuffers::Verifier verifier(payload.data(), payload.size());
    if (!verifier.VerifyBuffer<mir2::proto::SelectRoleRsp>(nullptr)) {
      return;
    }
    const auto* rsp = flatbuffers::GetRoot<mir2::proto::SelectRoleRsp>(payload.data());
    if (rsp &&
        rsp->code() == mir2::proto::ErrorCode::ERR_OK &&
        rsp->player_id() != 0) {
      session->SetUserId(rsp->player_id());
    }
    return;
  }

  if (msg_id == static_cast<uint16_t>(common::MsgId::kEnterGameRsp)) {
    flatbuffers::Verifier verifier(payload.data(), payload.size());
    if (!verifier.VerifyBuffer<mir2::proto::EnterGameRsp>(nullptr)) {
      return;
    }
    const auto* rsp = flatbuffers::GetRoot<mir2::proto::EnterGameRsp>(payload.data());
    if (!rsp) {
      return;
    }
    if (rsp->code() != mir2::proto::ErrorCode::ERR_OK) {
      return;
    }
    const auto* player = rsp->player();
    if (player && player->id() != 0) {
      session->SetUserId(player->id());
    }
  }
}
}  // namespace

bool GatewayServer::Initialize(const std::string& config_path) {
  shutting_down_.store(false, std::memory_order_release);

  if (!config::ConfigManager::Instance().Load(config_path)) {
    return false;
  }

  const auto& log_config = config::ConfigManager::Instance().GetLogConfig();
  if (!log::Logger::Instance().Initialize(log_config.path, log_config.level,
                                          log_config.max_size_mb, log_config.max_files)) {
    return false;
  }

  const auto& server_config = config::ConfigManager::Instance().GetServerConfig();
  const int configured_capacity = server_config.login_ip_rate_limit_capacity;
  const int configured_refill_rate = server_config.login_ip_rate_limit_refill_rate;
  const mir2::security::RateLimiter::Config login_rate_limit_config{
      .capacity = std::max(1, configured_capacity),
      .refill_rate = std::max(1, configured_refill_rate)};
  if (configured_capacity <= 0 || configured_refill_rate <= 0) {
    SYSLOG_WARN(
        "GatewayServer invalid login IP rate limit config "
        "(capacity={}, refill_rate={}), clamped to (capacity={}, refill_rate={})",
        configured_capacity,
        configured_refill_rate,
        login_rate_limit_config.capacity,
        login_rate_limit_config.refill_rate);
  }
  login_ip_rate_limiter_.SetConfig(login_rate_limit_config);

  const int configured_udp_send_fault_inject_every_n =
      server_config.udp_send_fault_inject_every_n;
  const uint64_t udp_send_fault_inject_every_n =
      configured_udp_send_fault_inject_every_n > 0
          ? static_cast<uint64_t>(configured_udp_send_fault_inject_every_n)
          : 0;
  if (configured_udp_send_fault_inject_every_n < 0) {
    SYSLOG_WARN(
        "GatewayServer invalid udp_send_fault_inject_every_n={} (must be >= 0), "
        "fallback to 0",
        configured_udp_send_fault_inject_every_n);
  }

  if (!app_.Initialize(server_config)) {
    SYSLOG_ERROR("GatewayServer application init failed");
    return false;
  }
  monitor::Metrics::Instance().Init(server_config.metrics_port);

  const uint16_t tcp_port = server_config.port;
  uint16_t udp_port = server_config.udp_port;
  if (udp_port == 0) {
    udp_port = static_cast<uint16_t>(tcp_port + 1);
    if (udp_port == 0) {
      udp_port = tcp_port;
    }
  }

  network_ = std::make_unique<network::DualChannelManager>(app_.GetIoContext());
  if (!network_->Start(server_config.bind_ip, tcp_port, udp_port, server_config.max_connections)) {
    SYSLOG_ERROR("GatewayServer network start failed");
    return false;
  }

  if (auto* kcp_server =
          dynamic_cast<network::KcpServer*>(&network_->GetKcpServer())) {
    kcp_server->SetUdpSendFaultInjectEveryN(udp_send_fault_inject_every_n);
    if (udp_send_fault_inject_every_n > 0) {
      SYSLOG_INFO("GatewayServer enabled udp_send fault injection every_n={}",
                  udp_send_fault_inject_every_n);
    }
  } else if (udp_send_fault_inject_every_n > 0) {
    SYSLOG_WARN(
        "GatewayServer failed to apply udp_send fault injection "
        "(every_n={}) because KcpServer implementation is not concrete",
        udp_send_fault_inject_every_n);
  }

  ConnectLogicService();

  RegisterHandlers();
  SYSLOG_INFO("GatewayServer initialized");
  return true;
}

void GatewayServer::Run() {
  if (logic_thread_.joinable()) {
    return;
  }
  logic_thread_ = std::thread([this]() { app_.Run([this](float delta_time) { Tick(delta_time); }); });
}

void GatewayServer::Shutdown() {
  if (shutting_down_.exchange(true, std::memory_order_acq_rel)) {
    return;
  }

  logic_reconnecting_.store(false, std::memory_order_release);
  {
    std::unique_lock<std::shared_mutex> lock(session_map_mutex_);
    pending_disconnect_events_.clear();
  }
  monitor::Metrics::Instance().SetGauge("gateway.disconnect_queue.size", 0);

  if (logic_client_) {
    logic_client_->Close();
  }
  if (network_) {
    network_->Stop();
  }
  app_.Stop();
  if (logic_thread_.joinable()) {
    logic_thread_.join();
  }
  app_.Shutdown();
}

void GatewayServer::Tick(float delta_time) {
    std::vector<std::shared_ptr<network::TcpSession>> sessions;
    if (network_) {
        network_->Tick();
        sessions = network_->GetAllSessions();
        for (const auto& session : sessions) {
            if (!session) {
                continue;
            }
            const uint64_t connection_id = session->GetSessionId();
            if (connection_id == 0) {
                continue;
            }
            if (!GetConnectionSession(connection_id)) {
                RegisterConnection(connection_id, session);
            }
        }
    }

    const int64_t now_ms = network::TcpSession::NowMs();
    ResumeBackpressuredSessions(now_ms);
    CheckHeartbeatTimeouts(sessions, now_ms);
    FlushBufferedMessages();
    ProcessDisconnectRetryQueue(now_ms);
    UpdateHoldingMetrics();

    stale_route_cleanup_elapsed_sec_ += delta_time;
    if (stale_route_cleanup_elapsed_sec_ >= kStaleRouteCleanupIntervalSec) {
        stale_route_cleanup_elapsed_sec_ -= kStaleRouteCleanupIntervalSec;
        CleanupStaleRoutes();
        monitor::Metrics::Instance().SetGauge(
            "gateway.session_count",
            static_cast<int64_t>(GetConnectionCount()));
    }
}

void GatewayServer::CheckHeartbeatTimeouts(
    const std::vector<std::shared_ptr<network::TcpSession>>& sessions,
    int64_t now_ms) {
    if (sessions.empty()) {
        return;
    }
    const int64_t timeout_ms =
        static_cast<int64_t>(config::ConfigManager::Instance()
                                 .GetServerConfig()
                                 .heartbeat_timeout_ms);
    if (timeout_ms <= 0) {
        return;
    }

    for (const auto& session : sessions) {
        if (!session) {
            continue;
        }
        const int64_t last_heartbeat_ms = session->GetLastHeartbeatMs();
        if (now_ms < last_heartbeat_ms ||
            now_ms - last_heartbeat_ms >= timeout_ms) {
            session->Kick(common::ErrorCode::kKickHeartbeatTimeout, "Heartbeat timeout");
            UnregisterSession(session);
        }
    }
}

void GatewayServer::RegisterConnection(uint64_t connection_id,
                                       const std::shared_ptr<network::TcpSession>& session) {
    if (!session) {
        return;
    }

    const uint64_t resolved_id = connection_id != 0 ? connection_id : session->GetSessionId();
    if (resolved_id == 0) {
        return;
    }

    {
        std::unique_lock<std::shared_mutex> lock(session_map_mutex_);
        session_map_[resolved_id] = session;
        auto& holder = connection_holders_[resolved_id];
        if (!holder) {
            holder = std::make_unique<ConnectionHolder>();
        }
        ApplyHolderState(*holder, holder_state_);
        connection_connected_at_ms_[resolved_id] = network::TcpSession::NowMs();
        client_backpressure_until_ms_.erase(resolved_id);
    }

    session->SetDisconnectedHandler([this](const std::shared_ptr<network::TcpSession>& disconnected) {
        UnregisterSession(disconnected);
    });
}

// RegisterUser 已移除：双进程架构下，UserID 由 LogicServer 管理

void GatewayServer::UnregisterSession(const std::shared_ptr<network::TcpSession>& session) {
    if (!session) {
        return;
    }

    const uint64_t connection_id = session->GetSessionId();

    if (network_ && connection_id != 0) {
        network_->UnbindKcpSession(connection_id);
    }

    {
        std::unique_lock<std::shared_mutex> lock(session_map_mutex_);
        if (connection_id != 0) {
            auto it = session_map_.find(connection_id);
            if (it != session_map_.end() && it->second == session) {
                session_map_.erase(it);
            }
            connection_holders_.erase(connection_id);
            connection_connected_at_ms_.erase(connection_id);
            client_backpressure_until_ms_.erase(connection_id);
        }
    }

    monitor::Metrics::Instance().IncrementCounter("gateway.session.unregister");
    NotifyClientDisconnected(connection_id);
}

void GatewayServer::CleanupStaleRoutes() {
    size_t before_connections = 0;
    size_t after_connections = 0;

    {
        std::unique_lock<std::shared_mutex> lock(session_map_mutex_);
        before_connections = session_map_.size();
        std::vector<uint64_t> removed_connections;

        for (auto it = session_map_.begin(); it != session_map_.end();) {
            const auto& session = it->second;
            if (!session || session->GetState() == network::TcpSession::SessionState::kClosed) {
                removed_connections.push_back(it->first);
                it = session_map_.erase(it);
            } else {
                ++it;
            }
        }

        for (uint64_t connection_id : removed_connections) {
            connection_holders_.erase(connection_id);
            connection_connected_at_ms_.erase(connection_id);
            client_backpressure_until_ms_.erase(connection_id);
        }

        after_connections = session_map_.size();
    }

    SYSLOG_INFO("Cleanup stale sessions: {}->{}", before_connections, after_connections);
}

std::shared_ptr<network::TcpSession> GatewayServer::GetConnectionSession(uint64_t connection_id) const {
    std::shared_lock<std::shared_mutex> lock(session_map_mutex_);
    auto it = session_map_.find(connection_id);
    if (it != session_map_.end()) {
        return it->second;
    }
    return nullptr;
}

size_t GatewayServer::GetConnectionCount() const {
    std::shared_lock<std::shared_mutex> lock(session_map_mutex_);
    return session_map_.size();
}

void GatewayServer::RegisterHandlers() {
  if (!network_) {
    return;
  }

  // 心跳消息特殊处理：Gateway 直接响应，不转发
  network_->RegisterHandler(static_cast<uint16_t>(common::MsgId::kHeartbeat),
                            [this](const std::shared_ptr<network::TcpSession>& session,
                                   const std::vector<uint8_t>& payload) {
                              HandleHeartbeat(session, payload);
                            });

  // 通用转发处理器：所有其他消息无脑转发到 LogicServer
  // 双进程架构下，认证检查由 LogicServer 负责
  auto universal_forward_handler = [this](const std::shared_ptr<network::TcpSession>& session,
                                          uint16_t msg_id,
                                          mir2::common::ChannelType channel,
                                          const std::vector<uint8_t>& payload) {
    HandleForwardMessage(session, msg_id, channel, payload);
  };

  // 批量注册需要转发的消息类型（共享矩阵，避免生产/测试漂移）
  for (auto msg_id : common::protocol::kUniversalForwardMsgIds) {
    network_->RegisterChannelAwareHandler(
        msg_id,
        [universal_forward_handler, msg_id](const std::shared_ptr<network::TcpSession>& session,
                                            mir2::common::ChannelType channel,
                                            const std::vector<uint8_t>& payload) {
          universal_forward_handler(session, msg_id, channel, payload);
        });
  }

  SYSLOG_INFO("Gateway registered {} message handlers (universal forward mode)",
              common::protocol::kUniversalForwardMsgIds.size() + 1);
}

bool GatewayServer::ConnectLogicService() {
  StartAsyncConnect();
  return true;
}

void GatewayServer::StartAsyncConnect() {
  asio::post(app_.GetIoContext(), [this]() {
    if (shutting_down_.load(std::memory_order_acquire)) {
      return;
    }
    if (!ConnectToLogicService()) {
      SYSLOG_ERROR("Initial connect failed, scheduling reconnect (logic)");
      ScheduleReconnect(0);
    }
  });
}

bool GatewayServer::IsLogicConnected() const {
  return logic_client_ && logic_client_->IsConnected();
}

bool GatewayServer::ConnectToLogicService() {
  if (shutting_down_.load(std::memory_order_acquire)) {
    return false;
  }

  const auto& services = config::ConfigManager::Instance().GetServiceConfig();

  if (!logic_client_) {
    logic_client_ = std::make_unique<network::TcpClient>(app_.GetIoContext());
  }
  logic_client_->SetPacketHandler([this](const network::Packet& packet) {
    OnLogicPacket(packet);
  });
  logic_client_->SetDisconnectHandler([this]() {
    if (shutting_down_.load(std::memory_order_acquire)) {
      return;
    }
    SYSLOG_ERROR("Logic service disconnected, scheduling reconnect");
    EnterHoldingState();
    ScheduleReconnect(0);
  });

  const auto& logic_endpoint = services.logic;
  const std::string transport = ToLowerAscii(logic_endpoint.transport);
  const bool prefer_uds_in_auto =
      !logic_endpoint.uds_path.empty() && IsLoopbackHost(logic_endpoint.host);
  bool connected = false;
  std::string selected_transport = "tcp";

  if (transport == "uds" || (transport == "auto" && prefer_uds_in_auto)) {
    if (logic_endpoint.uds_path.empty()) {
      if (transport == "uds") {
        SYSLOG_ERROR("Logic transport 'uds' requires non-empty services.logic.uds_path");
        monitor::Metrics::Instance().IncrementCounter("gateway.service.disconnected.logic");
        return false;
      }
    } else {
#if defined(ASIO_HAS_LOCAL_SOCKETS)
      connected = logic_client_->ConnectUnix(logic_endpoint.uds_path);
      if (connected) {
        selected_transport = "uds";
      } else if (transport == "auto") {
        SYSLOG_WARN(
            "Gateway failed to connect to Logic via uds(path={}), fallback to tcp {}:{}",
            logic_endpoint.uds_path,
            logic_endpoint.host,
            logic_endpoint.port);
      } else {
        SYSLOG_ERROR("Failed to connect Logic service via uds path={}",
                     logic_endpoint.uds_path);
      }
#else
      if (transport == "uds") {
        SYSLOG_ERROR("Logic transport 'uds' is not supported on this platform");
        monitor::Metrics::Instance().IncrementCounter("gateway.service.disconnected.logic");
        return false;
      }
      if (transport == "auto") {
        SYSLOG_WARN("Logic uds transport unavailable on this platform, fallback to tcp");
      }
#endif
    }
  }

  if (!connected) {
    connected = logic_client_->Connect(logic_endpoint.host, logic_endpoint.port);
    selected_transport = "tcp";
  }

  if (!connected) {
    SYSLOG_ERROR("Failed to connect Logic service");
    monitor::Metrics::Instance().IncrementCounter("gateway.service.disconnected.logic");
    return false;
  }

  SYSLOG_INFO("Gateway connected to Logic via {} endpoint={}{}", selected_transport,
              selected_transport == "uds" ? logic_endpoint.uds_path : logic_endpoint.host,
              selected_transport == "uds"
                  ? ""
                  : ":" + std::to_string(logic_endpoint.port));
  monitor::Metrics::Instance().IncrementCounter("gateway.service.connected.logic");
  logic_client_->Send(static_cast<uint16_t>(common::InternalMsgId::kServiceHello),
                      common::BuildServiceHello(common::ServiceType::kGateway));
  return true;
}

void GatewayServer::ScheduleReconnect(int retry_count) {
  if (shutting_down_.load(std::memory_order_acquire)) {
    return;
  }

  bool expected = false;
  if (!logic_reconnecting_.compare_exchange_strong(
          expected, true, std::memory_order_acq_rel)) {
    return;
  }

  const int capped_retry = std::min(retry_count, 5);
  const int delay_ms = std::min(1000 * (1 << capped_retry), 30000);
  SYSLOG_INFO("Scheduling reconnect to logic service in {}ms (retry={})",
              delay_ms, retry_count);

  auto timer = std::make_shared<asio::steady_timer>(
      app_.GetIoContext(), std::chrono::milliseconds(delay_ms));
  timer->async_wait([this, retry_count, timer](const asio::error_code& ec) {
    if (shutting_down_.load(std::memory_order_acquire)) {
      logic_reconnecting_.store(false, std::memory_order_release);
      return;
    }

    if (ec) {
      logic_reconnecting_.store(false, std::memory_order_release);
      return;
    }

    if (ConnectToLogicService()) {
      SYSLOG_INFO("Reconnected to logic service");
      logic_reconnecting_.store(false, std::memory_order_release);
      return;
    }

    logic_reconnecting_.store(false, std::memory_order_release);
    ScheduleReconnect(retry_count + 1);
  });
}

bool GatewayServer::ForwardToLogic(uint64_t client_id, uint16_t msg_id,
                                   const std::vector<uint8_t>& payload) {
  if (!logic_client_ || !logic_client_->IsConnected()) {
    SYSLOG_ERROR("Logic service not connected, msg_id={}", msg_id);
    return false;
  }

  monitor::Metrics::Instance().IncrementCounter("gateway.forward.total");
  monitor::Metrics::Instance().IncrementCounter("gateway.forward.service.logic");

  const auto routed = common::BuildRoutedMessage(client_id, msg_id, payload);
  logic_client_->Send(static_cast<uint16_t>(common::InternalMsgId::kRoutedMessage), routed);
  return true;
}

void GatewayServer::NotifyClientDisconnected(uint64_t client_id) {
  if (client_id == 0) {
    return;
  }
  const int64_t now_ms = network::TcpSession::NowMs();
  EnqueueDisconnectEvent(client_id, now_ms);
  ProcessDisconnectRetryQueue(now_ms);
}

void GatewayServer::EnqueueDisconnectEvent(uint64_t client_id, int64_t now_ms) {
  if (client_id == 0) {
    return;
  }

  size_t queue_size = 0;
  {
    std::unique_lock<std::shared_mutex> lock(session_map_mutex_);
    if (pending_disconnect_events_.size() >= kDisconnectRetryMaxQueueSize) {
      pending_disconnect_events_.pop_front();
      monitor::Metrics::Instance().IncrementCounter(
          "gateway.disconnect_queue.dropped_overflow_total");
    }

    PendingDisconnectEvent event{};
    event.client_id = client_id;
    event.sequence = next_disconnect_sequence_++;
    event.first_seen_ms = now_ms;
    event.next_retry_ms = now_ms;
    pending_disconnect_events_.push_back(event);
    queue_size = pending_disconnect_events_.size();
  }

  monitor::Metrics::Instance().IncrementCounter("gateway.disconnect_queue.enqueue_total");
  monitor::Metrics::Instance().SetGauge(
      "gateway.disconnect_queue.size",
      static_cast<int64_t>(queue_size));
}

bool GatewayServer::TrySendDisconnectEvent(
    const PendingDisconnectEvent& event) {
  static const std::vector<uint8_t> kEmptyPayload;
  return ForwardToLogic(event.client_id,
                        static_cast<uint16_t>(common::MsgId::kLogout),
                        kEmptyPayload);
}

void GatewayServer::TrimExpiredDisconnectEvents(int64_t now_ms) {
  size_t expired_count = 0;
  size_t queue_size = 0;
  {
    std::unique_lock<std::shared_mutex> lock(session_map_mutex_);
    for (auto it = pending_disconnect_events_.begin();
         it != pending_disconnect_events_.end();) {
      if (now_ms - it->first_seen_ms < kDisconnectRetryTtlMs) {
        ++it;
        continue;
      }
      it = pending_disconnect_events_.erase(it);
      ++expired_count;
    }
    queue_size = pending_disconnect_events_.size();
  }

  for (size_t i = 0; i < expired_count; ++i) {
    monitor::Metrics::Instance().IncrementCounter("gateway.disconnect_queue.expired_total");
  }
  monitor::Metrics::Instance().SetGauge(
      "gateway.disconnect_queue.size",
      static_cast<int64_t>(queue_size));
}

void GatewayServer::ProcessDisconnectRetryQueue(int64_t now_ms) {
  TrimExpiredDisconnectEvents(now_ms);
  if (!IsLogicConnected()) {
    return;
  }

  std::vector<PendingDisconnectEvent> due_events;
  {
    std::unique_lock<std::shared_mutex> lock(session_map_mutex_);
    for (auto it = pending_disconnect_events_.begin();
         it != pending_disconnect_events_.end();) {
      if (it->next_retry_ms > now_ms) {
        ++it;
        continue;
      }
      due_events.push_back(*it);
      it = pending_disconnect_events_.erase(it);
    }
  }

  if (due_events.empty()) {
    return;
  }

  for (auto& event : due_events) {
    if (TrySendDisconnectEvent(event)) {
      monitor::Metrics::Instance().IncrementCounter(
          "gateway.disconnect_queue.delivered_total");
      continue;
    }

    monitor::Metrics::Instance().IncrementCounter("gateway.disconnect_queue.retry_total");
    event.retry_count += 1;
    event.next_retry_ms = now_ms + ComputeDisconnectBackoffMs(event.retry_count);

    bool dropped_expired = false;
    bool dropped_overflow = false;
    {
      std::unique_lock<std::shared_mutex> lock(session_map_mutex_);
      if (now_ms - event.first_seen_ms >= kDisconnectRetryTtlMs) {
        dropped_expired = true;
      } else {
        if (pending_disconnect_events_.size() >= kDisconnectRetryMaxQueueSize) {
          pending_disconnect_events_.pop_front();
          dropped_overflow = true;
        }
        pending_disconnect_events_.push_back(event);
      }
    }
    if (dropped_expired) {
      monitor::Metrics::Instance().IncrementCounter("gateway.disconnect_queue.expired_total");
    }
    if (dropped_overflow) {
      monitor::Metrics::Instance().IncrementCounter(
          "gateway.disconnect_queue.dropped_overflow_total");
    }
  }

  size_t queue_size = 0;
  {
    std::shared_lock<std::shared_mutex> lock(session_map_mutex_);
    queue_size = pending_disconnect_events_.size();
  }
  monitor::Metrics::Instance().SetGauge(
      "gateway.disconnect_queue.size",
      static_cast<int64_t>(queue_size));
}

void GatewayServer::UpdateHoldingMetrics() {
  size_t total_buffered_bytes = 0;
  size_t queue_size = 0;
  {
    std::shared_lock<std::shared_mutex> lock(session_map_mutex_);
    for (const auto& [_, holder] : connection_holders_) {
      if (holder) {
        total_buffered_bytes += holder->BufferedBytes();
      }
    }
    queue_size = pending_disconnect_events_.size();
  }

  monitor::Metrics::Instance().SetGauge(
      "gateway.holding.buffer_size_bytes",
      static_cast<int64_t>(total_buffered_bytes));
  monitor::Metrics::Instance().SetGauge(
      "gateway.holding.total_buffer_bytes",
      static_cast<int64_t>(total_buffered_bytes));
  monitor::Metrics::Instance().SetGauge(
      "gateway.disconnect_queue.size",
      static_cast<int64_t>(queue_size));
}

void GatewayServer::OnLogicPacket(const network::Packet& packet) {
  if (!network_) {
    return;
  }

  if (packet.msg_id == static_cast<uint16_t>(common::InternalMsgId::kServiceHelloAck)) {
    common::ServiceType remote = common::ServiceType::kGateway;
    bool ok = false;
    if (common::ParseServiceHelloAck(packet.payload, &remote, &ok)) {
      SYSLOG_INFO("Logic service handshake ack: remote={} ok={}",
                  static_cast<int>(remote), ok);
    }
    return;
  }

  if (packet.msg_id == static_cast<uint16_t>(common::InternalMsgId::kContextRestore)) {
    flatbuffers::Verifier verifier(packet.payload.data(), packet.payload.size());
    if (!verifier.VerifyBuffer<mir2::proto::ContextRestore>(nullptr)) {
      SYSLOG_WARN("Gateway failed to verify ContextRestore request");
      return;
    }
    const auto* request =
        flatbuffers::GetRoot<mir2::proto::ContextRestore>(packet.payload.data());
    if (!request) {
      return;
    }
    const uint32_t request_id = request->request_id();
    const auto response_payload = BuildContextRestoreResponse(request_id);
    if (logic_client_) {
      logic_client_->Send(static_cast<uint16_t>(common::InternalMsgId::kContextRestore),
                          response_payload);
    }
    EnterRestoringState();
    return;
  }

  if (packet.msg_id == static_cast<uint16_t>(common::InternalMsgId::kLogicReady)) {
    flatbuffers::Verifier verifier(packet.payload.data(), packet.payload.size());
    if (!verifier.VerifyBuffer<mir2::proto::LogicReady>(nullptr)) {
      SYSLOG_WARN("Gateway failed to verify LogicReady");
      return;
    }
    const auto* ready = flatbuffers::GetRoot<mir2::proto::LogicReady>(packet.payload.data());
    if (!ready) {
      return;
    }
    SYSLOG_INFO("LogicReady received (prewarm_count={}, duration_ms={})",
                ready->prewarm_count(), ready->prewarm_duration_ms());
    if (network_) {
      flatbuffers::FlatBufferBuilder builder;
      const uint8_t reason = 0;  // LogicRestart
      const uint64_t timestamp_ms = static_cast<uint64_t>(network::TcpSession::NowMs());
      const auto reset = mir2::proto::CreateKcpReset(builder, reason, timestamp_ms);
      builder.Finish(reset);
      const uint8_t* data = builder.GetBufferPointer();
      std::vector<uint8_t> payload(data, data + builder.GetSize());
      network_->Broadcast(static_cast<uint16_t>(common::InternalMsgId::kKcpReset), payload);
      SYSLOG_INFO("Broadcast KcpReset to {} sessions", network_->GetConnectionCount());
    }
    EnterFlushingState();
    return;
  }

  if (packet.msg_id ==
      static_cast<uint16_t>(common::InternalMsgId::kBackpressureControl)) {
    HandleBackpressureControl(packet.payload);
    return;
  }

  if (packet.msg_id != static_cast<uint16_t>(common::InternalMsgId::kRoutedMessage)) {
    return;
  }

  common::RoutedMessageData routed;
  if (!common::ParseRoutedMessage(packet.payload, &routed)) {
    SYSLOG_ERROR("Failed to parse routed message from service");
    return;
  }

  auto session = network_->GetSession(routed.client_id);
  if (!session) {
    SYSLOG_ERROR("Client session not found, client_id={}", routed.client_id);
    return;
  }
  UpdateSessionContextFromLogicMessage(session, routed.msg_id, routed.payload);
  network_->Send(routed.client_id, routed.msg_id, routed.payload);
}

void GatewayServer::HandleBackpressureControl(const std::vector<uint8_t>& payload) {
  if (payload.empty()) {
    return;
  }

  flatbuffers::Verifier verifier(payload.data(), payload.size());
  if (!verifier.VerifyBuffer<mir2::proto::BackpressureControl>(nullptr)) {
    SYSLOG_WARN("Gateway failed to verify BackpressureControl");
    return;
  }

  const auto* control =
      flatbuffers::GetRoot<mir2::proto::BackpressureControl>(payload.data());
  if (!control || control->client_id() == 0) {
    return;
  }

  const uint64_t client_id = control->client_id();
  const auto action = control->action();
  auto session = GetConnectionSession(client_id);
  if (!session) {
    SYSLOG_DEBUG("Gateway backpressure target missing (client_id={})", client_id);
    return;
  }

  if (action == mir2::proto::BackpressureAction::RESUME_READ) {
    session->ResumeRead();
    {
      std::unique_lock<std::shared_mutex> lock(session_map_mutex_);
      client_backpressure_until_ms_.erase(client_id);
    }
    monitor::Metrics::Instance().IncrementCounter("gateway.backpressure.resume_total");
    return;
  }

  const uint32_t pause_ms = std::clamp(
      control->duration_ms() == 0 ? kDefaultBackpressurePauseMs : control->duration_ms(),
      1u,
      kMaxBackpressurePauseMs);
  const int64_t until_ms = network::TcpSession::NowMs() + pause_ms;

  session->PauseRead();
  {
    std::unique_lock<std::shared_mutex> lock(session_map_mutex_);
    auto& existing = client_backpressure_until_ms_[client_id];
    if (existing < until_ms) {
      existing = until_ms;
    }
  }
  monitor::Metrics::Instance().IncrementCounter("gateway.backpressure.pause_total");
}

void GatewayServer::ResumeBackpressuredSessions(int64_t now_ms) {
  std::vector<std::shared_ptr<network::TcpSession>> sessions_to_resume;
  {
    std::unique_lock<std::shared_mutex> lock(session_map_mutex_);
    for (auto it = client_backpressure_until_ms_.begin();
         it != client_backpressure_until_ms_.end();) {
      if (now_ms < it->second) {
        ++it;
        continue;
      }
      auto session_it = session_map_.find(it->first);
      if (session_it != session_map_.end() && session_it->second) {
        sessions_to_resume.push_back(session_it->second);
      }
      it = client_backpressure_until_ms_.erase(it);
    }
  }

  for (const auto& session : sessions_to_resume) {
    if (session) {
      session->ResumeRead();
      monitor::Metrics::Instance().IncrementCounter("gateway.backpressure.resume_total");
    }
  }
}

std::vector<uint8_t> GatewayServer::BuildContextRestoreResponse(uint32_t request_id) const {
  std::vector<ConnectionSnapshot> snapshot;
  {
    std::shared_lock<std::shared_mutex> lock(session_map_mutex_);
    snapshot.reserve(session_map_.size());
    for (const auto& [client_id, session] : session_map_) {
      if (!session || session->GetState() == network::TcpSession::SessionState::kClosed) {
        continue;
      }
      ConnectionSnapshot entry;
      entry.client_id = client_id;
      entry.player_id = static_cast<uint32_t>(session->GetUserId());
      entry.account_id = session->GetAccountId();
      entry.ip_address = session->GetRemoteAddress();
      auto it = connection_connected_at_ms_.find(client_id);
      entry.connected_at_ms = it != connection_connected_at_ms_.end()
          ? static_cast<uint64_t>(it->second)
          : static_cast<uint64_t>(network::TcpSession::NowMs());
      entry.last_active_ms = static_cast<uint64_t>(session->GetLastHeartbeatMs());
      snapshot.push_back(std::move(entry));
    }
  }

  flatbuffers::FlatBufferBuilder builder;
  std::vector<flatbuffers::Offset<mir2::proto::ConnectionContext>> contexts;
  contexts.reserve(snapshot.size());
  for (const auto& entry : snapshot) {
    const auto ip_offset = builder.CreateString(entry.ip_address);
    contexts.push_back(mir2::proto::CreateConnectionContext(
        builder,
        entry.client_id,
        entry.player_id,
        entry.account_id,
        ip_offset,
        entry.connected_at_ms,
        entry.last_active_ms));
  }

  const auto connections_offset = builder.CreateVector(contexts);
  const auto response = mir2::proto::CreateContextRestoreResponse(
      builder,
      request_id,
      connections_offset,
      static_cast<uint32_t>(snapshot.size()));
  builder.Finish(response);

  const uint8_t* data = builder.GetBufferPointer();
  return std::vector<uint8_t>(data, data + builder.GetSize());
}

void GatewayServer::EnterHoldingState() {
  std::unique_lock<std::shared_mutex> lock(session_map_mutex_);
  if (holder_state_ == ConnectionHolder::State::HOLDING) {
    return;
  }
  holder_state_ = ConnectionHolder::State::HOLDING;
  for (auto& [_, holder] : connection_holders_) {
    if (holder) {
      ApplyHolderState(*holder, holder_state_);
    }
  }
}

void GatewayServer::EnterRestoringState() {
  std::unique_lock<std::shared_mutex> lock(session_map_mutex_);
  if (holder_state_ == ConnectionHolder::State::FLUSHING ||
      holder_state_ == ConnectionHolder::State::RESTORING) {
    return;
  }
  holder_state_ = ConnectionHolder::State::RESTORING;
  for (auto& [_, holder] : connection_holders_) {
    if (holder) {
      ApplyHolderState(*holder, holder_state_);
    }
  }
}

void GatewayServer::EnterFlushingState() {
  std::unique_lock<std::shared_mutex> lock(session_map_mutex_);
  if (holder_state_ == ConnectionHolder::State::FLUSHING) {
    return;
  }
  holder_state_ = ConnectionHolder::State::FLUSHING;
  for (auto& [_, holder] : connection_holders_) {
    if (holder) {
      ApplyHolderState(*holder, holder_state_);
    }
  }
}

void GatewayServer::FlushBufferedMessages() {
  if (!IsLogicConnected()) {
    return;
  }

  std::vector<std::pair<uint64_t, BufferedMessage>> drained;
  {
    std::unique_lock<std::shared_mutex> lock(session_map_mutex_);
    if (holder_state_ != ConnectionHolder::State::FLUSHING) {
      return;
    }

    for (auto& [client_id, holder] : connection_holders_) {
      if (!holder) {
        continue;
      }
      BufferedMessage message{};
      while (holder->PopBufferedMessage(&message)) {
        drained.emplace_back(client_id, std::move(message));
        message = BufferedMessage{};
      }
    }

    bool empty = true;
    for (const auto& [_, holder] : connection_holders_) {
      if (holder && holder->HasBufferedMessages()) {
        empty = false;
        break;
      }
    }

    if (empty) {
      holder_state_ = ConnectionHolder::State::FORWARDING;
      for (auto& [_, holder] : connection_holders_) {
        if (holder) {
          ApplyHolderState(*holder, holder_state_);
        }
      }
    }
  }

  if (drained.empty()) {
    return;
  }

  asio::post(app_.GetIoContext(),
             [this, drained = std::move(drained)]() mutable {
               for (auto& entry : drained) {
                 ForwardToLogic(entry.first, entry.second.msg_id, entry.second.payload);
               }
             });
}

void GatewayServer::ApplyHolderState(ConnectionHolder& holder,
                                     ConnectionHolder::State target) {
  switch (target) {
    case ConnectionHolder::State::FORWARDING:
      if (holder.GetState() == ConnectionHolder::State::HOLDING) {
        holder.EnterRestoring();
      }
      if (holder.GetState() == ConnectionHolder::State::RESTORING) {
        holder.EnterFlushing();
      }
      if (holder.GetState() == ConnectionHolder::State::FLUSHING) {
        holder.ReturnToForwarding();
      }
      break;
    case ConnectionHolder::State::HOLDING:
      holder.EnterHolding();
      break;
    case ConnectionHolder::State::RESTORING:
      if (holder.GetState() == ConnectionHolder::State::FORWARDING) {
        holder.EnterHolding();
      }
      if (holder.GetState() == ConnectionHolder::State::HOLDING) {
        holder.EnterRestoring();
      }
      break;
    case ConnectionHolder::State::FLUSHING:
      if (holder.GetState() == ConnectionHolder::State::FORWARDING) {
        holder.EnterHolding();
      }
      if (holder.GetState() == ConnectionHolder::State::HOLDING) {
        holder.EnterRestoring();
      }
      if (holder.GetState() == ConnectionHolder::State::RESTORING) {
        holder.EnterFlushing();
      }
      break;
  }
}

network::TcpClient* GatewayServer::GetLogicClient() const {
  return logic_client_.get();
}

void GatewayServer::HandleHeartbeat(const std::shared_ptr<network::TcpSession>& session,
                                    const std::vector<uint8_t>& payload) {
  if (!session) {
    return;
  }

  uint32_t seq = 0;
  if (!payload.empty()) {
    flatbuffers::Verifier verifier(payload.data(), payload.size());
    if (verifier.VerifyBuffer<mir2::proto::Heartbeat>(nullptr)) {
      const auto* hb = flatbuffers::GetRoot<mir2::proto::Heartbeat>(payload.data());
      if (hb) {
        seq = hb->seq();
      }
    }
  }

  const uint32_t server_time = static_cast<uint32_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::steady_clock::now().time_since_epoch())
          .count());

  flatbuffers::FlatBufferBuilder builder;
  const auto rsp = mir2::proto::CreateHeartbeatRsp(builder, seq, server_time);
  builder.Finish(rsp);

  const uint8_t* data = builder.GetBufferPointer();
  std::vector<uint8_t> rsp_payload(data, data + builder.GetSize());

  if (network_) {
    network_->Send(session->GetSessionId(),
                   static_cast<uint16_t>(common::MsgId::kHeartbeatRsp),
                   rsp_payload);
  } else {
    session->Send(static_cast<uint16_t>(common::MsgId::kHeartbeatRsp), rsp_payload);
  }
}

void GatewayServer::HandleForwardMessage(const std::shared_ptr<network::TcpSession>& session,
                                         uint16_t msg_id,
                                         mir2::common::ChannelType channel,
                                         const std::vector<uint8_t>& payload) {
  if (!session) {
    return;
  }

  const uint64_t client_id = session->GetSessionId();
  if (client_id == 0) {
    SYSLOG_WARN("Invalid session ID, dropping message msg_id={}", msg_id);
    return;
  }

  if (payload.size() > kMaxForwardPayloadBytes) {
    SYSLOG_WARN(
        "Gateway dropped oversized payload client_id={} msg_id={} channel={} size={}",
        client_id,
        msg_id,
        static_cast<int>(channel),
        payload.size());
    monitor::Metrics::Instance().IncrementCounter("gateway.forward.payload_too_large");
    session->Kick(common::ErrorCode::INVALID_PACKET, "Payload too large");
    return;
  }

  if (msg_id == static_cast<uint16_t>(common::MsgId::kLoginReq)) {
    std::string rate_limit_key = session->GetRemoteAddress();
    if (rate_limit_key.empty()) {
      rate_limit_key = "client:" + std::to_string(client_id);
    }

    if (!login_ip_rate_limiter_.TryAcquire(rate_limit_key)) {
      SYSLOG_WARN("Gateway login rate limited ip={} client_id={}",
                  rate_limit_key,
                  client_id);
      monitor::Metrics::Instance().IncrementCounter(
          "gateway.login.ip_rate_limited_total");
      const auto limited_payload = BuildLoginRateLimitedPayload();
      if (network_) {
        network_->Send(client_id,
                       static_cast<uint16_t>(common::MsgId::kLoginRsp),
                       limited_payload);
      } else {
        session->Send(static_cast<uint16_t>(common::MsgId::kLoginRsp), limited_payload);
      }
      return;
    }
  }

  // 检查 LogicServer 连接状态
  if (!IsLogicConnected()) {
    EnterHoldingState();
  }

  // 通过 ConnectionHolder 处理消息（缓冲或转发）
  ConnectionHolder::Action action = ConnectionHolder::Action::kForward;
  {
    std::unique_lock<std::shared_mutex> lock(session_map_mutex_);
    auto& holder = connection_holders_[client_id];
    if (!holder) {
      holder = std::make_unique<ConnectionHolder>();
      ApplyHolderState(*holder, holder_state_);
      connection_connected_at_ms_.try_emplace(client_id, network::TcpSession::NowMs());
    }
    action = holder->HandleClientMessage(msg_id, channel, payload);
  }

  // 根据 ConnectionHolder 的决策执行操作
  switch (action) {
    case ConnectionHolder::Action::kForward:
      if (!IsLogicConnected()) {
        SYSLOG_ERROR("Logic service not connected, msg_id={}", msg_id);
        return;
      }
      ForwardToLogic(client_id, msg_id, payload);
      break;
    case ConnectionHolder::Action::kBuffered:
      // 消息已缓冲，等待 LogicServer 恢复
      break;
    case ConnectionHolder::Action::kDroppedRealtime:
      // 实时消息已丢弃（如移动消息）
      break;
    case ConnectionHolder::Action::kDisconnect:
      // 缓冲区溢出，踢下线
      session->Kick(common::ErrorCode::kKickAdminManual, "Gateway holding overflow");
      break;
  }
}

}  // namespace mir2::gateway
