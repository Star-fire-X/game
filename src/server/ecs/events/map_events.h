/**
 * @file map_events.h
 * @brief Map event definitions
 */

#ifndef LEGEND2_SERVER_ECS_EVENTS_MAP_EVENTS_H
#define LEGEND2_SERVER_ECS_EVENTS_MAP_EVENTS_H

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

}  // namespace mir2::ecs::events

#endif  // LEGEND2_SERVER_ECS_EVENTS_MAP_EVENTS_H
