#include "world/world_server.h"

#include "common/enums.h"
#include "common/internal_message_helper.h"
#include "config/config_manager.h"
#include "handlers/character/character_handler.h"
#include "log/logger.h"
#include "monitor/metrics.h"

namespace mir2::world {

WorldServer::WorldServer()
    : character_entity_manager_(ecs_world_.Registry()) {}

bool WorldServer::Initialize(const std::string& config_path) {
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
        SYSLOG_ERROR("WorldServer application init failed");
        return false;
    }
    monitor::Metrics::Instance().Init(server_config.metrics_port);

    network_ = std::make_unique<network::NetworkManager>(app_.GetIoContext());
    if (!network_->Start(server_config.bind_ip, server_config.port, server_config.max_connections)) {
        SYSLOG_ERROR("WorldServer network start failed");
        return false;
    }

    RegisterMessageHandlers();
    RegisterHandlers();
    SYSLOG_INFO("WorldServer initialized");
    return true;
}

void WorldServer::Run() {
    if (logic_thread_.joinable()) {
        return;
    }
    logic_thread_ = std::thread([this]() { app_.Run([this](float delta_time) { Tick(delta_time); }); });
}

void WorldServer::Shutdown() {
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

void WorldServer::Tick(float delta_time) {
    character_entity_manager_.Update(delta_time);
    if (network_) {
        network_->Tick();
    }
}

void WorldServer::RegisterHandlers() {
    if (!network_) {
        return;
    }

    network_->RegisterHandler(static_cast<uint16_t>(common::InternalMsgId::kServiceHello),
                              [this](const std::shared_ptr<network::TcpSession>& session,
                                     const std::vector<uint8_t>& payload) {
                                  if (!session) {
                                      return;
                                  }
                                  common::ServiceType remote = common::ServiceType::kGateway;
                                  if (!common::ParseServiceHello(payload, &remote)) {
                                      return;
                                  }
                                  SYSLOG_INFO("WorldServer handshake from service={}",
                                              static_cast<int>(remote));
                                  const auto ack = common::BuildServiceHelloAck(common::ServiceType::kWorld, true);
                                  session->Send(static_cast<uint16_t>(common::InternalMsgId::kServiceHelloAck),
                                                ack);
                              });

    network_->RegisterHandler(static_cast<uint16_t>(common::InternalMsgId::kRoutedMessage),
                              [this](const std::shared_ptr<network::TcpSession>& session,
                                     const std::vector<uint8_t>& payload) {
                                  HandleRoutedMessage(session, payload);
                              });
}

void WorldServer::RegisterMessageHandlers() {
    auto handler = std::make_shared<legend2::handlers::CharacterHandler>(
        character_entity_manager_, role_store_);
    handler_registry_.Register(static_cast<uint16_t>(mir2::common::MsgId::kRoleListReq),
                               handler);
    handler_registry_.Register(static_cast<uint16_t>(mir2::common::MsgId::kCreateRoleReq),
                               handler);
    handler_registry_.Register(static_cast<uint16_t>(mir2::common::MsgId::kSelectRoleReq),
                               handler);
    handler_registry_.Register(static_cast<uint16_t>(mir2::common::MsgId::kLogout),
                               handler);
}

void WorldServer::HandleRoutedMessage(const std::shared_ptr<network::TcpSession>& session,
                                      const std::vector<uint8_t>& payload) {
    if (!session) {
        return;
    }
    common::RoutedMessageData routed;
    if (!common::ParseRoutedMessage(payload, &routed)) {
        SYSLOG_ERROR("WorldServer failed to parse routed message");
        return;
    }

    legend2::handlers::HandlerContext context;
    context.client_id = routed.client_id;
    context.session = session;
    context.post = [this](std::function<void()> task) {
        if (task) {
            app_.GetIoContext().post(std::move(task));
        }
    };

    bool handled = handler_registry_.Dispatch(
        context, routed.msg_id, routed.payload,
        [session](const legend2::handlers::ResponseList& responses) {
            if (!session) {
                return;
            }
            for (const auto& response : responses) {
                const auto routed_rsp = common::BuildRoutedMessage(
                    response.client_id, response.msg_id, response.payload);
                session->Send(static_cast<uint16_t>(common::InternalMsgId::kRoutedMessage), routed_rsp);
            }
        });

    if (!handled) {
        SYSLOG_WARN("WorldServer no handler for msg_id={}", routed.msg_id);
    }
}

}  // namespace mir2::world
