/**
 * @file skill_executor.cc
 * @brief Client skill executor implementation.
 */

#include "skill_executor.h"

#include <chrono>
#include <utility>

namespace mir2::client::skill {

SkillExecutor::SkillExecutor(SkillManager& manager)
    : manager_(manager) {}

void SkillExecutor::set_send_callback(SendSkillRequestFn callback) {
    send_callback_ = std::move(callback);
}

SkillUseResult SkillExecutor::try_use_skill_by_hotkey(uint8_t slot, int current_mp, uint64_t target_id) {
    const uint32_t skill_id = manager_.get_skill_by_hotkey(slot);
    if (skill_id == 0) {
        return SkillUseResult::NotLearned;
    }
    return try_use_skill(skill_id, current_mp, target_id);
}

SkillUseResult SkillExecutor::try_use_skill(uint32_t skill_id, int current_mp, uint64_t target_id) {
    if (!manager_.has_skill(skill_id)) {
        return SkillUseResult::NotLearned;
    }

    if (manager_.is_casting()) {
        return SkillUseResult::AlreadyCasting;
    }

    const ClientSkillTemplate* skill = manager_.get_template(skill_id);
    if (!skill) {
        return SkillUseResult::NotLearned;
    }

    const int64_t now = now_ms();
    if (!manager_.is_ready(skill_id, now)) {
        return SkillUseResult::OnCooldown;
    }

    if (current_mp < skill->mp_cost) {
        return SkillUseResult::NotEnoughMP;
    }

    if (!is_target_valid(*skill, target_id)) {
        return SkillUseResult::InvalidTarget;
    }

    if (send_callback_) {
        send_callback_(skill_id, target_id);
    }

    return SkillUseResult::Success;
}

int64_t SkillExecutor::now_ms() {
    using clock = std::chrono::steady_clock;
    return std::chrono::duration_cast<std::chrono::milliseconds>(clock::now().time_since_epoch()).count();
}

bool SkillExecutor::is_target_valid(const ClientSkillTemplate& skill, uint64_t target_id) const {
    switch (skill.target_type) {
        case mir2::common::SkillTarget::SELF:
            return true;
        case mir2::common::SkillTarget::SINGLE_ENEMY:
        case mir2::common::SkillTarget::SINGLE_ALLY:
            return target_id != 0;
        case mir2::common::SkillTarget::AOE_ENEMY:
        case mir2::common::SkillTarget::AOE_ALLY:
        case mir2::common::SkillTarget::AOE_ALL:
            if (target_id != 0) {
                return true;
            }
            return skill.range <= 0.0f;
        default:
            return false;
    }
}

} // namespace mir2::client::skill
