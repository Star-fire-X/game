/**
 * @file i_guild_port.h
 * @brief ECS <-> guild domain operations port interface.
 */

#ifndef MIR2_SERVER_GAME_PORTS_I_GUILD_PORT_H_
#define MIR2_SERVER_GAME_PORTS_I_GUILD_PORT_H_

#include <string>

#include <entt/entt.hpp>

#include "ecs/components/guild_component.h"

namespace mir2::game::ports {

class IGuildPort {
 public:
  virtual ~IGuildPort() = default;

  virtual entt::entity CreateGuild(mir2::ecs::GuildId guild_id,
                                   const std::string& name,
                                   entt::entity leader,
                                   entt::registry& registry) = 0;
  virtual bool DeleteGuild(mir2::ecs::GuildId guild_id,
                           entt::registry& registry) = 0;
  virtual entt::entity GetGuildEntity(mir2::ecs::GuildId guild_id) const = 0;
  virtual entt::entity GetGuildByName(const std::string& name) const = 0;
  virtual mir2::ecs::GuildComponent* GetGuild(mir2::ecs::GuildId guild_id,
                                              entt::registry& registry) = 0;
  virtual bool AddMember(mir2::ecs::GuildId guild_id,
                         entt::entity member,
                         entt::registry& registry) = 0;
  virtual bool RemoveMember(mir2::ecs::GuildId guild_id,
                            entt::entity member,
                            entt::registry& registry) = 0;
  virtual bool DeclareWar(mir2::ecs::GuildId attacker_id,
                          mir2::ecs::GuildId target_id,
                          entt::registry& registry) = 0;
  virtual bool CancelWar(mir2::ecs::GuildId guild1_id,
                         mir2::ecs::GuildId guild2_id,
                         entt::registry& registry) = 0;
  virtual bool IsAtWar(mir2::ecs::GuildId guild1_id,
                       mir2::ecs::GuildId guild2_id,
                       entt::registry& registry) const = 0;
  virtual bool BreakAlliance(mir2::ecs::GuildId guild1_id,
                             mir2::ecs::GuildId guild2_id,
                             entt::registry& registry) = 0;
  virtual bool IsAllied(mir2::ecs::GuildId guild1_id,
                        mir2::ecs::GuildId guild2_id,
                        entt::registry& registry) const = 0;
  virtual mir2::ecs::GuildId GenerateGuildId() = 0;
};

}  // namespace mir2::game::ports

#endif  // MIR2_SERVER_GAME_PORTS_I_GUILD_PORT_H_
