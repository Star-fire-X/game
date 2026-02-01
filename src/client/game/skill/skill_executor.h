/**
 * @file skill_executor.h
 * @brief Client skill executor.
 */

#ifndef LEGEND2_CLIENT_GAME_SKILL_SKILL_EXECUTOR_H
#define LEGEND2_CLIENT_GAME_SKILL_SKILL_EXECUTOR_H

#include "client/game/skill/skill_manager.h"

#include <cstdint>
#include <functional>

namespace mir2::client::skill {

/**
 * @brief Skill use result.
 */
enum class SkillUseResult : uint8_t {
    Success = 0,
    NotLearned,
    OnCooldown,
    NotEnoughMP,
    InvalidTarget,
    AlreadyCasting,
    OutOfRange
};

/**
 * @brief Client skill executor.
 */
class SkillExecutor {
public:
    using SendSkillRequestFn = std::function<void(uint32_t skill_id, uint64_t target_id)>;

    explicit SkillExecutor(SkillManager& manager);

    /**
     * @brief Set the skill request callback.
     */
    void set_send_callback(SendSkillRequestFn callback);

    /**
     * @brief Try to use a skill by hotkey.
     */
    SkillUseResult try_use_skill_by_hotkey(uint8_t slot, int current_mp, uint64_t target_id);

    /**
     * @brief Try to use a skill.
     */
    SkillUseResult try_use_skill(uint32_t skill_id, int current_mp, uint64_t target_id);

private:
    static int64_t now_ms();
    bool is_target_valid(const ClientSkillTemplate& skill, uint64_t target_id) const;

    SkillManager& manager_;
    SendSkillRequestFn send_callback_{};
};

} // namespace mir2::client::skill

#endif // LEGEND2_CLIENT_GAME_SKILL_SKILL_EXECUTOR_H
