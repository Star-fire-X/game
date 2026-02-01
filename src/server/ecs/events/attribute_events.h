/**
 * @file attribute_events.h
 * @brief 属性变更事件定义
 */

#ifndef MIR2_ECS_EVENTS_ATTRIBUTE_EVENTS_H
#define MIR2_ECS_EVENTS_ATTRIBUTE_EVENTS_H

#include <entt/entt.hpp>
#include <string>

namespace mir2::ecs::events {

/**
 * @brief 升级事件
 */
struct LevelUpEvent {
  entt::entity entity;
  int old_level;
  int new_level;
};

/**
 * @brief 经验获得事件
 */
struct ExperienceGainedEvent {
  entt::entity entity;
  int amount;
  int total_experience;
};

/**
 * @brief 属性变更事件
 */
struct AttributeChangedEvent {
  entt::entity entity;
  std::string attribute_name;
  int old_value;
  int new_value;
};

}  // namespace mir2::ecs::events

#endif  // MIR2_ECS_EVENTS_ATTRIBUTE_EVENTS_H
