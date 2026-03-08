/**
 * @file achievement_component.h
 * @brief Achievement progress component.
 */

#ifndef MIR2_SERVER_ECS_COMPONENTS_ACHIEVEMENT_COMPONENT_H_
#define MIR2_SERVER_ECS_COMPONENTS_ACHIEVEMENT_COMPONENT_H_

#include <cstdint>
#include <unordered_map>

namespace mir2::ecs {

struct AchievementProgress {
  uint32_t achievement_id = 0;
  uint32_t progress = 0;
  uint32_t target = 0;
  bool completed = false;
  bool claimed = false;
  uint64_t completed_time_ms = 0;
  uint32_t reward_gold = 0;
};

struct AchievementComponent {
  std::unordered_map<uint32_t, AchievementProgress> achievements;
};

}  // namespace mir2::ecs

#endif  // MIR2_SERVER_ECS_COMPONENTS_ACHIEVEMENT_COMPONENT_H_
