/**
 * @file skill_data.h
 * @brief Client-side skill data structures.
 */

#ifndef LEGEND2_CLIENT_GAME_SKILL_SKILL_DATA_H
#define LEGEND2_CLIENT_GAME_SKILL_SKILL_DATA_H

#include "common/enums.h"

#include <cstdint>
#include <string>

namespace mir2::client::skill {

/**
 * @brief Client skill template data.
 */
struct ClientSkillTemplate {
    uint32_t id = 0;    ///< Skill ID.
    std::string name;   ///< Skill name.
    std::string description;   ///< Skill description.
    int mp_cost = 0;    ///< MP cost.
    int cooldown_ms = 0;    ///< Cooldown in milliseconds.
    int cast_time_ms = 0;   ///< Cast time in milliseconds.
    float range = 0.0f;     ///< Cast range.
    float aoe_radius = 0.0f;    ///< AOE radius.
    mir2::common::SkillType skill_type = mir2::common::SkillType::PHYSICAL;   ///< Skill type.
    mir2::common::SkillTarget target_type = mir2::common::SkillTarget::SELF;  ///< Target type.
    uint32_t icon_id = 0;   ///< Icon ID.
    uint32_t effect_id = 0; ///< Effect ID.
    bool is_passive = false;    ///< Passive skill flag.
};

/**
 * @brief Learned skill state on the client.
 */
struct ClientLearnedSkill {
    uint32_t skill_id = 0;  ///< Skill template ID.
    uint8_t level = 0;      ///< Skill level (0-3).
    int32_t train_points = 0;   ///< Training points.
    uint8_t hotkey = 0;     ///< Hotkey slot (0=unbound, 1-8=F1-F8).
};

/**
 * @brief Skill cooldown state.
 */
struct SkillCooldownState {
    uint32_t skill_id = 0;      ///< Skill template ID.
    int64_t cooldown_end_ms = 0;    ///< Cooldown end time in ms.
    int64_t total_cooldown_ms = 0;  ///< Total cooldown duration in ms.
};

/**
 * @brief Casting state.
 */
struct CastingState {
    bool is_casting = false;    ///< Whether casting is in progress.
    uint32_t skill_id = 0;      ///< Casting skill ID.
    uint64_t target_id = 0;     ///< Target entity ID.
    int64_t cast_start_ms = 0;  ///< Cast start time in ms.
    int64_t cast_end_ms = 0;    ///< Cast end time in ms.

    /**
     * @brief Get casting progress (0.0-1.0).
     */
    float get_progress(int64_t now_ms) const {
        if (!is_casting || cast_end_ms <= cast_start_ms) {
            return 0.0f;
        }
        int64_t elapsed = now_ms - cast_start_ms;
        if (elapsed <= 0) {
            return 0.0f;
        }
        int64_t total = cast_end_ms - cast_start_ms;
        float progress = static_cast<float>(elapsed) / static_cast<float>(total);
        if (progress < 0.0f) {
            return 0.0f;
        }
        return progress > 1.0f ? 1.0f : progress;
    }

    /**
     * @brief Check whether casting is complete.
     */
    bool is_complete(int64_t now_ms) const {
        return is_casting && now_ms >= cast_end_ms;
    }

    /**
     * @brief Cancel casting.
     */
    void cancel() {
        is_casting = false;
        skill_id = 0;
        target_id = 0;
        cast_start_ms = 0;
        cast_end_ms = 0;
    }
};

} // namespace mir2::client::skill

#endif // LEGEND2_CLIENT_GAME_SKILL_SKILL_DATA_H
