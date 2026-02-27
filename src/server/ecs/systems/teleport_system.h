/**
 * @file teleport_system.h
 * @brief 传送系统
 */

#ifndef MIR2_ECS_SYSTEMS_TELEPORT_SYSTEM_H_
#define MIR2_ECS_SYSTEMS_TELEPORT_SYSTEM_H_

#include "ecs/event_bus.h"
#include "ecs/world.h"
#include "game/ports/i_world_map_port.h"

#include <entt/entt.hpp>
#include <cstdint>
#include <mutex>
#include <queue>

namespace mir2::ecs {

struct TeleportRequest {
  entt::entity entity = entt::null;
  int32_t target_map_id = 0;
  int32_t target_x = 0;
  int32_t target_y = 0;
};

/**
 * @brief 传送系统
 *
 * 处理实体跨地图传送，依赖地图端口与 ECS。
 */
class TeleportSystem : public System {
 public:
  TeleportSystem(game::ports::IWorldMapPort& world_map_port, EventBus& event_bus);

  /**
   * @brief 请求传送
   *
   * @param request 传送请求
   */
  void RequestTeleport(const TeleportRequest& request);

  /**
   * @brief 系统更新
   */
  void Update(entt::registry& registry, float delta_time) override;

 private:
  game::ports::IWorldMapPort& world_map_port_;
  EventBus* event_bus_ = nullptr;
  EventBus::Subscription teleport_request_subscription_;
  std::mutex teleport_queue_mutex_;
  std::queue<TeleportRequest> teleport_queue_;
};

}  // namespace mir2::ecs

#endif  // MIR2_ECS_SYSTEMS_TELEPORT_SYSTEM_H_
