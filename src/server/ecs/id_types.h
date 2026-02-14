/**
 * @file id_types.h
 * @brief ECS stable business ID type aliases and account-id adapters.
 */

#ifndef MIR2_SERVER_ECS_ID_TYPES_H_
#define MIR2_SERVER_ECS_ID_TYPES_H_

#include <charconv>
#include <cstdint>
#include <string>
#include <string_view>
#include <system_error>

namespace mir2::ecs {

using AccountId = std::uint64_t;   // cross-session stable account id
using CharacterId = std::uint32_t; // cross-session stable character id
using GuildId = std::uint32_t;     // cross-world stable guild id

inline constexpr AccountId kInvalidAccountId = 0;
inline constexpr CharacterId kInvalidCharacterId = 0;
inline constexpr GuildId kInvalidGuildId = 0;

inline bool TryParseAccountId(std::string_view text, AccountId& out) {
    if (text.empty()) {
        return false;
    }

    AccountId parsed = 0;
    const char* begin = text.data();
    const char* end = begin + text.size();
    const auto result = std::from_chars(begin, end, parsed);
    if (result.ec != std::errc() || result.ptr != end) {
        return false;
    }

    out = parsed;
    return true;
}

inline AccountId ParseAccountIdOr(std::string_view text, AccountId fallback) {
    AccountId parsed = 0;
    if (!TryParseAccountId(text, parsed)) {
        return fallback;
    }
    return parsed;
}

inline std::string AccountIdToString(AccountId account_id) {
    return std::to_string(account_id);
}

}  // namespace mir2::ecs

#endif  // MIR2_SERVER_ECS_ID_TYPES_H_
