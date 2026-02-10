/**
 * @file ground_item_system.h
 * @brief ECS 地面物品清理系统
 */

#ifndef MIR2_SERVER_ECS_SYSTEMS_GROUND_ITEM_SYSTEM_H_
#define MIR2_SERVER_ECS_SYSTEMS_GROUND_ITEM_SYSTEM_H_

#include <cstdint>

#include "ecs/world.h"

namespace mir2::ecs {

/**
 * @brief 地面物品清理系统
 *
 * 定期检查地面物品过期状态并清理。
 */
class GroundItemSystem : public System {
 public:
    GroundItemSystem();

    void Update(entt::registry& registry, float delta_time) override;

 private:
    void CleanupExpired(entt::registry& registry, int64_t now_ms);

    float check_timer_ = 0.0f;

    static constexpr float kCheckIntervalSeconds = 1.0f;
};

}  // namespace mir2::ecs

#endif  // MIR2_SERVER_ECS_SYSTEMS_GROUND_ITEM_SYSTEM_H_
