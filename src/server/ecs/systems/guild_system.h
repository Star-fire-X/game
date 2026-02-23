/**
 * @file guild_system.h
 * @brief ECS guild logic system
 */

#ifndef MIR2_SERVER_ECS_SYSTEMS_GUILD_SYSTEM_H
#define MIR2_SERVER_ECS_SYSTEMS_GUILD_SYSTEM_H

#include <string>
#include <vector>

#include <entt/entt.hpp>

#include "ecs/components/guild_component.h"
#include "ecs/world.h"
#include "game/guild/guild_manager.h"

namespace mir2::ecs {

class EventBus;

/**
 * @brief Guild logic system
 */
class GuildSystem : public System {
 public:
    explicit GuildSystem(EventBus& event_bus, game::guild::GuildManager& guild_mgr);

    void Update(entt::registry& registry, float delta_time) override;

    // Create guild. Returns: 0=success, -1=already in guild, -2=insufficient gold,
    // -3=missing item, -4=duplicate name.
    int CreateGuild(entt::registry& registry, entt::entity player,
                    const std::string& guild_name);

    bool JoinGuild(entt::registry& registry, entt::entity player, GuildId guild_id);
    bool LeaveGuild(entt::registry& registry, entt::entity player);
    // Dissolve guild. Returns: 0=success, -1=not leader, -2=member count > 1.
    int DissolveGuild(entt::registry& registry, entt::entity player);
    bool KickMember(entt::registry& registry, entt::entity kicker, entt::entity target);
    // Transfer leadership. Returns: 0=success, -1=not leader, -2=target not in guild.
    int TransferLeadership(entt::registry& registry, entt::entity current_leader,
                           entt::entity new_leader);
    // Update rank structure. Returns: 0=success, -2=leader missing, -3=leader rank empty,
    // -4=leader count exceeded, -5=no leader online, -6=member mismatch, -7=invalid rank.
    int UpdateRankStructure(entt::registry& registry, entt::entity player,
                            const std::vector<ecs::GuildRank>& new_ranks);
    // Update member rank based on current rank structure.
    bool SetMemberRank(entt::registry& registry, entt::entity operator_entity,
                       CharacterId target_character_id, uint8_t new_rank);

    // Declare war. Returns: 0=success, -1=not leader, -2=insufficient gold,
    // -3=target not found, -4=allied.
    int DeclareWar(entt::registry& registry, entt::entity player,
                   GuildId target_guild_id);
    bool CancelWar(entt::registry& registry, entt::entity player,
                   GuildId target_guild_id);

    int MakeAlliance(entt::registry& registry, entt::entity player,
                     GuildId target_guild_id);
    bool BreakAlliance(entt::registry& registry, entt::entity player,
                       GuildId target_guild_id);

    bool UpdateNotice(entt::registry& registry, entt::entity player,
                      const std::vector<std::string>& notice_lines);
    std::vector<std::string> GetNotice(entt::registry& registry,
                                       entt::entity player);

    bool StartTeamFight(entt::registry& registry, entt::entity player);
    bool EndTeamFight(entt::registry& registry, entt::entity player);
    bool JoinTeamFight(entt::registry& registry, entt::entity player);
    void RecordTeamFightKill(entt::registry& registry, entt::entity killer,
                             entt::entity victim);

 private:
    void CheckWarTimeout(entt::registry& registry);

    EventBus& event_bus_;
    game::guild::GuildManager& guild_mgr_;
    float war_check_timer_ = 0.0f;

    static constexpr float WAR_CHECK_INTERVAL = 60.0f;
};

}  // namespace mir2::ecs

#endif  // MIR2_SERVER_ECS_SYSTEMS_GUILD_SYSTEM_H
