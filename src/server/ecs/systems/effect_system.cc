#include "ecs/systems/effect_system.h"

#include "ecs/components/character_components.h"
#include "ecs/dirty_tracker.h"
#include "ecs/event_bus.h"
#include "ecs/events/skill_events.h"

#include <algorithm>
#include <cstdlib>
#include <vector>

namespace mir2::ecs {

EffectSystem::EffectSystem(entt::registry& registry, EventBus* event_bus)
    : registry_(registry),
      event_bus_(event_bus) {}

void EffectSystem::publish_buff_added(entt::entity target,
                                      const ActiveEffect& effect) {
    if (!event_bus_) {
        return;
    }

    events::BuffAppliedEvent event;
    event.target = target;
    event.source = effect.source_entity;
    event.category = effect.category;
    event.skill_id = effect.skill_id;
    event.duration_ms = effect.end_time_ms > effect.start_time_ms
        ? (effect.end_time_ms - effect.start_time_ms)
        : 0;
    event_bus_->Publish(event);
}

void EffectSystem::publish_buff_removed(entt::entity target,
                                        EffectCategory category,
                                        uint32_t skill_id,
                                        bool expired) {
    if (!event_bus_) {
        return;
    }

    events::BuffRemovedEvent event;
    event.target = target;
    event.category = category;
    event.skill_id = skill_id;
    event.expired = expired;
    event_bus_->Publish(event);
}

void EffectSystem::apply_effect(entt::entity target, const ActiveEffect& effect) {
    if (!registry_.valid(target)) {
        return;
    }

    auto& effects = registry_.get_or_emplace<EffectListComponent>(target);
    effects.add_effect(effect);
    publish_buff_added(target, effect);

    if (effect.category == EffectCategory::STAT_BUFF ||
        effect.category == EffectCategory::STAT_DEBUFF) {
        apply_stat_modifiers(target);
    }
}

void EffectSystem::remove_effect(entt::entity target, uint32_t skill_id) {
    if (!registry_.valid(target)) {
        return;
    }

    auto* effects = registry_.try_get<EffectListComponent>(target);
    if (!effects) {
        return;
    }

    std::vector<EffectCategory> removed_categories;
    removed_categories.reserve(effects->effects.size());
    for (const auto& effect : effects->effects) {
        if (effect.skill_id == skill_id) {
            removed_categories.push_back(effect.category);
        }
    }

    effects->remove_effects_by_skill(skill_id);
    for (EffectCategory category : removed_categories) {
        publish_buff_removed(target, category, skill_id, /*expired=*/false);
    }
    apply_stat_modifiers(target);
}

void EffectSystem::update(int64_t current_time_ms) {
    current_time_ms_ = current_time_ms;
    process_dot_effects(current_time_ms_);
    process_poison_effects(current_time_ms_);
    process_frenzy_effects();
    process_expired_effects(current_time_ms_);
}

int EffectSystem::absorb_damage(entt::entity entity, int damage) {
    if (damage <= 0) {
        return 0;
    }

    auto* effects = registry_.try_get<EffectListComponent>(entity);
    if (!effects || effects->effects.empty()) {
        return damage;
    }

    bool shield_changed = false;
    for (auto& effect : effects->effects) {
        if (effect.category != EffectCategory::SHIELD) {
            continue;
        }
        if (damage <= 0) {
            break;
        }
        if (effect.shield_remaining <= 0) {
            continue;
        }

        const int absorbed = std::min(damage, effect.shield_remaining);
        effect.shield_remaining -= absorbed;
        damage -= absorbed;
        shield_changed = true;
    }

    if (shield_changed) {
        std::vector<uint32_t> removed_skill_ids;
        const std::size_t original_size = effects->effects.size();
        for (const auto& effect : effects->effects) {
            if (effect.category == EffectCategory::SHIELD &&
                effect.shield_remaining <= 0) {
                removed_skill_ids.push_back(effect.skill_id);
            }
        }
        effects->effects.erase(
            std::remove_if(
                effects->effects.begin(),
                effects->effects.end(),
                [](const ActiveEffect& effect) {
                    return effect.category == EffectCategory::SHIELD &&
                        effect.shield_remaining <= 0;
                }),
            effects->effects.end());
        if (effects->effects.size() != original_size) {
            effects->mark_effects_dirty();
            for (uint32_t skill_id : removed_skill_ids) {
                publish_buff_removed(entity,
                                    EffectCategory::SHIELD,
                                    skill_id,
                                    /*expired=*/false);
            }
        }
    }

    return damage;
}

bool EffectSystem::is_invisible(entt::entity entity) const {
    const auto* effects = registry_.try_get<EffectListComponent>(entity);
    if (!effects) {
        return false;
    }
    return effects->has_category(EffectCategory::INVISIBLE);
}

void EffectSystem::break_invisibility(entt::entity entity) {
    auto* effects = registry_.try_get<EffectListComponent>(entity);
    if (!effects) {
        return;
    }

    std::vector<uint32_t> removed_skill_ids;
    removed_skill_ids.reserve(effects->effects.size());
    for (const auto& effect : effects->effects) {
        if (effect.category == EffectCategory::INVISIBLE) {
            removed_skill_ids.push_back(effect.skill_id);
        }
    }

    const std::size_t original_size = effects->effects.size();
    effects->effects.erase(
        std::remove_if(
            effects->effects.begin(),
            effects->effects.end(),
            [](const ActiveEffect& effect) {
                return effect.category == EffectCategory::INVISIBLE;
            }),
            effects->effects.end());
    if (effects->effects.size() != original_size) {
        effects->mark_effects_dirty();
        for (uint32_t skill_id : removed_skill_ids) {
            publish_buff_removed(entity,
                                EffectCategory::INVISIBLE,
                                skill_id,
                                /*expired=*/false);
        }
    }
}

bool EffectSystem::is_immobilized(entt::entity entity) const {
    const auto* effects = registry_.try_get<EffectListComponent>(entity);
    if (!effects) {
        return false;
    }
    return effects->has_any_category({
        EffectCategory::STUN,
        EffectCategory::HOLY_SEIZE,
        EffectCategory::PARALYSIS,
    });
}

void EffectSystem::process_dot_effects(int64_t now_ms) {
    auto view = registry_.view<EffectListComponent, CharacterAttributesComponent>();
    for (auto entity : view) {
        auto& effects = view.get<EffectListComponent>(entity);
        auto& attributes = view.get<CharacterAttributesComponent>(entity);

        if (!effects.has_any_category(
                {EffectCategory::DAMAGE_OVER_TIME, EffectCategory::HEAL_OVER_TIME})) {
            continue;
        }

        if (attributes.hp <= 0) {
            continue;
        }

        bool hp_changed = false;
        for (auto& effect : effects.effects) {
            if (effect.category != EffectCategory::DAMAGE_OVER_TIME &&
                effect.category != EffectCategory::HEAL_OVER_TIME) {
                continue;
            }

            const int interval_ms = std::max(1, effect.tick_interval_ms);
            if (now_ms - effect.last_tick_ms < interval_ms) {
                continue;
            }

            effect.last_tick_ms = now_ms;

            if (effect.category == EffectCategory::DAMAGE_OVER_TIME) {
                int damage = std::abs(effect.value);
                if (damage <= 0) {
                    continue;
                }

                damage = std::max(1, damage);
                attributes.hp = std::max(0, attributes.hp - damage);
                hp_changed = true;
                if (attributes.hp <= 0) {
                    break;
                }
            } else {
                int healing = std::abs(effect.value);
                if (healing <= 0) {
                    continue;
                }

                attributes.hp = std::min(attributes.max_hp, attributes.hp + healing);
                hp_changed = true;
            }
        }

        if (hp_changed) {
            dirty_tracker::mark_attributes_dirty(registry_, entity);
        }
    }
}

void EffectSystem::process_expired_effects(int64_t now_ms) {
    auto view = registry_.view<EffectListComponent>();
    for (auto entity : view) {
        auto& effects = view.get<EffectListComponent>(entity);
        if (!effects.has_expired(now_ms)) {
            continue;
        }

        bool stat_expired = false;
        std::vector<std::pair<EffectCategory, uint32_t>> expired_effects;
        expired_effects.reserve(effects.effects.size());
        for (const auto& effect : effects.effects) {
            if (effect.end_time_ms > 0 && effect.end_time_ms <= now_ms) {
                expired_effects.emplace_back(effect.category, effect.skill_id);
                if (effect.category == EffectCategory::STAT_BUFF ||
                    effect.category == EffectCategory::STAT_DEBUFF) {
                    stat_expired = true;
                }
            }
        }

        effects.remove_expired(now_ms);
        for (const auto& [category, skill_id] : expired_effects) {
            publish_buff_removed(entity, category, skill_id, /*expired=*/true);
        }

        if (stat_expired) {
            apply_stat_modifiers(entity);
        }
    }
}

void EffectSystem::apply_stat_modifiers(entt::entity entity) {
    auto* attributes = registry_.try_get<CharacterAttributesComponent>(entity);
    auto* effects = registry_.try_get<EffectListComponent>(entity);
    if (!attributes || !effects) {
        return;
    }

    int attack_bonus = 0;
    int defense_penalty = 0;
    int magic_attack_bonus = 0;
    int sc_bonus = 0;
    int speed_bonus = 0;
    int max_hp_bonus = 0;
    int max_mp_bonus = 0;
    for (const auto& effect : effects->effects) {
        if (effect.category != EffectCategory::STAT_BUFF &&
            effect.category != EffectCategory::STAT_DEBUFF) {
            continue;
        }

        if (effect.category == EffectCategory::STAT_BUFF) {
            attack_bonus += effect.value;
        } else {
            defense_penalty += effect.value;
        }

        magic_attack_bonus += effect.magic_attack_bonus;
        sc_bonus += effect.sc_bonus;
        speed_bonus += effect.speed_bonus;
        max_hp_bonus += effect.max_hp_bonus;
        max_mp_bonus += effect.max_mp_bonus;
    }

    const int delta_attack = attack_bonus - effects->applied_attack_bonus;
    const int delta_defense = defense_penalty - effects->applied_defense_penalty;
    const int delta_magic_attack =
        magic_attack_bonus - effects->applied_magic_attack_bonus;
    const int delta_sc = sc_bonus - effects->applied_sc_bonus;
    const int delta_speed = speed_bonus - effects->applied_speed_bonus;
    const int delta_max_hp = max_hp_bonus - effects->applied_max_hp_bonus;
    const int delta_max_mp = max_mp_bonus - effects->applied_max_mp_bonus;
    if (delta_attack == 0 && delta_defense == 0 && delta_magic_attack == 0 &&
        delta_sc == 0 && delta_speed == 0 && delta_max_hp == 0 &&
        delta_max_mp == 0) {
        return;
    }

    attributes->attack += delta_attack;
    attributes->defense -= delta_defense;
    attributes->magic_attack += delta_magic_attack;
    attributes->sc += delta_sc;
    attributes->speed = std::max(0, attributes->speed + delta_speed);
    attributes->max_hp = std::max(1, attributes->max_hp + delta_max_hp);
    attributes->max_mp = std::max(0, attributes->max_mp + delta_max_mp);
    attributes->hp = std::min(attributes->hp, attributes->max_hp);
    attributes->mp = std::min(attributes->mp, attributes->max_mp);
    effects->applied_attack_bonus = attack_bonus;
    effects->applied_defense_penalty = defense_penalty;
    effects->applied_magic_attack_bonus = magic_attack_bonus;
    effects->applied_sc_bonus = sc_bonus;
    effects->applied_speed_bonus = speed_bonus;
    effects->applied_max_hp_bonus = max_hp_bonus;
    effects->applied_max_mp_bonus = max_mp_bonus;

    dirty_tracker::mark_attributes_dirty(registry_, entity);
}

void EffectSystem::process_poison_effects(int64_t now_ms) {
    auto view = registry_.view<EffectListComponent, CharacterAttributesComponent>();
    for (auto entity : view) {
        auto& effects = view.get<EffectListComponent>(entity);
        auto& attributes = view.get<CharacterAttributesComponent>(entity);

        if (!effects.has_category(EffectCategory::POISON)) {
            continue;
        }

        if (attributes.hp <= 0) {
            continue;
        }

        bool hp_changed = false;
        for (auto& effect : effects.effects) {
            if (effect.category != EffectCategory::POISON) {
                continue;
            }

            const int interval_ms = std::max(1, effect.tick_interval_ms);
            if (now_ms - effect.last_tick_ms < interval_ms) {
                continue;
            }

            effect.last_tick_ms = now_ms;

            // 计算中毒伤害：max_hp * poison_percent / 100
            int poison_damage = attributes.max_hp * effect.poison_percent / 100;
            poison_damage = std::max(1, poison_damage);

            attributes.hp = std::max(0, attributes.hp - poison_damage);
            hp_changed = true;

            if (attributes.hp <= 0) {
                break;
            }
        }

        if (hp_changed) {
            dirty_tracker::mark_attributes_dirty(registry_, entity);
        }
    }
}

void EffectSystem::process_frenzy_effects() {
    auto view = registry_.view<EffectListComponent, CharacterAttributesComponent>();
    for (auto entity : view) {
        auto& effects = view.get<EffectListComponent>(entity);
        auto& attributes = view.get<CharacterAttributesComponent>(entity);

        if (!effects.has_category(EffectCategory::FRENZY)) {
            if (effects.applied_frenzy_attack_bonus != 0 ||
                effects.applied_frenzy_defense_penalty != 0) {
                attributes.attack -= effects.applied_frenzy_attack_bonus;
                attributes.defense += effects.applied_frenzy_defense_penalty;
                effects.applied_frenzy_attack_bonus = 0;
                effects.applied_frenzy_defense_penalty = 0;
                dirty_tracker::mark_attributes_dirty(registry_, entity);
            }
            continue;
        }

        int frenzy_attack_bonus = 0;
        int frenzy_defense_penalty = 0;

        for (const auto& effect : effects.effects) {
            if (effect.category != EffectCategory::FRENZY) {
                continue;
            }
            // 疯狂状态：攻击+50%，防御-30%
            frenzy_attack_bonus += static_cast<int>(attributes.attack * (effect.attack_multiplier - 1.0f));
            frenzy_defense_penalty += static_cast<int>(attributes.defense * (1.0f - effect.defense_multiplier));
        }

        const int delta_attack = frenzy_attack_bonus - effects.applied_frenzy_attack_bonus;
        const int delta_defense = frenzy_defense_penalty - effects.applied_frenzy_defense_penalty;

        if (delta_attack == 0 && delta_defense == 0) {
            continue;
        }

        attributes.attack += delta_attack;
        attributes.defense -= delta_defense;
        effects.applied_frenzy_attack_bonus = frenzy_attack_bonus;
        effects.applied_frenzy_defense_penalty = frenzy_defense_penalty;

        dirty_tracker::mark_attributes_dirty(registry_, entity);
    }
}

bool EffectSystem::has_frenzy(entt::entity entity) const {
    const auto* effects = registry_.try_get<EffectListComponent>(entity);
    if (!effects) {
        return false;
    }
    return effects->has_category(EffectCategory::FRENZY);
}

float EffectSystem::get_attack_multiplier(entt::entity entity) const {
    const auto* effects = registry_.try_get<EffectListComponent>(entity);
    if (!effects) {
        return 1.0f;
    }
    float multiplier = 1.0f;
    for (const auto& effect : effects->effects) {
        if (effect.category == EffectCategory::FRENZY) {
            multiplier *= effect.attack_multiplier;
        }
    }
    return multiplier;
}

float EffectSystem::get_defense_multiplier(entt::entity entity) const {
    const auto* effects = registry_.try_get<EffectListComponent>(entity);
    if (!effects) {
        return 1.0f;
    }
    float multiplier = 1.0f;
    for (const auto& effect : effects->effects) {
        if (effect.category == EffectCategory::FRENZY) {
            multiplier *= effect.defense_multiplier;
        }
    }
    return multiplier;
}

} // namespace mir2::ecs
