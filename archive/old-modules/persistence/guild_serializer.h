/**
 * @file guild_serializer.h
 * @brief Guild component JSON serialization helpers
 */

#ifndef MIR2_PERSISTENCE_GUILD_SERIALIZER_H
#define MIR2_PERSISTENCE_GUILD_SERIALIZER_H

#include "ecs/components/guild_component.h"
#include "nlohmann/json.hpp"

namespace mir2::persistence {

nlohmann::json SerializeGuild(const ecs::GuildComponent& guild);

ecs::GuildComponent DeserializeGuild(const nlohmann::json& j);

nlohmann::json SerializeGuildMember(const ecs::GuildMemberComponent& member);

ecs::GuildMemberComponent DeserializeGuildMember(const nlohmann::json& j);

}  // namespace mir2::persistence

#endif  // MIR2_PERSISTENCE_GUILD_SERIALIZER_H
