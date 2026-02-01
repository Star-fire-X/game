#include "ecs/systems/effect_broadcaster.h"

#include "ecs/components/character_components.h"

#include <utility>

namespace mir2::ecs {

namespace {
constexpr uint8_t kEffectCast = 1;
constexpr uint8_t kEffectHit = 3;

uint64_t ResolveEntityId(const entt::registry& registry, entt::entity entity) {
    if (entity == entt::null || !registry.valid(entity)) {
        return 0;
    }

    if (const auto* identity = registry.try_get<CharacterIdentityComponent>(entity)) {
        return identity->id;
    }

    return static_cast<uint64_t>(entity);
}

std::string ResolveEffectId(const SkillTemplate& skill) {
    if (skill.effect_id != 0) {
        return std::to_string(skill.effect_id);
    }
    if (!skill.animation_id.empty()) {
        return skill.animation_id;
    }
    return {};
}

uint32_t ResolveDurationMs(uint8_t effect_type, const SkillTemplate& skill) {
    if (effect_type == kEffectCast && skill.cast_time_ms > 0) {
        return static_cast<uint32_t>(skill.cast_time_ms);
    }
    if (skill.duration_ms > 0) {
        return static_cast<uint32_t>(skill.duration_ms);
    }
    return 0;
}

} // namespace

EffectBroadcaster::EffectBroadcaster(entt::registry& registry)
    : registry_(registry) {}

void EffectBroadcaster::set_broadcast_callback(EffectBroadcastCallback callback) {
    broadcast_callback_ = std::move(callback);
}

void EffectBroadcaster::broadcast_skill_effect(entt::entity caster, entt::entity target,
                                               const SkillTemplate& skill, uint8_t effect_type) {
    if (!broadcast_callback_) {
        return;
    }

    const std::string effect_id = ResolveEffectId(skill);
    const std::string& sound_id = skill.sound_id;
    if (effect_id.empty() && sound_id.empty()) {
        return;
    }

    const uint64_t caster_id = ResolveEntityId(registry_, caster);
    const uint64_t target_id = ResolveEntityId(registry_, target);

    mir2::common::Position position{};
    if (effect_type == kEffectCast) {
        position = get_entity_position(caster);
    } else if (target != entt::null && registry_.valid(target)) {
        position = get_entity_position(target);
    } else {
        position = get_entity_position(caster);
    }

    const uint32_t duration_ms = ResolveDurationMs(effect_type, skill);

    broadcast_callback_(caster_id, target_id, skill.id, effect_type,
                        effect_id, sound_id, position.x, position.y, duration_ms);
}

void EffectBroadcaster::broadcast_cast_effect(entt::entity caster, const SkillTemplate& skill) {
    broadcast_skill_effect(caster, entt::null, skill, kEffectCast);
}

void EffectBroadcaster::broadcast_hit_effect(entt::entity target, const SkillTemplate& skill) {
    broadcast_skill_effect(entt::null, target, skill, kEffectHit);
}

void EffectBroadcaster::broadcast_hit_effect(entt::entity caster, entt::entity target,
                                             const SkillTemplate& skill) {
    broadcast_skill_effect(caster, target, skill, kEffectHit);
}

mir2::common::Position EffectBroadcaster::get_entity_position(entt::entity entity) const {
    if (entity == entt::null || !registry_.valid(entity)) {
        return {};
    }

    if (const auto* state = registry_.try_get<CharacterStateComponent>(entity)) {
        return state->position;
    }

    return {};
}

} // namespace mir2::ecs
