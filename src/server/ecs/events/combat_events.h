/**
 * @file combat_events.h
 * @brief 战斗相关事件定义
 */

#ifndef MIR2_ECS_EVENTS_COMBAT_EVENTS_H
#define MIR2_ECS_EVENTS_COMBAT_EVENTS_H

#include <entt/entt.hpp>
#include "common/types.h"

namespace mir2::ecs::events {

/**
 * @brief 伤害造成事件
 */
struct DamageDealtEvent {
  entt::entity attacker;
  entt::entity target;
  int damage;
  bool is_critical;
  bool is_miss;
};

/**
 * @brief 实体死亡事件
 */
struct EntityDeathEvent {
  entt::entity entity;
  entt::entity killer;
  mir2::common::Position position;
  uint32_t map_id;
};

/**
 * @brief 实体复活事件
 */
struct EntityRespawnEvent {
  entt::entity entity;
  mir2::common::Position position;
  uint32_t map_id;
  float hp_percent;
  float mp_percent;
};

}  // namespace mir2::ecs::events

#endif  // MIR2_ECS_EVENTS_COMBAT_EVENTS_H
