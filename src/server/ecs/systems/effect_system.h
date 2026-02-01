#ifndef LEGEND2_SERVER_ECS_EFFECT_SYSTEM_H
#define LEGEND2_SERVER_ECS_EFFECT_SYSTEM_H

#include "ecs/components/effect_component.h"
#include <entt/entt.hpp>

namespace mir2::ecs {

class EffectSystem {
public:
    explicit EffectSystem(entt::registry& registry);

    // Apply effect to target
    void apply_effect(entt::entity target, const ActiveEffect& effect);

    // Remove effect by skill id
    void remove_effect(entt::entity target, uint32_t skill_id);

    // Per-frame update
    void update(int64_t current_time_ms);

    // Shield damage absorption
    int absorb_damage(entt::entity entity, int damage);

    // Invisibility checks
    bool is_invisible(entt::entity entity) const;
    void break_invisibility(entt::entity entity);

    // Movement restriction checks
    bool is_immobilized(entt::entity entity) const;

    // Check if entity has frenzy effect
    bool has_frenzy(entt::entity entity) const;

    // Get frenzy multipliers
    float get_attack_multiplier(entt::entity entity) const;
    float get_defense_multiplier(entt::entity entity) const;

private:
    entt::registry& registry_;
    int64_t current_time_ms_ = 0;

    void process_dot_effects(int64_t now_ms);
    void process_poison_effects(int64_t now_ms);
    void process_frenzy_effects();
    void process_expired_effects(int64_t now_ms);
    void apply_stat_modifiers(entt::entity entity);
};

} // namespace mir2::ecs
#endif
