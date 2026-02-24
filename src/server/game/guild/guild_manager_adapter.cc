#include "game/guild/guild_manager_adapter.h"

#include "game/guild/guild_manager.h"

namespace mir2::game::guild {

GuildManagerAdapter::GuildManagerAdapter(GuildManager& guild_manager)
    : guild_manager_(guild_manager) {}

entt::entity GuildManagerAdapter::CreateGuild(mir2::ecs::GuildId guild_id,
                                              const std::string& name,
                                              entt::entity leader,
                                              entt::registry& registry) {
  return guild_manager_.CreateGuild(guild_id, name, leader, registry);
}

bool GuildManagerAdapter::DeleteGuild(mir2::ecs::GuildId guild_id,
                                      entt::registry& registry) {
  return guild_manager_.DeleteGuild(guild_id, registry);
}

entt::entity GuildManagerAdapter::GetGuildEntity(
    mir2::ecs::GuildId guild_id) const {
  return guild_manager_.GetGuildEntity(guild_id);
}

entt::entity GuildManagerAdapter::GetGuildByName(const std::string& name) const {
  return guild_manager_.GetGuildByName(name);
}

mir2::ecs::GuildComponent* GuildManagerAdapter::GetGuild(
    mir2::ecs::GuildId guild_id,
    entt::registry& registry) {
  return guild_manager_.GetGuild(guild_id, registry);
}

bool GuildManagerAdapter::AddMember(mir2::ecs::GuildId guild_id,
                                    entt::entity member,
                                    entt::registry& registry) {
  return guild_manager_.AddMember(guild_id, member, registry);
}

bool GuildManagerAdapter::RemoveMember(mir2::ecs::GuildId guild_id,
                                       entt::entity member,
                                       entt::registry& registry) {
  return guild_manager_.RemoveMember(guild_id, member, registry);
}

bool GuildManagerAdapter::DeclareWar(mir2::ecs::GuildId attacker_id,
                                     mir2::ecs::GuildId target_id,
                                     entt::registry& registry) {
  return guild_manager_.DeclareWar(attacker_id, target_id, registry);
}

bool GuildManagerAdapter::CancelWar(mir2::ecs::GuildId guild1_id,
                                    mir2::ecs::GuildId guild2_id,
                                    entt::registry& registry) {
  return guild_manager_.CancelWar(guild1_id, guild2_id, registry);
}

bool GuildManagerAdapter::IsAtWar(mir2::ecs::GuildId guild1_id,
                                  mir2::ecs::GuildId guild2_id,
                                  entt::registry& registry) const {
  return guild_manager_.IsAtWar(guild1_id, guild2_id, registry);
}

bool GuildManagerAdapter::BreakAlliance(mir2::ecs::GuildId guild1_id,
                                        mir2::ecs::GuildId guild2_id,
                                        entt::registry& registry) {
  return guild_manager_.BreakAlliance(guild1_id, guild2_id, registry);
}

bool GuildManagerAdapter::IsAllied(mir2::ecs::GuildId guild1_id,
                                   mir2::ecs::GuildId guild2_id,
                                   entt::registry& registry) const {
  return guild_manager_.IsAllied(guild1_id, guild2_id, registry);
}

mir2::ecs::GuildId GuildManagerAdapter::GenerateGuildId() {
  return guild_manager_.GenerateGuildId();
}

}  // namespace mir2::game::guild
