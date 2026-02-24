/**
 * @file guild_manager_adapter.h
 * @brief GuildManager adapter for IGuildPort.
 */

#ifndef MIR2_SERVER_GAME_GUILD_GUILD_MANAGER_ADAPTER_H_
#define MIR2_SERVER_GAME_GUILD_GUILD_MANAGER_ADAPTER_H_

#include "game/ports/i_guild_port.h"

namespace mir2::game::guild {

class GuildManager;

class GuildManagerAdapter final : public ports::IGuildPort {
 public:
  explicit GuildManagerAdapter(GuildManager& guild_manager);

  entt::entity CreateGuild(mir2::ecs::GuildId guild_id,
                           const std::string& name,
                           entt::entity leader,
                           entt::registry& registry) override;
  bool DeleteGuild(mir2::ecs::GuildId guild_id,
                   entt::registry& registry) override;
  entt::entity GetGuildEntity(mir2::ecs::GuildId guild_id) const override;
  entt::entity GetGuildByName(const std::string& name) const override;
  mir2::ecs::GuildComponent* GetGuild(mir2::ecs::GuildId guild_id,
                                      entt::registry& registry) override;
  bool AddMember(mir2::ecs::GuildId guild_id,
                 entt::entity member,
                 entt::registry& registry) override;
  bool RemoveMember(mir2::ecs::GuildId guild_id,
                    entt::entity member,
                    entt::registry& registry) override;
  bool DeclareWar(mir2::ecs::GuildId attacker_id,
                  mir2::ecs::GuildId target_id,
                  entt::registry& registry) override;
  bool CancelWar(mir2::ecs::GuildId guild1_id,
                 mir2::ecs::GuildId guild2_id,
                 entt::registry& registry) override;
  bool IsAtWar(mir2::ecs::GuildId guild1_id,
               mir2::ecs::GuildId guild2_id,
               entt::registry& registry) const override;
  bool BreakAlliance(mir2::ecs::GuildId guild1_id,
                     mir2::ecs::GuildId guild2_id,
                     entt::registry& registry) override;
  bool IsAllied(mir2::ecs::GuildId guild1_id,
                mir2::ecs::GuildId guild2_id,
                entt::registry& registry) const override;
  mir2::ecs::GuildId GenerateGuildId() override;

 private:
  GuildManager& guild_manager_;
};

}  // namespace mir2::game::guild

#endif  // MIR2_SERVER_GAME_GUILD_GUILD_MANAGER_ADAPTER_H_
