/**
 * @file id_types.h
 * @brief ECS stable business ID type aliases.
 */

#ifndef MIR2_SERVER_ECS_ID_TYPES_H_
#define MIR2_SERVER_ECS_ID_TYPES_H_

#include <cstdint>

namespace mir2::ecs {

using AccountId = std::uint64_t;   // cross-session stable account id
using CharacterId = std::uint32_t; // cross-session stable character id
using GuildId = std::uint32_t;     // cross-world stable guild id

inline constexpr AccountId kInvalidAccountId = 0;
inline constexpr CharacterId kInvalidCharacterId = 0;
inline constexpr GuildId kInvalidGuildId = 0;

}  // namespace mir2::ecs

#endif  // MIR2_SERVER_ECS_ID_TYPES_H_
