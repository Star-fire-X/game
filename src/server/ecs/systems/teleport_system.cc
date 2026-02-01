/**
 * @file teleport_system.cc
 * @brief 传送系统实现
 */

#include "ecs/systems/teleport_system.h"
#include "ecs/components/character_components.h"
#include "ecs/event_bus.h"
#include "ecs/events/map_events.h"
#include "log/logger.h"

namespace mir2::ecs {

TeleportSystem::TeleportSystem(game::map::SceneManager& scene_manager, EventBus& event_bus)
    : System(SystemPriority::kMovement), scene_manager_(scene_manager), event_bus_(&event_bus) {}

void TeleportSystem::RequestTeleport(const game::map::TeleportCommand& cmd) {
  teleport_queue_.push(cmd);
}

void TeleportSystem::Update(entt::registry& registry, float /*delta_time*/) {
  while (!teleport_queue_.empty()) {
    auto cmd = teleport_queue_.front();
    teleport_queue_.pop();

    // 验证实体存在
    if (!registry.valid(cmd.entity)) {
      SYSLOG_WARN("TeleportSystem: Invalid entity");
      continue;
    }

    // 获取当前状态
    auto* state = registry.try_get<CharacterStateComponent>(cmd.entity);
    if (!state) {
      SYSLOG_WARN("TeleportSystem: Entity missing CharacterStateComponent");
      continue;
    }

    auto old_map_id = static_cast<int32_t>(state->map_id);

    // 检查是否同地图传送
    if (state->map_id == static_cast<uint32_t>(cmd.target_map_id)) {
      SYSLOG_DEBUG("TeleportSystem: Same map teleport, use movement instead");
      // 同地图传送，直接更新位置
      scene_manager_.UpdateEntityPosition(cmd.entity, cmd.target_x, cmd.target_y);
      state->position.x = cmd.target_x;
      state->position.y = cmd.target_y;
      continue;
    }

    // 验证目标地图存在
    auto* target_map = scene_manager_.GetMap(cmd.target_map_id);
    if (!target_map) {
      SYSLOG_WARN("TeleportSystem: Target map {} not found", cmd.target_map_id);
      continue;
    }

    // 从旧地图移除
    scene_manager_.RemoveEntityFromMap(cmd.entity);

    // 添加到新地图
    if (!scene_manager_.AddEntityToMap(cmd.target_map_id, cmd.entity,
                                       cmd.target_x, cmd.target_y)) {
      SYSLOG_ERROR("TeleportSystem: Failed to add entity to map {}", cmd.target_map_id);
      continue;
    }

    // 更新组件状态
    state->map_id = cmd.target_map_id;
    state->position.x = cmd.target_x;
    state->position.y = cmd.target_y;

    if (event_bus_) {
      events::MapChangeEvent event{cmd.entity, old_map_id, cmd.target_map_id,
                                   cmd.target_x, cmd.target_y};
      event_bus_->Publish(event);
    }

    SYSLOG_INFO("TeleportSystem: Entity teleported to map {} at ({}, {})",
                cmd.target_map_id, cmd.target_x, cmd.target_y);
  }
}

}  // namespace mir2::ecs
