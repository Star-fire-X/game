/**
 * @file summon_component.h
 * @brief ECS 召唤相关组件定义
 */

#ifndef LEGEND2_SERVER_ECS_SUMMON_COMPONENT_H
#define LEGEND2_SERVER_ECS_SUMMON_COMPONENT_H

#include <cstdint>
#include <vector>

#include <entt/entt.hpp>

namespace mir2::ecs {

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
};

} // namespace mir2::ecs

#endif  // LEGEND2_SERVER_ECS_SUMMON_COMPONENT_H
