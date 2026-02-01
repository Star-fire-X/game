/**
 * @file lifecycle_events.h
 * @brief 角色生命周期事件定义
 */

#ifndef MIR2_ECS_EVENTS_LIFECYCLE_EVENTS_H
#define MIR2_ECS_EVENTS_LIFECYCLE_EVENTS_H

#include <entt/entt.hpp>
#include <cstdint>

namespace mir2::ecs::events {

/**
 * @brief 角色创建事件
 */
struct CharacterCreatedEvent {
  uint32_t character_id;
  entt::entity entity;
};

/**
 * @brief 角色登录事件
 */
struct CharacterLoginEvent {
  uint32_t character_id;
  entt::entity entity;
};

/**
 * @brief 角色登出事件
 */
struct CharacterLogoutEvent {
  uint32_t character_id;
  entt::entity entity;
};

}  // namespace mir2::ecs::events

#endif  // MIR2_ECS_EVENTS_LIFECYCLE_EVENTS_H
