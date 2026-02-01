#include "ecs/systems/effect_system.h"

#include "ecs/components/character_components.h"
#include "ecs/dirty_tracker.h"

#include <algorithm>
#include <cstdlib>

namespace mir2::ecs {

EffectSystem::EffectSystem(entt::registry& registry)
    : registry_(registry) {}

void EffectSystem::apply_effect(entt::entity target, const ActiveEffect& effect) {
    if (!registry_.valid(target)) {
        return;
    }

    auto& effects = registry_.get_or_emplace<EffectListComponent>(target);
    effects.add_effect(effect);

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

    effects->remove_effects_by_skill(skill_id);
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
        effects->effects.erase(
            std::remove_if(
                effects->effects.begin(),
                effects->effects.end(),
                [](const ActiveEffect& effect) {
                    return effect.category == EffectCategory::SHIELD &&
                        effect.shield_remaining <= 0;
                }),
            effects->effects.end());
    }

    return damage;
}

bool EffectSystem::is_invisible(entt::entity entity) const {
    const auto* effects = registry_.try_get<EffectListComponent>(entity);
    if (!effects) {
        return false;
    }

    for (const auto& effect : effects->effects) {
        if (effect.category == EffectCategory::INVISIBLE) {
            return true;
        }
    }

    return false;
}

void EffectSystem::break_invisibility(entt::entity entity) {
    auto* effects = registry_.try_get<EffectListComponent>(entity);
    if (!effects) {
        return;
    }

    effects->effects.erase(
        std::remove_if(
            effects->effects.begin(),
            effects->effects.end(),
            [](const ActiveEffect& effect) {
                return effect.category == EffectCategory::INVISIBLE;
            }),
        effects->effects.end());
}

bool EffectSystem::is_immobilized(entt::entity entity) const {
    const auto* effects = registry_.try_get<EffectListComponent>(entity);
    if (!effects) {
        return false;
    }

    for (const auto& effect : effects->effects) {
        if (effect.category == EffectCategory::STUN ||
            effect.category == EffectCategory::HOLY_SEIZE ||
            effect.category == EffectCategory::PARALYSIS) {
            return true;
        }
    }

    return false;
}

void EffectSystem::process_dot_effects(int64_t now_ms) {
    auto view = registry_.view<EffectListComponent, CharacterAttributesComponent>();
    for (auto entity : view) {
        auto& effects = view.get<EffectListComponent>(entity);
        auto& attributes = view.get<CharacterAttributesComponent>(entity);

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

        bool stat_expired = false;
        for (const auto& effect : effects.effects) {
            if (effect.end_time_ms > 0 && effect.end_time_ms <= now_ms &&
                (effect.category == EffectCategory::STAT_BUFF ||
                 effect.category == EffectCategory::STAT_DEBUFF)) {
                stat_expired = true;
                break;
            }
        }

        effects.remove_expired(now_ms);

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
    for (const auto& effect : effects->effects) {
        if (effect.category == EffectCategory::STAT_BUFF) {
            attack_bonus += effect.value;
        } else if (effect.category == EffectCategory::STAT_DEBUFF) {
            defense_penalty += effect.value;
        }
    }

    const int delta_attack = attack_bonus - effects->applied_attack_bonus;
    const int delta_defense = defense_penalty - effects->applied_defense_penalty;
    if (delta_attack == 0 && delta_defense == 0) {
        return;
    }

    attributes->attack += delta_attack;
    attributes->defense -= delta_defense;
    effects->applied_attack_bonus = attack_bonus;
    effects->applied_defense_penalty = defense_penalty;

    dirty_tracker::mark_attributes_dirty(registry_, entity);
}

void EffectSystem::process_poison_effects(int64_t now_ms) {
    auto view = registry_.view<EffectListComponent, CharacterAttributesComponent>();
    for (auto entity : view) {
        auto& effects = view.get<EffectListComponent>(entity);
        auto& attributes = view.get<CharacterAttributesComponent>(entity);

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
    for (const auto& effect : effects->effects) {
        if (effect.category == EffectCategory::FRENZY) {
            return true;
        }
    }
    return false;
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
