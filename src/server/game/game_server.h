/**
 * @file game_server.h
 * @brief 游戏服务器
 */

#ifndef MIR2_GAME_GAME_SERVER_H
#define MIR2_GAME_GAME_SERVER_H

#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "core/application.h"
#include "ecs/registry_manager.h"
#include "handlers/client_registry.h"
#include "handlers/effect/effect_broadcast_service.h"
#include "handlers/handler_registry.h"
#include "network/network_manager.h"
#include "game/map/gate_manager.h"
#include "game/map/scene_manager.h"
#include "ecs/systems/effect_broadcaster.h"
#include "ecs/systems/teleport_system.h"

namespace legend2::handlers {
class CombatService;
class InventoryService;
class EntityBroadcastService;
}  // namespace legend2::handlers

namespace mir2::game {

namespace handlers = ::legend2::handlers;

/**
 * @brief 游戏服务器
 *
 * 负责地图逻辑、战斗、ECS更新等核心玩法。
 */
class GameServer {
 public:
  GameServer();
  ~GameServer();
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
  ecs::RegistryManager& registry_manager_;
  ecs::CharacterEntityManager& character_entity_manager_;
  handlers::ClientRegistry client_registry_;
  game::map::SceneManager scene_manager_;
  game::map::GateManager gate_manager_;
  ecs::TeleportSystem* teleport_system_ = nullptr;  // 默认地图系统（可选）
  handlers::HandlerRegistry handler_registry_;
  std::unique_ptr<handlers::CombatService> combat_service_;
  std::unique_ptr<handlers::InventoryService> inventory_service_;
  std::vector<std::unique_ptr<handlers::EffectBroadcastService>> effect_broadcast_services_;
  std::vector<std::unique_ptr<handlers::EntityBroadcastService>> entity_broadcast_services_;
  std::vector<std::unique_ptr<ecs::EffectBroadcaster>> effect_broadcasters_;
  std::thread logic_thread_;
};

}  // namespace mir2::game

#endif  // MIR2_GAME_GAME_SERVER_H
