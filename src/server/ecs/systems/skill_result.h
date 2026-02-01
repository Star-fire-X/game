#ifndef LEGEND2_SERVER_ECS_SKILL_RESULT_H
#define LEGEND2_SERVER_ECS_SKILL_RESULT_H

#include "ecs/components/effect_component.h"
#include "common/types/error_codes.h"

#include <cstdint>
#include <optional>
#include <vector>

namespace mir2::ecs {

struct TargetResult {
    uint32_t target = 0;
    int damage_dealt = 0;
    int healing_done = 0;
    bool target_died = false;
};

struct SkillCastResult {
    bool success = false;
    mir2::common::ErrorCode error_code = mir2::common::ErrorCode::SUCCESS;
    int mp_consumed = 0;
    std::vector<TargetResult> targets;
    std::optional<ActiveEffect> applied_effect;

    static SkillCastResult ok() {
        SkillCastResult r; r.success = true; return r;
    }
    static SkillCastResult error(mir2::common::ErrorCode code) {
        SkillCastResult r; r.error_code = code; return r;
    }
};

} // namespace mir2::ecs
#endif
