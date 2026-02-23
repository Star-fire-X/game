/**
 * @file luck_system.h
 * @brief BodyLuck 幸运值衰减系统
 */

#ifndef MIR2_SERVER_ECS_SYSTEMS_LUCK_SYSTEM_H_
#define MIR2_SERVER_ECS_SYSTEMS_LUCK_SYSTEM_H_

#include "ecs/world.h"
#include <entt/entt.hpp>

namespace mir2::ecs {

/**
 * @brief BodyLuck 幸运值系统
 *
 * - 升级时 body_luck +100（在 LevelUpSystem 中处理）
 * - 每 tick body_luck 衰减 -1（直到 0）
 * - body_luck 影响暴击和掉落概率
 */
class LuckSystem : public System {
public:
    LuckSystem();

    void Update(entt::registry& registry, float delta_time) override;

private:
    static constexpr float kTickInterval = 1.0f;  // 每秒衰减一次
    float accumulator_ = 0.0f;
};

}  // namespace mir2::ecs

#endif  // MIR2_SERVER_ECS_SYSTEMS_LUCK_SYSTEM_H_
