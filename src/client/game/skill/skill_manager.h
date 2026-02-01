/**
 * @file skill_manager.h
 * @brief Client-side skill manager.
 */

#ifndef LEGEND2_CLIENT_GAME_SKILL_SKILL_MANAGER_H
#define LEGEND2_CLIENT_GAME_SKILL_SKILL_MANAGER_H

#include "client/game/skill/skill_data.h"

#include <array>
#include <optional>
#include <string>
#include <unordered_map>

namespace mir2::client::skill {

/**
 * @brief Client skill manager.
 */
class SkillManager {
public:
    /// Maximum learned skills.
    static constexpr size_t kMaxSkills = 20;
    /// Hotkey slots (F1-F8).
    static constexpr size_t kHotkeyCount = 8;

    SkillManager() = default;

    SkillManager(const SkillManager&) = delete;
    SkillManager& operator=(const SkillManager&) = delete;

    /**
     * @brief Load skill templates from YAML.
     * @param path YAML file path.
     * @return true on success.
     */
    bool load_templates_from_yaml(const std::string& path);

    /**
     * @brief Add a learned skill.
     * @return true if added.
     */
    bool add_learned_skill(const ClientLearnedSkill& skill);

    /**
     * @brief Remove a learned skill.
     * @return true if removed.
     */
    bool remove_learned_skill(uint32_t skill_id);

    /**
     * @brief Get a skill template.
     * @return Template pointer or nullptr.
     */
    const ClientSkillTemplate* get_template(uint32_t skill_id) const;

    /**
     * @brief Get a learned skill.
     */
    const ClientLearnedSkill* get_learned_skill(uint32_t skill_id) const;

    /**
     * @brief Get all learned skills.
     */
    const std::array<std::optional<ClientLearnedSkill>, kMaxSkills>& get_learned_skills() const {
        return learned_skills_;
    }

    /**
     * @brief Check whether the skill is learned.
     */
    bool has_skill(uint32_t skill_id) const;

    /**
     * @brief Bind a skill to a hotkey slot.
     * @param slot Hotkey slot (1-8).
     * @return true if bound.
     */
    bool bind_hotkey(uint8_t slot, uint32_t skill_id);

    /**
     * @brief Unbind a hotkey slot.
     * @param slot Hotkey slot (1-8).
     * @return true if unbound.
     */
    bool unbind_hotkey(uint8_t slot);

    /**
     * @brief Get the skill bound to a hotkey slot.
     * @param slot Hotkey slot (1-8).
     * @return Skill ID or 0 if unbound.
     */
    uint32_t get_skill_by_hotkey(uint8_t slot) const;

    /**
     * @brief Check whether a skill cooldown is ready.
     */
    bool is_ready(uint32_t skill_id, int64_t now_ms) const;

    /**
     * @brief Start a cooldown.
     */
    void start_cooldown(uint32_t skill_id, int64_t duration_ms, int64_t now_ms);

    /**
     * @brief Get remaining cooldown time.
     * @return Remaining milliseconds.
     */
    int64_t get_remaining_cooldown_ms(uint32_t skill_id, int64_t now_ms) const;

    /**
     * @brief Start casting.
     */
    void start_casting(uint32_t skill_id, uint64_t target_id, int64_t cast_time_ms, int64_t now_ms);

    /**
     * @brief Cancel casting.
     */
    void cancel_casting();

    /**
     * @brief Whether currently casting.
     */
    bool is_casting() const { return casting_state_.is_casting; }

    /**
     * @brief Get the current casting state.
     */
    const CastingState& get_casting_state() const { return casting_state_; }

    /**
     * @brief Update state (cleanup expired cooldowns).
     */
    void update(int64_t now_ms);

private:
    std::unordered_map<uint32_t, ClientSkillTemplate> templates_;
    std::array<std::optional<ClientLearnedSkill>, kMaxSkills> learned_skills_{};
    std::array<uint32_t, kHotkeyCount> hotkey_bindings_{};
    std::unordered_map<uint32_t, SkillCooldownState> cooldowns_;
    CastingState casting_state_{};
};

} // namespace mir2::client::skill

#endif // LEGEND2_CLIENT_GAME_SKILL_SKILL_MANAGER_H
