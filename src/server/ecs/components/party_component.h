/**
 * @file party_component.h
 * @brief 小队/组队组件定义
 */

#ifndef MIR2_ECS_COMPONENTS_PARTY_COMPONENT_H_
#define MIR2_ECS_COMPONENTS_PARTY_COMPONENT_H_

#include <entt/entt.hpp>
#include <cstdint>
#include <vector>

namespace mir2::ecs {

/**
 * @brief 掉落分配模式
 */
enum class LootMode : uint8_t {
    FREE_FOR_ALL = 0,   ///< 自由拾取
    ROUND_ROBIN = 1,    ///< 轮流分配
    LEADER_ASSIGN = 2,  ///< 队长分配
    DAMAGE_BASED = 3    ///< 按伤害分配
};

/**
 * @brief 小队组件
 */
struct PartyComponent {
    uint32_t party_id = 0;              ///< 小队ID
    entt::entity leader = entt::null;   ///< 队长实体
    std::vector<entt::entity> members;  ///< 成员列表（包含队长）
    LootMode loot_mode = LootMode::FREE_FOR_ALL;  ///< 掉落分配模式
    int32_t loot_range = 15;            ///< 掉落共享范围（格子数）
    uint8_t max_members = 11;           ///< 最大成员数 (GROUP_MAX)

    bool IsFull() const { return members.size() >= max_members; }
    bool IsLeader(entt::entity e) const { return e == leader; }
    bool IsMember(entt::entity e) const {
        for (auto m : members) {
            if (m == e) return true;
        }
        return false;
    }
};

/**
 * @brief 小队成员标记组件（附加到每个队员）
 */
struct PartyMemberComponent {
    uint32_t party_id = 0;  ///< 所属小队ID
    uint32_t character_id = 0;  ///< 角色ID（用于快速查询）
};

}  // namespace mir2::ecs

#endif  // MIR2_ECS_COMPONENTS_PARTY_COMPONENT_H_
