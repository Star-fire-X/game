/**
 * @file guild_events.h
 * @brief Guild event definitions.
 */

#ifndef MIR2_ECS_EVENTS_GUILD_EVENTS_H_
#define MIR2_ECS_EVENTS_GUILD_EVENTS_H_

#include <string>

#include <entt/entt.hpp>

#include "ecs/id_types.h"

namespace mir2::ecs::events {

/**
 * @brief Guild created event.
 */
struct GuildCreatedEvent {
  GuildId guild_id = kInvalidGuildId;
  entt::entity leader;
  std::string guild_name;
};

/**
 * @brief Guild member joined event.
 */
struct GuildMemberJoinedEvent {
  GuildId guild_id = kInvalidGuildId;
  entt::entity member;
  std::string member_name;
};

/**
 * @brief Guild member left event.
 */
struct GuildMemberLeftEvent {
  GuildId guild_id = kInvalidGuildId;
  entt::entity member;
  std::string member_name;
  bool is_kicked;
};

/**
 * @brief Guild war declared event.
 */
struct GuildWarDeclaredEvent {
  GuildId attacker_guild_id = kInvalidGuildId;
  GuildId target_guild_id = kInvalidGuildId;
  uint64_t duration_ms;
};

/**
 * @brief Guild war ended event.
 */
struct GuildWarEndedEvent {
  GuildId guild1_id = kInvalidGuildId;
  GuildId guild2_id = kInvalidGuildId;
  bool is_timeout;
};

/**
 * @brief Guild alliance formed event.
 */
struct GuildAllianceFormedEvent {
  GuildId guild1_id = kInvalidGuildId;
  GuildId guild2_id = kInvalidGuildId;
};

/**
 * @brief Guild alliance broken event.
 */
struct GuildAllianceBrokenEvent {
  GuildId guild1_id = kInvalidGuildId;
  GuildId guild2_id = kInvalidGuildId;
};

}  // namespace mir2::ecs::events

#endif  // MIR2_ECS_EVENTS_GUILD_EVENTS_H_
