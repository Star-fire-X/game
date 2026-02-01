#ifndef LEGEND2_SERVER_ECS_SKILL_SYSTEM_H
#define LEGEND2_SERVER_ECS_SKILL_SYSTEM_H

#include "ecs/systems/skill_result.h"
#include "ecs/skill_registry.h"
#include "common/types.h"

#include <entt/entt.hpp>

namespace mir2::ecs {

class EventBus;
class EffectBroadcaster;

class SkillSystem {
public:
    explicit SkillSystem(entt::registry& registry);

    void set_event_bus(EventBus* event_bus);
    void set_effect_broadcaster(EffectBroadcaster* broadcaster);

    // Learn skill
    mir2::common::ErrorCode learn_skill(entt::entity entity, uint32_t skill_id);
    bool can_learn_skill(entt::entity entity, uint32_t skill_id) const;

    // Cast skill
    SkillCastResult cast_skill(
        entt::entity caster,
        uint32_t skill_id,
        entt::entity target = entt::null,
        const mir2::common::Position* target_pos = nullptr);

    // Cooldown check
    bool is_skill_ready(entt::entity entity, uint32_t skill_id) const;

    // Training points
    void add_training_points(entt::entity entity, uint32_t skill_id, int points);

    // Per-frame update
    void update(int64_t current_time_ms);

    // Interrupt casting if possible
    bool interrupt_casting(entt::entity entity);

private:
    entt::registry& registry_;
    int64_t current_time_ms_ = 0;
    EventBus* event_bus_ = nullptr;
    EffectBroadcaster* effect_broadcaster_ = nullptr;

    mir2::common::ErrorCode validate_cast(entt::entity caster, const SkillTemplate& skill) const;
    void apply_skill_effect(entt::entity caster, entt::entity target,
                            const SkillTemplate& skill, int skill_level);
    void apply_damage(entt::entity caster, entt::entity target, uint32_t skill_id, int damage);
    void apply_healing(entt::entity caster, entt::entity target, uint32_t skill_id, int healing);
};

} // namespace mir2::ecs
#endif
