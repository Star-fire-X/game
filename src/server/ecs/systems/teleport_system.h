/**
 * @file teleport_system.h
 * @brief 传送系统
 */

#ifndef MIR2_ECS_SYSTEMS_TELEPORT_SYSTEM_H_
#define MIR2_ECS_SYSTEMS_TELEPORT_SYSTEM_H_

#include "ecs/world.h"
#include "game/map/scene_manager.h"
#include "game/map/teleport_command.h"

#include <entt/entt.hpp>
#include <queue>

namespace mir2::ecs {

class EventBus;

/**
 * @brief 传送系统
 *
 * 处理实体跨地图传送，集成 SceneManager 和 ECS。
 */
class TeleportSystem : public System {
 public:
  TeleportSystem(game::map::SceneManager& scene_manager, EventBus& event_bus);

  /**
   * @brief 请求传送
   *
   * @param cmd 传送命令
   */
  void RequestTeleport(const game::map::TeleportCommand& cmd);

  /**
   * @brief 系统更新
   */
  void Update(entt::registry& registry, float delta_time) override;

 private:
  game::map::SceneManager& scene_manager_;
  EventBus* event_bus_ = nullptr;
  std::queue<game::map::TeleportCommand> teleport_queue_;
};

}  // namespace mir2::ecs

#endif  // MIR2_ECS_SYSTEMS_TELEPORT_SYSTEM_H_
