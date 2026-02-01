#include "db/db_server.h"

#include <functional>

#include "common/enums.h"
#include "common/internal_message_helper.h"
#include "config/config_manager.h"
#include "handlers/login/login_handler.h"
#include "log/logger.h"
#include "monitor/metrics.h"

namespace mir2::db {

namespace {

uint64_t GenerateAccountId(const std::string& username) {
    return static_cast<uint64_t>(std::hash<std::string>{}(username));
}

class DbLoginService final : public legend2::handlers::LoginService {
public:
    DbLoginService(DatabaseManager& database_manager, asio::io_context& io_context)
        : database_manager_(database_manager), io_context_(io_context) {}

    void Login(const std::string& username,
               const std::string& /*password*/,
               legend2::handlers::LoginCallback callback) override {
        io_context_.post([username, callback = std::move(callback)]() mutable {
            legend2::handlers::LoginResult result;
            result.code = username.empty() ? mir2::common::ErrorCode::kAccountNotFound
                                           : mir2::common::ErrorCode::kOk;
            result.account_id = username.empty() ? 0 : GenerateAccountId(username);
            result.token = username.empty() ? "" : ("token_" + std::to_string(result.account_id));
            if (callback) {
                callback(result);
            }
        });
    }

private:
    [[maybe_unused]] DatabaseManager& database_manager_;
    asio::io_context& io_context_;
};

}  // namespace

DbServer::~DbServer() = default;

bool DbServer::Initialize(const std::string& config_path) {
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
        SYSLOG_ERROR("DBServer application init failed");
        return false;
    }
    monitor::Metrics::Instance().Init(server_config.metrics_port);

    const auto& db_config = config::ConfigManager::Instance().GetDatabaseConfig();
    const auto& redis_config = config::ConfigManager::Instance().GetRedisConfig();
    if (!database_manager_.Initialize(db_config, redis_config)) {
        SYSLOG_ERROR("DBServer database init failed");
        return false;
    }

    network_ = std::make_unique<network::NetworkManager>(app_.GetIoContext());
    if (!network_->Start(server_config.bind_ip, server_config.port, server_config.max_connections)) {
        SYSLOG_ERROR("DBServer network start failed");
        return false;
    }

    login_service_ = std::make_unique<DbLoginService>(database_manager_, app_.GetIoContext());
    RegisterMessageHandlers();
    RegisterHandlers();
    SYSLOG_INFO("DBServer initialized");
    return true;
}

void DbServer::Run() {
    if (logic_thread_.joinable()) {
        return;
    }
    logic_thread_ = std::thread([this]() { app_.Run([this](float delta_time) { Tick(delta_time); }); });
}

void DbServer::Shutdown() {
    if (network_) {
        network_->Stop();
    }
    app_.Stop();
    if (logic_thread_.joinable()) {
        logic_thread_.join();
    }
    database_manager_.Shutdown();
    app_.Shutdown();
    log::Logger::Instance().Shutdown();
}

void DbServer::Tick(float /*delta_time*/) {
    if (network_) {
        network_->Tick();
    }
}

void DbServer::RegisterHandlers() {
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
                                  SYSLOG_INFO("DBServer handshake from service={}",
                                              static_cast<int>(remote));
                                  const auto ack = common::BuildServiceHelloAck(common::ServiceType::kDb, true);
                                  session->Send(static_cast<uint16_t>(common::InternalMsgId::kServiceHelloAck),
                                                ack);
                              });

    network_->RegisterHandler(static_cast<uint16_t>(common::InternalMsgId::kRoutedMessage),
                              [this](const std::shared_ptr<network::TcpSession>& session,
                                     const std::vector<uint8_t>& payload) {
                                  HandleRoutedMessage(session, payload);
                              });
}

void DbServer::RegisterMessageHandlers() {
    auto login_handler = std::make_shared<legend2::handlers::LoginHandler>(*login_service_);
    handler_registry_.Register(static_cast<uint16_t>(mir2::common::MsgId::kLoginReq),
                               login_handler);
}

void DbServer::HandleRoutedMessage(const std::shared_ptr<network::TcpSession>& session,
                                   const std::vector<uint8_t>& payload) {
    if (!session) {
        return;
    }
    common::RoutedMessageData routed;
    if (!common::ParseRoutedMessage(payload, &routed)) {
        SYSLOG_ERROR("DBServer failed to parse routed message");
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
        SYSLOG_WARN("DBServer no handler for msg_id={}", routed.msg_id);
    }
}

}  // namespace mir2::db
