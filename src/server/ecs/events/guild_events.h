/**
 * @file guild_events.h
 * @brief Guild event definitions.
 */

#ifndef MIR2_ECS_EVENTS_GUILD_EVENTS_H_
#define MIR2_ECS_EVENTS_GUILD_EVENTS_H_

#include <cstdint>
#include <string>

#include <entt/entt.hpp>

namespace mir2::ecs::events {

/**
 * @brief Guild created event.
 */
struct GuildCreatedEvent {
  uint32_t guild_id;
  entt::entity leader;
  std::string guild_name;
};

/**
 * @brief Guild member joined event.
 */
struct GuildMemberJoinedEvent {
  uint32_t guild_id;
  entt::entity member;
  std::string member_name;
};

/**
 * @brief Guild member left event.
 */
struct GuildMemberLeftEvent {
  uint32_t guild_id;
  entt::entity member;
  std::string member_name;
  bool is_kicked;
};

/**
 * @brief Guild war declared event.
 */
struct GuildWarDeclaredEvent {
  uint32_t attacker_guild_id;
  uint32_t target_guild_id;
  uint64_t duration_ms;
};

/**
 * @brief Guild war ended event.
 */
struct GuildWarEndedEvent {
  uint32_t guild1_id;
  uint32_t guild2_id;
  bool is_timeout;
};

/**
 * @brief Guild alliance formed event.
 */
struct GuildAllianceFormedEvent {
  uint32_t guild1_id;
  uint32_t guild2_id;
};

/**
 * @brief Guild alliance broken event.
 */
struct GuildAllianceBrokenEvent {
  uint32_t guild1_id;
  uint32_t guild2_id;
};

}  // namespace mir2::ecs::events

#endif  // MIR2_ECS_EVENTS_GUILD_EVENTS_H_
