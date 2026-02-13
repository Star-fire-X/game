/**
 * @file recovery_system.h
 * @brief HP/MP 自然恢复系统
 */

#ifndef MIR2_SERVER_ECS_SYSTEMS_RECOVERY_SYSTEM_H_
#define MIR2_SERVER_ECS_SYSTEMS_RECOVERY_SYSTEM_H_

#include "ecs/world.h"
#include <entt/entt.hpp>

namespace mir2::ecs {

/**
 * @brief HP/MP 自然恢复系统
 *
 * 每 3-5 秒 tick 一次，非满血/蓝角色自动恢复。
 * 恢复公式: hp += max_hp / 50, mp += max_mp / 50
 */
class RecoverySystem : public System {
public:
    RecoverySystem();

    void Update(entt::registry& registry, float delta_time) override;

private:
    static constexpr float kRecoveryInterval = 4.0f;  // 恢复间隔(秒)
    float accumulator_ = 0.0f;
};

}  // namespace mir2::ecs

#endif  // MIR2_SERVER_ECS_SYSTEMS_RECOVERY_SYSTEM_H_
