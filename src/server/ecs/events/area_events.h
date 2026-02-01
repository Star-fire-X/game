/**
 * @file area_events.h
 * @brief 区域事件定义
 */

#ifndef MIR2_ECS_EVENTS_AREA_EVENTS_H
#define MIR2_ECS_EVENTS_AREA_EVENTS_H

#include <entt/entt.hpp>

#include <cstdint>

#include "game/map/area_trigger.h"

namespace mir2::ecs::events {

/**
 * @brief 进入区域事件
 */
struct AreaEnterEvent {
  entt::entity entity;
  uint32_t trigger_id;
  ::mir2::game::map::AreaEffectType effect_type;
};

/**
 * @brief 离开区域事件
 */
struct AreaExitEvent {
  entt::entity entity;
  uint32_t trigger_id;
  ::mir2::game::map::AreaEffectType effect_type;
};

/**
 * @brief 持续伤害触发事件
 */
struct AreaDamageTickEvent {
  entt::entity entity;
  uint32_t effect_id;
  int32_t damage;
};

/**
 * @brief 持续治疗触发事件
 */
struct AreaHealTickEvent {
  entt::entity entity;
  uint32_t effect_id;
  int32_t heal;
};

/**
 * @brief 火墙灼烧触发事件
 */
struct FireBurnTickEvent {
  uint32_t trigger_id;
  entt::entity target;
  int32_t damage;
  float burn_duration;
};

/**
 * @brief 圣言护体触发事件
 */
struct HolyCurtainTickEvent {
  uint32_t trigger_id;
  entt::entity caster;
  entt::entity target;
  int32_t shield_bonus;
};

/**
 * @brief 地雷触发事件
 */
struct MineEvent {
  uint32_t trigger_id;
  entt::entity target;
  int32_t blast_radius;
};

}  // namespace mir2::ecs::events

#endif  // MIR2_ECS_EVENTS_AREA_EVENTS_H
