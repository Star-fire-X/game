/**
 * @file guild_component.h
 * @brief 行会组件定义
 */

#ifndef MIR2_ECS_COMPONENTS_GUILD_COMPONENT_H_
#define MIR2_ECS_COMPONENTS_GUILD_COMPONENT_H_

#include "ecs/id_types.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <unordered_set>
#include <vector>

#include <entt/entt.hpp>

namespace mir2::ecs {

// 职位等级常量
constexpr uint8_t GUILD_RANK_LEADER = 1;      // 会长
constexpr uint8_t GUILD_RANK_VICE_LEADER = 2; // 副会长
constexpr uint8_t GUILD_RANK_MEMBER = 99;     // 普通成员

// 行会关系常量
constexpr int GUILD_RELATION_NONE = 0;    // 无关系
constexpr int GUILD_RELATION_SAME = 1;    // 同行会
constexpr int GUILD_RELATION_ENEMY = 2;   // 敌对
constexpr int GUILD_RELATION_ALLY = 3;    // 同盟

// 行会颜色常量
constexpr uint8_t GUILD_COLOR_SAME = 180;  // 同行会/同盟
constexpr uint8_t GUILD_COLOR_ENEMY = 69;  // 敌对
constexpr uint8_t GUILD_COLOR_NONE = 0;    // 无关系

// 行会费用常量
constexpr uint32_t GUILD_CREATE_FEE = 1000000;  // 创建费用
constexpr uint32_t GUILD_WAR_FEE = 30000;       // 宣战费用
constexpr uint64_t GUILD_WAR_DURATION = 10800000; // 战争时长3小时(ms)

/**
 * @brief 职位记录
 */
struct GuildRank {
    uint8_t rank = GUILD_RANK_MEMBER;
    std::string rank_name;
    std::vector<CharacterId> member_ids;  // stable rank mapping
};

/**
 * @brief 战争信息
 */
struct GuildWarInfo {
    GuildId enemy_guild_id = kInvalidGuildId;
    uint64_t start_time = 0;
    uint64_t remain_time = GUILD_WAR_DURATION;
};

/**
 * @brief 行会组件（附加到行会实体）
 */
struct GuildComponent {
    GuildId guild_id = kInvalidGuildId;
    std::string guild_name;
    entt::entity leader = entt::null;
    std::vector<entt::entity> members;
    mutable std::unordered_set<entt::entity> member_index;
    uint8_t max_members = 100;

    // 职位系统
    std::vector<GuildRank> ranks;

    // 公告系统
    std::vector<std::string> notice_list;

    // 战争系统
    std::vector<GuildWarInfo> war_guilds;

    // 同盟系统
    std::vector<GuildId> ally_guild_ids;
    bool allow_ally = true;

    // 城堡归属
    bool owns_castle = false;

    // 门派对战
    bool in_team_fight = false;
    int32_t match_point = 0;
    std::vector<entt::entity> fight_members;
    mutable std::unordered_set<entt::entity> fight_member_index;

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

    void RebuildFightMemberIndex() const {
        fight_member_index.clear();
        fight_member_index.reserve(fight_members.size());
        for (entt::entity member : fight_members) {
            if (member != entt::null) {
                fight_member_index.insert(member);
            }
        }
    }

    bool IsFightMember(entt::entity e) const {
        if (e == entt::null) {
            return false;
        }
        if (fight_member_index.size() != fight_members.size()) {
            RebuildFightMemberIndex();
        }
        return fight_member_index.contains(e);
    }

    bool AddFightMember(entt::entity e) {
        if (e == entt::null) {
            return false;
        }
        if (fight_member_index.size() != fight_members.size()) {
            RebuildFightMemberIndex();
        }
        if (!fight_member_index.insert(e).second) {
            return false;
        }
        fight_members.push_back(e);
        return true;
    }

    void ClearFightMembers() {
        fight_members.clear();
        fight_member_index.clear();
    }

    bool IsAtWarWith(GuildId guild_id) const {
        for (const auto& war : war_guilds) {
            if (war.enemy_guild_id == guild_id) return true;
        }
        return false;
    }

    bool IsAlliedWith(GuildId guild_id) const {
        for (const auto& ally_id : ally_guild_ids) {
            if (ally_id == guild_id) return true;
        }
        return false;
    }
};

/**
 * @brief 行会成员标记组件（附加到玩家）
 */
struct GuildMemberComponent {
    GuildId guild_id = kInvalidGuildId;
    uint8_t rank = GUILD_RANK_MEMBER;
    std::string rank_name;
    CharacterId character_id = kInvalidCharacterId;
};

}  // namespace mir2::ecs

#endif  // MIR2_ECS_COMPONENTS_GUILD_COMPONENT_H_
