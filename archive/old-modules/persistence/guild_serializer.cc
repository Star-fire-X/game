/**
 * @file guild_serializer.cc
 * @brief Guild component JSON serialization implementation
 */

#include "persistence/guild_serializer.h"

namespace mir2::persistence {

nlohmann::json SerializeGuild(const ecs::GuildComponent& guild) {
    nlohmann::json j;
    j["guild_id"] = guild.guild_id;
    j["guild_name"] = guild.guild_name;
    j["max_members"] = guild.max_members;

    j["ranks"] = nlohmann::json::array();
    for (const auto& rank : guild.ranks) {
        j["ranks"].push_back({
            {"rank", rank.rank},
            {"rank_name", rank.rank_name},
            {"member_names", rank.member_names}
        });
    }

    j["notice_list"] = guild.notice_list;

    j["war_guilds"] = nlohmann::json::array();
    for (const auto& war : guild.war_guilds) {
        j["war_guilds"].push_back({
            {"enemy_guild_id", war.enemy_guild_id},
            {"remain_time", war.remain_time}
        });
    }

    j["ally_guild_ids"] = guild.ally_guild_ids;
    j["allow_ally"] = guild.allow_ally;

    return j;
}

ecs::GuildComponent DeserializeGuild(const nlohmann::json& j) {
    ecs::GuildComponent guild;
    guild.guild_id = j.value("guild_id", 0U);
    guild.guild_name = j.value("guild_name", "");
    guild.max_members = j.value("max_members", static_cast<uint8_t>(100));

    if (j.contains("ranks") && j["ranks"].is_array()) {
        for (const auto& rank_j : j["ranks"]) {
            ecs::GuildRank rank;
            rank.rank = rank_j.value("rank", ecs::GUILD_RANK_MEMBER);
            rank.rank_name = rank_j.value("rank_name", "");
            if (rank_j.contains("member_names") && rank_j["member_names"].is_array()) {
                rank.member_names = rank_j["member_names"].get<std::vector<std::string>>();
            }
            guild.ranks.push_back(rank);
        }
    }

    if (j.contains("notice_list") && j["notice_list"].is_array()) {
        guild.notice_list = j["notice_list"].get<std::vector<std::string>>();
    }

    if (j.contains("war_guilds") && j["war_guilds"].is_array()) {
        for (const auto& war_j : j["war_guilds"]) {
            ecs::GuildWarInfo info;
            info.enemy_guild_id = war_j.value("enemy_guild_id", 0U);
            info.remain_time = war_j.value(
                "remain_time",
                static_cast<uint64_t>(ecs::GUILD_WAR_DURATION));
            guild.war_guilds.push_back(info);
        }
    }

    if (j.contains("ally_guild_ids") && j["ally_guild_ids"].is_array()) {
        guild.ally_guild_ids = j["ally_guild_ids"].get<std::vector<uint32_t>>();
    }

    guild.allow_ally = j.value("allow_ally", true);

    return guild;
}

nlohmann::json SerializeGuildMember(const ecs::GuildMemberComponent& member) {
    return nlohmann::json{
        {"guild_id", member.guild_id},
        {"rank", member.rank},
        {"rank_name", member.rank_name}
    };
}

ecs::GuildMemberComponent DeserializeGuildMember(const nlohmann::json& j) {
    ecs::GuildMemberComponent member;
    member.guild_id = j.value("guild_id", 0U);
    member.rank = j.value("rank", ecs::GUILD_RANK_MEMBER);
    member.rank_name = j.value("rank_name", "");
    return member;
}

}  // namespace mir2::persistence
