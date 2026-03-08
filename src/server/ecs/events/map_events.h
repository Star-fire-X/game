/**
 * @file map_events.h
 * @brief Map event definitions
 */

#ifndef MIR2_SERVER_ECS_EVENTS_MAP_EVENTS_H_
#define MIR2_SERVER_ECS_EVENTS_MAP_EVENTS_H_

#include <entt/entt.hpp>

#include <cstdint>

namespace mir2::ecs::events {

/**
 * @brief Map change event
 */
struct MapChangeEvent {
  entt::entity entity;
  int32_t old_map_id;
  int32_t new_map_id;
  int32_t new_x;
  int32_t new_y;
};

/**
 * @brief 传送请求事件
 */
struct TeleportRequestEvent {
  entt::entity entity;
  int32_t target_map_id;
  int32_t target_x;
  int32_t target_y;
};

}  // namespace mir2::ecs::events

#endif  // MIR2_SERVER_ECS_EVENTS_MAP_EVENTS_H_
