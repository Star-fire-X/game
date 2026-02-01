/**
 * @file world_server.h
 * @brief 世界服务器
 */

#ifndef MIR2_WORLD_WORLD_SERVER_H
#define MIR2_WORLD_WORLD_SERVER_H

#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "core/application.h"
#include "ecs/character_entity_manager.h"
#include "ecs/world.h"
#include "handlers/handler_registry.h"
#include "network/network_manager.h"
#include "world/role_record.h"
#include "world/role_store.h"

namespace mir2::world {

/**
 * @brief 世界服务器
 *
 * 负责跨服逻辑、公会、排行等全局功能。
 */
class WorldServer {
 public:
  WorldServer();
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
  std::thread logic_thread_;
  ecs::World ecs_world_;
  ecs::CharacterEntityManager character_entity_manager_;
  mir2::world::RoleStore role_store_;
  legend2::handlers::HandlerRegistry handler_registry_;
};

}  // namespace mir2::world

#endif  // MIR2_WORLD_WORLD_SERVER_H
