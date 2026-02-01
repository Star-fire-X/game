/**
 * @file db_server.h
 * @brief DB服务器
 */

#ifndef MIR2_DB_DB_SERVER_H
#define MIR2_DB_DB_SERVER_H

#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "core/application.h"
#include "db/database_manager.h"
#include "handlers/handler_registry.h"
#include "network/network_manager.h"

#include "handlers/login/login_handler.h"

namespace mir2::db {

namespace handlers = ::legend2::handlers;

/**
 * @brief DB服务器
 *
 * 负责处理数据库与缓存请求。
 */
class DbServer {
 public:
  ~DbServer();
  bool Initialize(const std::string& config_path);
  void Run();
  void Shutdown();

 private:
  void Tick(float delta_time);
  void RegisterHandlers();
  void RegisterMessageHandlers();
  void HandleRoutedMessage(const std::shared_ptr<network::TcpSession>& session,
                          const std::vector<uint8_t>& payload);

  core::Application app_;
  std::unique_ptr<network::NetworkManager> network_;
  DatabaseManager database_manager_;
  handlers::HandlerRegistry handler_registry_;
  std::unique_ptr<handlers::LoginService> login_service_;
  std::thread logic_thread_;
};

}  // namespace mir2::db

#endif  // MIR2_DB_DB_SERVER_H
