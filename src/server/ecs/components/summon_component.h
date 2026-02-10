/**
 * @file summon_component.h
 * @brief ECS 召唤相关组件定义
 */

#ifndef MIR2_SERVER_ECS_SUMMON_COMPONENT_H_
#define MIR2_SERVER_ECS_SUMMON_COMPONENT_H_

#include <cstdint>
#include <vector>

#include <entt/entt.hpp>

namespace mir2::ecs {

/**
 * @brief 召唤兽AI状态
 */
enum class SummonAIState : uint8_t {
    IDLE = 0,
    FOLLOW = 1,
    ATTACK = 2,
};

/**
 * @brief 召唤者组件（附加在角色上）
 */
struct SummonerComponent {
    std::vector<entt::entity> summons;
    int max_summons = 1;
};

/**
 * @brief 被召唤生物组件
 */
struct SummonComponent {
    entt::entity owner = entt::null;
    uint32_t skill_id = 0;
    uint8_t skill_level = 0;
    int64_t summon_time_ms = 0;
    int64_t loyalty_expire_time_ms = 0;  ///< 忠诚度过期时间（MasterRoyaltyTime）
    int summon_experience = 0;            ///< 召唤兽经验值（SlaveExp）

    // AI相关字段
    SummonAIState ai_state = SummonAIState::IDLE;
    entt::entity target_entity = entt::null;
    int64_t last_ai_update_ms = 0;
    int64_t next_attack_time_ms = 0;
};

} // namespace mir2::ecs

#endif  // MIR2_SERVER_ECS_SUMMON_COMPONENT_H_
