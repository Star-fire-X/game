/**
 * @file boss_events.h
 * @brief BOSS事件定义
 */

#ifndef MIR2_ECS_EVENTS_BOSS_EVENTS_H
#define MIR2_ECS_EVENTS_BOSS_EVENTS_H

#include <cstdint>
#include <vector>

namespace mir2::ecs::events {

/**
 * @brief BOSS进入狂暴事件
 */
struct BossEnterRageEvent {
  uint32_t boss_id;
  uint32_t monster_id;
  int32_t attack;
  float attack_speed;
};

/**
 * @brief BOSS召唤事件
 */
struct BossSummonEvent {
  uint32_t boss_id;
  uint32_t monster_id;
  uint32_t summon_count;
  std::vector<uint32_t> summon_monster_ids;
};

/**
 * @brief BOSS死亡事件
 */
struct BossDeadEvent {
  uint32_t boss_id;
  uint32_t monster_id;
};

/**
 * @brief BOSS释放特殊技能事件
 */
struct BossSpecialSkillEvent {
  uint32_t boss_id;
  uint32_t monster_id;
  int32_t damage;
};

}  // namespace mir2::ecs::events

#endif  // MIR2_ECS_EVENTS_BOSS_EVENTS_H
