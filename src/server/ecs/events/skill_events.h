#ifndef LEGEND2_SERVER_ECS_SKILL_EVENTS_H
#define LEGEND2_SERVER_ECS_SKILL_EVENTS_H

#include "ecs/components/effect_component.h"
#include "ecs/systems/skill_result.h"
#include <cstdint>
#include <entt/entt.hpp>

namespace mir2::ecs::events {

struct SkillCastEvent {
    entt::entity caster;
    uint32_t skill_id;
    SkillCastResult result;
};

struct SkillLearnedEvent {
    entt::entity entity;
    uint32_t skill_id;
};

struct SkillLevelUpEvent {
    entt::entity entity;
    uint32_t skill_id;
    uint8_t new_level;
};

struct EffectAppliedEvent {
    entt::entity target;
    ActiveEffect effect;
};

struct EffectExpiredEvent {
    entt::entity target;
    uint32_t skill_id;
};

struct DamageDealtEvent {
    entt::entity source;
    entt::entity target;
    uint32_t skill_id;
    int damage;
    bool is_critical;
};

struct HealingDoneEvent {
    entt::entity source;
    entt::entity target;
    uint32_t skill_id;
    int healing;
};

struct EntityDeathEvent {
    entt::entity entity;
    entt::entity killer;
    uint32_t killing_skill_id;
};

struct BuffAppliedEvent {
    entt::entity target;
    entt::entity source;
    EffectCategory category;
    uint32_t skill_id;
    int64_t duration_ms;
};

struct BuffRemovedEvent {
    entt::entity target;
    EffectCategory category;
    uint32_t skill_id;
    bool expired;  // true=自然过期, false=被驱散
};

} // namespace mir2::ecs::events
#endif
