/**
 * @file skill_result.h
 * @brief Skill result enum shared by client and server.
 */

#ifndef LEGEND2_COMMON_PROTOCOL_SKILL_RESULT_H
#define LEGEND2_COMMON_PROTOCOL_SKILL_RESULT_H

#include <cstdint>

namespace mir2::common {

/**
 * @brief Skill execution result (hit/miss/etc).
 */
enum class SkillResult : uint8_t {
    HIT = 0,
    MISS = 1,
    RESISTED = 2,
    BLOCKED = 3,
    IMMUNE = 4
};

} // namespace mir2::common

#endif // LEGEND2_COMMON_PROTOCOL_SKILL_RESULT_H
