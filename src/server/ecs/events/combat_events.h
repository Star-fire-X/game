/**
 * @file combat_events.h
 * @brief 战斗相关事件定义
 */

#ifndef MIR2_ECS_EVENTS_COMBAT_EVENTS_H_
#define MIR2_ECS_EVENTS_COMBAT_EVENTS_H_

#include <entt/entt.hpp>

#include <cstdint>

#include "common/types.h"

namespace mir2::ecs::events {

/**
 * @brief 伤害造成事件
 */
struct DamageDealtEvent {
  entt::entity attacker = entt::null;
  entt::entity target = entt::null;
  uint32_t skill_id = 0;  // 0 表示普通攻击
  int damage = 0;
  bool is_critical = false;
  bool is_miss = false;
};

/**
 * @brief 实体死亡事件
 */
struct EntityDeathEvent {
  entt::entity entity = entt::null;         // 死亡实体
  entt::entity killer = entt::null;         // 击杀者
  uint32_t skill_id = 0;                    // 致死技能（0=普通攻击）
  mir2::common::Position position{};        // 死亡位置
  uint32_t map_id = 0;                      // 所在地图
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

#endif  // MIR2_ECS_EVENTS_COMBAT_EVENTS_H_
