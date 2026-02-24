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

TeleportSystem::TeleportSystem(game::ports::IWorldMapPort& world_map_port, EventBus& event_bus)
    : System(SystemPriority::kMovement),
      world_map_port_(world_map_port),
      event_bus_(&event_bus) {
  if (event_bus_) {
    event_bus_->Subscribe<events::TeleportRequestEvent>(
        [this](events::TeleportRequestEvent& event) {
          RequestTeleport(TeleportRequest{
              event.entity, event.target_map_id, event.target_x, event.target_y});
        });
  }
}

void TeleportSystem::RequestTeleport(const TeleportRequest& cmd) {
  std::lock_guard<std::mutex> lock(teleport_queue_mutex_);
  teleport_queue_.push(cmd);
}

void TeleportSystem::Update(entt::registry& registry, float /*delta_time*/) {
  std::queue<TeleportRequest> pending_queue;
  {
    std::lock_guard<std::mutex> lock(teleport_queue_mutex_);
    std::swap(pending_queue, teleport_queue_);
  }

  while (!pending_queue.empty()) {
    auto cmd = pending_queue.front();
    pending_queue.pop();

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
      // 同地图传送，直接更新位置。仅在地图端口更新成功时同步组件状态。
      if (!world_map_port_.UpdateEntityPosition(cmd.entity,
                                                cmd.target_x,
                                                cmd.target_y)) {
        SYSLOG_WARN("TeleportSystem: Same map teleport failed on map {} to ({}, {})",
                    cmd.target_map_id, cmd.target_x, cmd.target_y);
        continue;
      }
      state->position.x = cmd.target_x;
      state->position.y = cmd.target_y;
      if (event_bus_) {
        events::MapChangeEvent event{cmd.entity, old_map_id, old_map_id,
                                     cmd.target_x, cmd.target_y};
        event_bus_->Publish(event);
      }
      continue;
    }

    // 验证目标地图存在
    if (!world_map_port_.MapExists(cmd.target_map_id)) {
      SYSLOG_WARN("TeleportSystem: Target map {} not found", cmd.target_map_id);
      continue;
    }

    // 通过地图端口原子迁移：仅当目标地图添加成功时才会从旧地图移除。
    if (!world_map_port_.AddEntityToMap(cmd.target_map_id,
                                        cmd.entity,
                                        cmd.target_x,
                                        cmd.target_y)) {
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
