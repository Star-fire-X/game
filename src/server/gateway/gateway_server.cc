#include "gateway/gateway_server.h"

#include <algorithm>
#include <chrono>
#include <flatbuffers/flatbuffers.h>

#include <asio/post.hpp>
#include <asio/steady_timer.hpp>

#include "common/enums.h"
#include "server/common/error_codes.h"
#include "common/internal_message_helper.h"
#include "config/config_manager.h"
#include "log/logger.h"
#include "monitor/metrics.h"
#include "system_generated.h"

namespace mir2::gateway {

namespace {
// 硬编码路由规则已迁移到 MessageRouter，保留此命名空间用于未来工具函数
constexpr float kStaleRouteCleanupIntervalSec = 30.0f;
}  // namespace

bool GatewayServer::Initialize(const std::string& config_path) {
  if (!config::ConfigManager::Instance().Load(config_path)) {
    return false;
  }

  const auto& log_config = config::ConfigManager::Instance().GetLogConfig();
  if (!log::Logger::Instance().Initialize(log_config.path, log_config.level,
                                          log_config.max_size_mb, log_config.max_files)) {
    return false;
  }

  const auto& server_config = config::ConfigManager::Instance().GetServerConfig();
  if (!app_.Initialize(server_config)) {
    SYSLOG_ERROR("GatewayServer application init failed");
    return false;
  }
  monitor::Metrics::Instance().Init(server_config.metrics_port);

  network_ = std::make_unique<network::NetworkManager>(app_.GetIoContext());
  if (!network_->Start(server_config.bind_ip, server_config.port, server_config.max_connections)) {
    SYSLOG_ERROR("GatewayServer network start failed");
    return false;
  }

  ConnectServices();

  // 加载消息路由表（先尝试从配置加载，失败则使用默认路由）
  if (!message_router_.LoadRoutesFromConfig(config_path)) {
    SYSLOG_WARN("Failed to load message routes from config, using default routes");
  }

  // 如果配置未提供路由表，注册默认路由规则
  if (message_router_.GetRouteCount() == 0) {
    RegisterDefaultRoutes();
  }

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
  if (world_client_) {
    world_client_->Close();
  }
  if (game_client_) {
    game_client_->Close();
  }
  if (db_client_) {
    db_client_->Close();
  }
  if (network_) {
    network_->Stop();
  }
  app_.Stop();
  if (logic_thread_.joinable()) {
    logic_thread_.join();
  }
  app_.Shutdown();
  log::Logger::Instance().Shutdown();
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
    CheckHeartbeatTimeouts(sessions, now_ms);

    stale_route_cleanup_elapsed_sec_ += delta_time;
    if (stale_route_cleanup_elapsed_sec_ >= kStaleRouteCleanupIntervalSec) {
        stale_route_cleanup_elapsed_sec_ -= kStaleRouteCleanupIntervalSec;
        CleanupStaleRoutes();
        monitor::Metrics::Instance().SetGauge(
            "gateway.route_table.connection_count",
            static_cast<int64_t>(GetConnectionRouteCount()));
        monitor::Metrics::Instance().SetGauge("gateway.route_table.user_count",
                                              static_cast<int64_t>(GetUserRouteCount()));
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
        std::unique_lock<std::shared_mutex> lock(route_table_mutex_);
        connection_route_table_[resolved_id] = session;
    }

    session->SetDisconnectedHandler([this](const std::shared_ptr<network::TcpSession>& disconnected) {
        UnregisterSession(disconnected);
    });
}

void GatewayServer::RegisterUser(uint64_t user_id,
                                 const std::shared_ptr<network::TcpSession>& session) {
    if (!session || user_id == 0) {
        return;
    }
    if (session->GetAuthState() != network::TcpSession::AuthState::kAuthed) {
        return;
    }

    {
        std::unique_lock<std::shared_mutex> lock(route_table_mutex_);
        auto it = user_route_table_.find(user_id);
        if (it != user_route_table_.end() && it->second && it->second != session) {
            SYSLOG_WARN("Duplicate login detected, kicking previous session (user_id={})", user_id);
            it->second->SetUserId(0);
            it->second->Kick(common::ErrorCode::kKickDuplicateLogin, "Duplicate login");
        }
        user_route_table_[user_id] = session;
        session->SetUserId(user_id);
    }

    monitor::Metrics::Instance().IncrementCounter("gateway.user.register");
}

void GatewayServer::UnregisterSession(const std::shared_ptr<network::TcpSession>& session) {
    if (!session) {
        return;
    }

    const uint64_t connection_id = session->GetSessionId();
    const uint64_t user_id = session->GetUserId();

    {
        std::unique_lock<std::shared_mutex> lock(route_table_mutex_);
        if (connection_id != 0) {
            auto it = connection_route_table_.find(connection_id);
            if (it != connection_route_table_.end() && it->second == session) {
                connection_route_table_.erase(it);
            }
        }

        if (user_id != 0) {
            auto it = user_route_table_.find(user_id);
            if (it != user_route_table_.end() && it->second == session) {
                user_route_table_.erase(it);
            }
        } else {
            for (auto it = user_route_table_.begin(); it != user_route_table_.end();) {
                if (it->second == session) {
                    it = user_route_table_.erase(it);
                } else {
                    ++it;
                }
            }
        }
    }

    session->SetUserId(0);

    monitor::Metrics::Instance().IncrementCounter("gateway.session.unregister");
    NotifyClientDisconnected(connection_id);
}

void GatewayServer::CleanupStaleRoutes() {
    size_t before_connections = 0;
    size_t before_users = 0;
    size_t after_connections = 0;
    size_t after_users = 0;

    {
        std::unique_lock<std::shared_mutex> lock(route_table_mutex_);
        before_connections = connection_route_table_.size();
        before_users = user_route_table_.size();

        for (auto it = connection_route_table_.begin(); it != connection_route_table_.end();) {
            const auto& session = it->second;
            if (!session || session->GetState() == network::TcpSession::SessionState::kClosed) {
                it = connection_route_table_.erase(it);
            } else {
                ++it;
            }
        }

        for (auto it = user_route_table_.begin(); it != user_route_table_.end();) {
            const auto& session = it->second;
            if (!session || session->GetState() == network::TcpSession::SessionState::kClosed) {
                if (session) {
                    session->SetUserId(0);
                }
                it = user_route_table_.erase(it);
            } else {
                ++it;
            }
        }

        after_connections = connection_route_table_.size();
        after_users = user_route_table_.size();
    }

    SYSLOG_INFO("Cleanup stale routes: connection {}->{} user {}->{}",
                before_connections, after_connections, before_users, after_users);
}

std::shared_ptr<network::TcpSession> GatewayServer::GetConnectionSession(uint64_t connection_id) const {
    std::shared_lock<std::shared_mutex> lock(route_table_mutex_);
    auto it = connection_route_table_.find(connection_id);
    if (it != connection_route_table_.end()) {
        return it->second;
    }
    return nullptr;
}

std::shared_ptr<network::TcpSession> GatewayServer::GetUserSession(uint64_t user_id) const {
    std::shared_lock<std::shared_mutex> lock(route_table_mutex_);
    auto it = user_route_table_.find(user_id);
    if (it != user_route_table_.end()) {
        return it->second;
    }
    return nullptr;
}

size_t GatewayServer::GetConnectionRouteCount() const {
    std::shared_lock<std::shared_mutex> lock(route_table_mutex_);
    return connection_route_table_.size();
}

size_t GatewayServer::GetUserRouteCount() const {
    std::shared_lock<std::shared_mutex> lock(route_table_mutex_);
    return user_route_table_.size();
}

void GatewayServer::RegisterDefaultRoutes() {
  // DB 服务消息（不需要认证）
  message_router_.RegisterRoute(static_cast<uint16_t>(common::MsgId::kLoginReq),
                                common::ServiceType::kDb, false);

  // Logout 由网关显式处理并通知 World/Game 服务。

  // World 服务消息（需要认证）
  message_router_.RegisterRoute(static_cast<uint16_t>(common::MsgId::kCreateRoleReq),
                                common::ServiceType::kWorld, true);
  message_router_.RegisterRoute(static_cast<uint16_t>(common::MsgId::kSelectRoleReq),
                                common::ServiceType::kWorld, true);
  message_router_.RegisterRoute(static_cast<uint16_t>(common::MsgId::kRoleListReq),
                                common::ServiceType::kWorld, true);

  // Game 服务消息（需要认证）
  message_router_.RegisterRoute(static_cast<uint16_t>(common::MsgId::kMoveReq),
                                common::ServiceType::kGame, true);
  message_router_.RegisterRoute(static_cast<uint16_t>(common::MsgId::kAttackReq),
                                common::ServiceType::kGame, true);
  message_router_.RegisterRoute(static_cast<uint16_t>(common::MsgId::kSkillReq),
                                common::ServiceType::kGame, true);

  SYSLOG_INFO("Registered {} default message routes", message_router_.GetRouteCount());
}

void GatewayServer::RegisterHandlers() {
  if (!network_) {
    return;
  }

  network_->RegisterHandler(static_cast<uint16_t>(common::MsgId::kHeartbeat),
                            [this](const std::shared_ptr<network::TcpSession>& session,
                                   const std::vector<uint8_t>& payload) {
                              if (!session) {
                                return;
                              }
                              uint32_t seq = 0;
                              if (!payload.empty()) {
                                flatbuffers::Verifier verifier(payload.data(), payload.size());
                                if (verifier.VerifyBuffer<mir2::proto::Heartbeat>(nullptr)) {
                                  const auto* hb =
                                      flatbuffers::GetRoot<mir2::proto::Heartbeat>(payload.data());
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
                              const auto rsp = mir2::proto::CreateHeartbeatRsp(
                                  builder, seq, server_time);
                              builder.Finish(rsp);
                              const uint8_t* data = builder.GetBufferPointer();
                              std::vector<uint8_t> rsp_payload(data, data + builder.GetSize());
                              session->Send(static_cast<uint16_t>(common::MsgId::kHeartbeatRsp),
                                            rsp_payload);
                            });

  auto forward_handler = [this](const std::shared_ptr<network::TcpSession>& session,
                                uint16_t msg_id,
                                const std::vector<uint8_t>& payload) {
    if (!session) {
      return;
    }

    // 使用动态路由表查找目标服务
    const auto target = message_router_.GetRouteTarget(msg_id);
    if (!target) {
      SYSLOG_WARN("No route found for msg_id={}, dropping message", msg_id);
      return;
    }

    if (!IsServiceConnected(*target)) {
      SYSLOG_ERROR("Service not connected, msg_id={} target={}",
                   msg_id, static_cast<int>(*target));
      return;
    }

    // 检查认证要求
    if (message_router_.RequiresAuth(msg_id) &&
        session->GetAuthState() != network::TcpSession::AuthState::kAuthed) {
      SYSLOG_WARN("Unauthorized message msg_id={} from session={}, dropping", msg_id, session->GetSessionId());
      return;
    }

    const uint64_t client_id = session->GetSessionId();
    ForwardToService(*target, client_id, msg_id, payload);
  };

  network_->RegisterHandler(static_cast<uint16_t>(common::MsgId::kLoginReq),
                            [forward_handler](const std::shared_ptr<network::TcpSession>& session,
                                              const std::vector<uint8_t>& payload) {
                              forward_handler(session,
                                              static_cast<uint16_t>(common::MsgId::kLoginReq),
                                              payload);
                            });

  network_->RegisterHandler(static_cast<uint16_t>(common::MsgId::kLogout),
                            [this](const std::shared_ptr<network::TcpSession>& session,
                                   const std::vector<uint8_t>& payload) {
                              if (!session) {
                                return;
                              }
                              if (session->GetAuthState() != network::TcpSession::AuthState::kAuthed) {
                                SYSLOG_WARN("Unauthorized logout from session={}, dropping",
                                            session->GetSessionId());
                                return;
                              }
                              const uint64_t client_id = session->GetSessionId();
                              ForwardToService(common::ServiceType::kWorld, client_id,
                                               static_cast<uint16_t>(common::MsgId::kLogout),
                                               payload);
                              ForwardToService(common::ServiceType::kGame, client_id,
                                               static_cast<uint16_t>(common::MsgId::kLogout),
                                               payload);
                            });

  network_->RegisterHandler(static_cast<uint16_t>(common::MsgId::kCreateRoleReq),
                            [forward_handler](const std::shared_ptr<network::TcpSession>& session,
                                              const std::vector<uint8_t>& payload) {
                              forward_handler(session,
                                              static_cast<uint16_t>(common::MsgId::kCreateRoleReq),
                                              payload);
                            });

  network_->RegisterHandler(static_cast<uint16_t>(common::MsgId::kSelectRoleReq),
                            [forward_handler](const std::shared_ptr<network::TcpSession>& session,
                                              const std::vector<uint8_t>& payload) {
                              forward_handler(session,
                                              static_cast<uint16_t>(common::MsgId::kSelectRoleReq),
                                              payload);
                            });

  network_->RegisterHandler(static_cast<uint16_t>(common::MsgId::kRoleListReq),
                            [forward_handler](const std::shared_ptr<network::TcpSession>& session,
                                              const std::vector<uint8_t>& payload) {
                              forward_handler(session,
                                              static_cast<uint16_t>(common::MsgId::kRoleListReq),
                                              payload);
                            });

  network_->RegisterHandler(static_cast<uint16_t>(common::MsgId::kMoveReq),
                            [forward_handler](const std::shared_ptr<network::TcpSession>& session,
                                              const std::vector<uint8_t>& payload) {
                              forward_handler(session, static_cast<uint16_t>(common::MsgId::kMoveReq),
                                              payload);
                            });

  network_->RegisterHandler(static_cast<uint16_t>(common::MsgId::kAttackReq),
                            [forward_handler](const std::shared_ptr<network::TcpSession>& session,
                                              const std::vector<uint8_t>& payload) {
                              forward_handler(session,
                                              static_cast<uint16_t>(common::MsgId::kAttackReq),
                                              payload);
                            });

  network_->RegisterHandler(static_cast<uint16_t>(common::MsgId::kSkillReq),
                            [forward_handler](const std::shared_ptr<network::TcpSession>& session,
                                              const std::vector<uint8_t>& payload) {
                              forward_handler(session, static_cast<uint16_t>(common::MsgId::kSkillReq),
                                              payload);
                            });
}

bool GatewayServer::ConnectServices() {
  StartAsyncConnect(common::ServiceType::kWorld);
  StartAsyncConnect(common::ServiceType::kGame);
  StartAsyncConnect(common::ServiceType::kDb);
  return true;
}

void GatewayServer::StartAsyncConnect(common::ServiceType service) {
  asio::post(app_.GetIoContext(), [this, service]() {
    bool success = false;
    switch (service) {
      case common::ServiceType::kWorld:
        success = ConnectToWorldService();
        break;
      case common::ServiceType::kGame:
        success = ConnectToGameService();
        break;
      case common::ServiceType::kDb:
        success = ConnectToDbService();
        break;
      default:
        break;
    }

    if (!success) {
      SYSLOG_ERROR("Initial connect failed, scheduling reconnect (service={})",
                   static_cast<int>(service));
      ScheduleReconnect(service, 0);
    }
  });
}

bool GatewayServer::IsServiceConnected(common::ServiceType service) const {
  const auto* client = GetServiceClient(service);
  return client && client->IsConnected();
}

bool GatewayServer::ConnectToWorldService() {
  const auto& services = config::ConfigManager::Instance().GetServiceConfig();

  if (!world_client_) {
    world_client_ = std::make_unique<network::TcpClient>(app_.GetIoContext());
  }
  world_client_->SetPacketHandler([this](const network::Packet& packet) {
    OnServicePacket(common::ServiceType::kWorld, packet);
  });
  world_client_->SetDisconnectHandler([this]() {
    SYSLOG_ERROR("World service disconnected, scheduling reconnect");
    ScheduleReconnect(common::ServiceType::kWorld, 0);
  });
  if (!world_client_->Connect(services.world.host, services.world.port)) {
    SYSLOG_ERROR("Failed to connect World service");
    monitor::Metrics::Instance().IncrementCounter("gateway.service.disconnected.world");
    return false;
  }
  monitor::Metrics::Instance().IncrementCounter("gateway.service.connected.world");
  world_client_->Send(static_cast<uint16_t>(common::InternalMsgId::kServiceHello),
                      common::BuildServiceHello(common::ServiceType::kGateway));
  return true;
}

bool GatewayServer::ConnectToGameService() {
  const auto& services = config::ConfigManager::Instance().GetServiceConfig();

  if (!game_client_) {
    game_client_ = std::make_unique<network::TcpClient>(app_.GetIoContext());
  }
  game_client_->SetPacketHandler([this](const network::Packet& packet) {
    OnServicePacket(common::ServiceType::kGame, packet);
  });
  game_client_->SetDisconnectHandler([this]() {
    SYSLOG_ERROR("Game service disconnected, scheduling reconnect");
    ScheduleReconnect(common::ServiceType::kGame, 0);
  });
  if (!game_client_->Connect(services.game.host, services.game.port)) {
    SYSLOG_ERROR("Failed to connect Game service");
    monitor::Metrics::Instance().IncrementCounter("gateway.service.disconnected.game");
    return false;
  }
  monitor::Metrics::Instance().IncrementCounter("gateway.service.connected.game");
  game_client_->Send(static_cast<uint16_t>(common::InternalMsgId::kServiceHello),
                     common::BuildServiceHello(common::ServiceType::kGateway));
  return true;
}

bool GatewayServer::ConnectToDbService() {
  const auto& services = config::ConfigManager::Instance().GetServiceConfig();

  if (!db_client_) {
    db_client_ = std::make_unique<network::TcpClient>(app_.GetIoContext());
  }
  db_client_->SetPacketHandler([this](const network::Packet& packet) {
    OnServicePacket(common::ServiceType::kDb, packet);
  });
  db_client_->SetDisconnectHandler([this]() {
    SYSLOG_ERROR("DB service disconnected, scheduling reconnect");
    ScheduleReconnect(common::ServiceType::kDb, 0);
  });
  if (!db_client_->Connect(services.db.host, services.db.port)) {
    SYSLOG_ERROR("Failed to connect DB service");
    monitor::Metrics::Instance().IncrementCounter("gateway.service.disconnected.db");
    return false;
  }
  monitor::Metrics::Instance().IncrementCounter("gateway.service.connected.db");
  db_client_->Send(static_cast<uint16_t>(common::InternalMsgId::kServiceHello),
                   common::BuildServiceHello(common::ServiceType::kGateway));
  return true;
}

void GatewayServer::ScheduleReconnect(common::ServiceType service, int retry_count) {
  {
    std::unique_lock<std::shared_mutex> lock(reconnect_mutex_);
    if (reconnecting_[service]) {
      return;
    }
    reconnecting_[service] = true;
  }

  const int capped_retry = std::min(retry_count, 5);
  const int delay_ms = std::min(1000 * (1 << capped_retry), 30000);
  SYSLOG_INFO("Scheduling reconnect to service {} in {}ms (retry={})",
              static_cast<int>(service), delay_ms, retry_count);

  auto timer = std::make_shared<asio::steady_timer>(
      app_.GetIoContext(), std::chrono::milliseconds(delay_ms));
  timer->async_wait([this, service, retry_count, timer](const asio::error_code& ec) {
    if (ec) {
      std::unique_lock<std::shared_mutex> lock(reconnect_mutex_);
      reconnecting_[service] = false;
      return;
    }

    bool success = false;
    switch (service) {
      case common::ServiceType::kWorld:
        success = ConnectToWorldService();
        break;
      case common::ServiceType::kGame:
        success = ConnectToGameService();
        break;
      case common::ServiceType::kDb:
        success = ConnectToDbService();
        break;
      default:
        break;
    }

    if (success) {
      SYSLOG_INFO("Reconnected to service {}", static_cast<int>(service));
      std::unique_lock<std::shared_mutex> lock(reconnect_mutex_);
      reconnecting_[service] = false;
      return;
    }

    {
      std::unique_lock<std::shared_mutex> lock(reconnect_mutex_);
      reconnecting_[service] = false;
    }
    ScheduleReconnect(service, retry_count + 1);
  });
}

void GatewayServer::ForwardToService(common::ServiceType service, uint64_t client_id, uint16_t msg_id,
                                    const std::vector<uint8_t>& payload) {
  auto* client = GetServiceClient(service);
  if (!client || !client->IsConnected()) {
    SYSLOG_ERROR("Service not connected, service={} msg_id={}",
                 static_cast<int>(service), msg_id);
    return;
  }

  monitor::Metrics::Instance().IncrementCounter("gateway.forward.total");
  switch (service) {
    case common::ServiceType::kWorld:
      monitor::Metrics::Instance().IncrementCounter("gateway.forward.service.world");
      break;
    case common::ServiceType::kGame:
      monitor::Metrics::Instance().IncrementCounter("gateway.forward.service.game");
      break;
    case common::ServiceType::kDb:
      monitor::Metrics::Instance().IncrementCounter("gateway.forward.service.db");
      break;
    default:
      monitor::Metrics::Instance().IncrementCounter("gateway.forward.service.unknown");
      break;
  }

  const auto routed = common::BuildRoutedMessage(client_id, msg_id, payload);
  client->Send(static_cast<uint16_t>(common::InternalMsgId::kRoutedMessage), routed);
}

void GatewayServer::NotifyClientDisconnected(uint64_t client_id) {
  if (client_id == 0) {
    return;
  }
  const std::vector<uint8_t> empty_payload;
  ForwardToService(common::ServiceType::kWorld, client_id,
                   static_cast<uint16_t>(common::MsgId::kLogout),
                   empty_payload);
  ForwardToService(common::ServiceType::kGame, client_id,
                   static_cast<uint16_t>(common::MsgId::kLogout),
                   empty_payload);
}

void GatewayServer::OnServicePacket(common::ServiceType service, const network::Packet& packet) {
  if (!network_) {
    return;
  }

  if (packet.msg_id == static_cast<uint16_t>(common::InternalMsgId::kServiceHelloAck)) {
    common::ServiceType remote = common::ServiceType::kGateway;
    bool ok = false;
    if (common::ParseServiceHelloAck(packet.payload, &remote, &ok)) {
      SYSLOG_INFO("Service handshake ack: service={} remote={} ok={}",
                  static_cast<int>(service), static_cast<int>(remote), ok);
    }
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
  session->Send(routed.msg_id, routed.payload);
}

network::TcpClient* GatewayServer::GetServiceClient(common::ServiceType service) const {
  switch (service) {
    case common::ServiceType::kWorld:
      return world_client_.get();
    case common::ServiceType::kGame:
      return game_client_.get();
    case common::ServiceType::kDb:
      return db_client_.get();
    default:
      return nullptr;
  }
}

}  // namespace mir2::gateway
