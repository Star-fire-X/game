/**
 * @file party_component.h
 * @brief 小队/组队组件定义
 */

#ifndef MIR2_ECS_COMPONENTS_PARTY_COMPONENT_H_
#define MIR2_ECS_COMPONENTS_PARTY_COMPONENT_H_

#include <algorithm>
#include <cstdint>
#include <entt/entt.hpp>
#include <unordered_set>
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
    mutable std::unordered_set<entt::entity> member_index;
    LootMode loot_mode = LootMode::FREE_FOR_ALL;  ///< 掉落分配模式
    int32_t loot_range = 15;            ///< 掉落共享范围（格子数）
    uint8_t max_members = 11;           ///< 最大成员数 (GROUP_MAX)

    bool IsFull() const { return members.size() >= max_members; }
    bool IsLeader(entt::entity e) const { return e == leader; }

    void RebuildMemberIndex() const {
        member_index.clear();
        member_index.reserve(members.size());
        for (entt::entity member : members) {
            if (member != entt::null) {
                member_index.insert(member);
            }
        }
    }

    bool IsMember(entt::entity e) const {
        if (e == entt::null) {
            return false;
        }
        if (member_index.size() != members.size()) {
            RebuildMemberIndex();
        }
        return member_index.contains(e);
    }

    bool AddMember(entt::entity e) {
        if (e == entt::null) {
            return false;
        }
        if (member_index.size() != members.size()) {
            RebuildMemberIndex();
        }
        if (!member_index.insert(e).second) {
            return false;
        }
        members.push_back(e);
        return true;
    }

    bool RemoveMember(entt::entity e) {
        if (e == entt::null) {
            return false;
        }
        if (member_index.size() != members.size()) {
            RebuildMemberIndex();
        }
        if (member_index.erase(e) == 0) {
            return false;
        }
        const auto it = std::remove(members.begin(), members.end(), e);
        if (it == members.end()) {
            return false;
        }
        members.erase(it, members.end());
        return true;
    }

    void ClearMembers() {
        members.clear();
        member_index.clear();
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
