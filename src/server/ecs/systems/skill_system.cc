#include "ecs/systems/skill_system.h"

#include "ecs/components/character_components.h"
#include "ecs/components/equipment_component.h"
#include "ecs/components/effect_component.h"
#include "ecs/components/item_component.h"
#include "ecs/components/skill_component.h"
#include "ecs/dirty_tracker.h"
#include "ecs/event_bus.h"
#include "ecs/events/skill_events.h"
#include "ecs/systems/combat_system.h"
#include "ecs/systems/damage_calculator.h"
#include "ecs/systems/effect_broadcaster.h"
#include "ecs/systems/spatial_query.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace mir2::ecs {

namespace {

// Amulet item shape values:
// 1-2: poison, 3: fire, 4: ice, 5: holy.
namespace AmuletShape {
    constexpr int POISON_MIN = 1;
    constexpr int POISON_MAX = 2;
    constexpr int FIRE = 3;
    constexpr int ICE = 4;
    constexpr int HOLY = 5;
}  // namespace AmuletShape

bool is_valid_entity(const entt::registry& registry, entt::entity entity) {
    return entity != entt::null && registry.valid(entity);
}

mir2::common::Position resolve_center_position(entt::registry& registry,
                                          entt::entity caster,
                                          entt::entity target,
                                          const mir2::common::Position* target_pos,
                                          bool* has_position) {
    if (has_position) {
        *has_position = true;
    }

    if (target_pos) {
        return *target_pos;
    }

    if (is_valid_entity(registry, target)) {
        if (const auto* state = registry.try_get<CharacterStateComponent>(target)) {
            return state->position;
        }
    }

    if (is_valid_entity(registry, caster)) {
        if (const auto* state = registry.try_get<CharacterStateComponent>(caster)) {
            return state->position;
        }
    }

    if (has_position) {
        *has_position = false;
    }
    return {};
}

std::vector<entt::entity> collect_targets(entt::registry& registry,
                                         entt::entity caster,
                                         entt::entity target,
                                         const mir2::common::Position* target_pos,
                                         const SkillTemplate& skill) {
    std::vector<entt::entity> targets;

    if (!registry.valid(caster)) {
        return targets;
    }

    const auto target_type = skill.target_type;
    if (target_type == mir2::common::SkillTarget::SELF) {
        targets.reserve(1);
        targets.push_back(caster);
        return targets;
    }

    const bool is_single = target_type == mir2::common::SkillTarget::SINGLE_ENEMY ||
                           target_type == mir2::common::SkillTarget::SINGLE_ALLY;
    const bool is_aoe = target_type == mir2::common::SkillTarget::AOE_ENEMY ||
                        target_type == mir2::common::SkillTarget::AOE_ALLY ||
                        target_type == mir2::common::SkillTarget::AOE_ALL;

    if (is_single || skill.aoe_radius <= 0.0f) {
        targets.reserve(1);
        if (is_valid_entity(registry, target)) {
            targets.push_back(target);
        }
        return targets;
    }

    if (!is_aoe) {
        targets.reserve(1);
        if (is_valid_entity(registry, target)) {
            targets.push_back(target);
        }
        return targets;
    }

    targets.reserve(16);
    bool has_center = false;
    const mir2::common::Position center = resolve_center_position(
        registry, caster, target, target_pos, &has_center);
    if (!has_center) {
        return targets;
    }

    const auto* caster_state = registry.try_get<CharacterStateComponent>(caster);
    const float radius = std::max(0.0f, skill.aoe_radius);
    SpatialQuery spatial_query(registry);
    const auto candidates = spatial_query.get_entities_in_radius(center, radius);
    for (auto entity : candidates) {
        const auto* state = registry.try_get<CharacterStateComponent>(entity);
        if (!state) {
            continue;
        }
        if (caster_state && state->map_id != caster_state->map_id) {
            continue;
        }
        if (!registry.all_of<CharacterAttributesComponent>(entity)) {
            continue;
        }
        targets.push_back(entity);
    }

    return targets;
}

int clamp_int(int value, int min_value, int max_value) {
    return std::min(std::max(value, min_value), max_value);
}

entt::entity get_equipped_item_entity(const entt::registry& registry,
                                      entt::entity owner,
                                      mir2::common::EquipSlot slot) {
    if (!registry.valid(owner)) {
        return entt::null;
    }

    const auto* equipment = registry.try_get<EquipmentSlotComponent>(owner);
    if (!equipment) {
        return entt::null;
    }

    const std::size_t index = static_cast<std::size_t>(slot);
    if (index >= equipment->slots.size()) {
        return entt::null;
    }

    const entt::entity item_entity = equipment->slots[index];
    if (item_entity == entt::null || !registry.valid(item_entity)) {
        return entt::null;
    }

    return item_entity;
}

const ItemComponent* get_equipped_item(const entt::registry& registry,
                                       entt::entity owner,
                                       mir2::common::EquipSlot slot) {
    const entt::entity item_entity = get_equipped_item_entity(registry, owner, slot);
    if (item_entity == entt::null) {
        return nullptr;
    }
    return registry.try_get<ItemComponent>(item_entity);
}

mir2::common::AmuletType resolve_amulet_type(const ItemComponent& item) {
    if (item.shape <= 0) {
        return mir2::common::AmuletType::NONE;
    }
    if (item.shape == AmuletShape::HOLY) {
        return mir2::common::AmuletType::HOLY;
    }
    if (item.shape >= AmuletShape::POISON_MIN &&
        item.shape <= AmuletShape::POISON_MAX) {
        return mir2::common::AmuletType::POISON;
    }
    if (item.shape == AmuletShape::FIRE) {
        return mir2::common::AmuletType::FIRE;
    }
    if (item.shape == AmuletShape::ICE) {
        return mir2::common::AmuletType::ICE;
    }
    return mir2::common::AmuletType::NONE;
}

}  // namespace

SkillSystem::SkillSystem(entt::registry& registry)
    : registry_(registry) {}

void SkillSystem::set_event_bus(EventBus* event_bus) {
    event_bus_ = event_bus;
}

void SkillSystem::set_effect_broadcaster(EffectBroadcaster* broadcaster) {
    effect_broadcaster_ = broadcaster;
}

mir2::common::ErrorCode SkillSystem::learn_skill(entt::entity entity, uint32_t skill_id) {
    if (!registry_.valid(entity)) {
        return mir2::common::ErrorCode::INVALID_ACTION;
    }

    const SkillTemplate* skill = SkillRegistry::instance().get_skill(skill_id);
    if (!skill) {
        return mir2::common::ErrorCode::SKILL_NOT_LEARNED;
    }

    auto* identity = registry_.try_get<CharacterIdentityComponent>(entity);
    auto* attributes = registry_.try_get<CharacterAttributesComponent>(entity);
    if (!identity || !attributes) {
        return mir2::common::ErrorCode::INVALID_ACTION;
    }

    if (identity->char_class != skill->required_class) {
        return mir2::common::ErrorCode::CLASS_REQUIREMENT_NOT_MET;
    }
    if (attributes->level < skill->required_level) {
        return mir2::common::ErrorCode::LEVEL_REQUIREMENT_NOT_MET;
    }

    auto& skill_list = registry_.get_or_emplace<SkillListComponent>(entity);
    if (skill_list.has_skill(skill_id)) {
        return mir2::common::ErrorCode::INVALID_ACTION;
    }

    if (!skill_list.add_skill(skill_id)) {
        return mir2::common::ErrorCode::INVALID_ACTION;
    }

    dirty_tracker::mark_skills_dirty(registry_, entity);
    if (event_bus_) {
        events::SkillLearnedEvent event;
        event.entity = entity;
        event.skill_id = skill_id;
        event_bus_->Publish(event);
    }
    return mir2::common::ErrorCode::SUCCESS;
}

bool SkillSystem::can_learn_skill(entt::entity entity, uint32_t skill_id) const {
    if (!registry_.valid(entity)) {
        return false;
    }

    const SkillTemplate* skill = SkillRegistry::instance().get_skill(skill_id);
    if (!skill) {
        return false;
    }

    const auto* identity = registry_.try_get<CharacterIdentityComponent>(entity);
    const auto* attributes = registry_.try_get<CharacterAttributesComponent>(entity);
    if (!identity || !attributes) {
        return false;
    }

    if (identity->char_class != skill->required_class) {
        return false;
    }
    if (attributes->level < skill->required_level) {
        return false;
    }

    const auto* skill_list = registry_.try_get<SkillListComponent>(entity);
    if (skill_list) {
        if (skill_list->has_skill(skill_id)) {
            return false;
        }
        if (skill_list->count >= SkillListComponent::MAX_SKILLS) {
            return false;
        }
    }

    return true;
}

SkillCastResult SkillSystem::cast_skill(entt::entity caster,
                                        uint32_t skill_id,
                                        entt::entity target,
                                        const mir2::common::Position* target_pos) {
    SkillCastResult result;

    if (!registry_.valid(caster)) {
        return SkillCastResult::error(mir2::common::ErrorCode::INVALID_ACTION);
    }

    const SkillTemplate* skill = SkillRegistry::instance().get_skill(skill_id);
    if (!skill) {
        return SkillCastResult::error(mir2::common::ErrorCode::SKILL_NOT_LEARNED);
    }

    auto* skill_list = registry_.try_get<SkillListComponent>(caster);
    if (!skill_list || !skill_list->has_skill(skill_id)) {
        return SkillCastResult::error(mir2::common::ErrorCode::SKILL_NOT_LEARNED);
    }

    const auto* learned = skill_list->get_skill(skill_id);
    const int skill_level = learned ? static_cast<int>(learned->level) : 0;

    const mir2::common::ErrorCode validation = validate_cast(caster, *skill);
    if (validation != mir2::common::ErrorCode::SUCCESS) {
        return SkillCastResult::error(validation);
    }

    entt::entity resolved_target = target;
    if (skill->target_type == mir2::common::SkillTarget::SELF) {
        resolved_target = caster;
    }

    const bool requires_target = skill->target_type == mir2::common::SkillTarget::SINGLE_ENEMY ||
                                 skill->target_type == mir2::common::SkillTarget::SINGLE_ALLY;

    if (requires_target && !is_valid_entity(registry_, resolved_target)) {
        return SkillCastResult::error(mir2::common::ErrorCode::TARGET_NOT_FOUND);
    }

    if (resolved_target != entt::null && !registry_.valid(resolved_target)) {
        return SkillCastResult::error(mir2::common::ErrorCode::TARGET_NOT_FOUND);
    }

    auto* caster_attributes = registry_.try_get<CharacterAttributesComponent>(caster);
    if (!caster_attributes) {
        return SkillCastResult::error(mir2::common::ErrorCode::INVALID_ACTION);
    }

    if (skill->mp_cost > 0) {
        caster_attributes->mp = clamp_int(caster_attributes->mp - skill->mp_cost, 0,
                                          caster_attributes->max_mp);
        dirty_tracker::mark_attributes_dirty(registry_, caster);
        result.mp_consumed = skill->mp_cost;
    }

    if (skill->required_amulet != mir2::common::AmuletType::NONE && skill->amulet_cost > 0) {
        const entt::entity amulet_entity = get_equipped_item_entity(
            registry_, caster, mir2::common::EquipSlot::AMULET);
        if (amulet_entity != entt::null) {
            if (auto* amulet = registry_.try_get<ItemComponent>(amulet_entity)) {
                amulet->durability = std::max(0, amulet->durability - skill->amulet_cost);
                dirty_tracker::mark_items_dirty(registry_, caster);
                dirty_tracker::mark_equipment_dirty(registry_, caster);
            }
        }
    }

    if (skill->cooldown_ms > 0) {
        auto& cooldowns = registry_.get_or_emplace<SkillCooldownComponent>(caster);
        cooldowns.start_cooldown(skill_id, skill->cooldown_ms, current_time_ms_);
    }

    if (skill->cast_time_ms > 0) {
        mir2::common::Position cast_pos{};
        bool has_center = false;
        cast_pos = resolve_center_position(registry_, caster, resolved_target, target_pos, &has_center);
        if (!has_center) {
            cast_pos = {};
        }

        auto& casting = registry_.get_or_emplace<CastingComponent>(caster);
        casting.start_cast(skill_id, resolved_target,
                           cast_pos, current_time_ms_, skill->cast_time_ms);

        if (effect_broadcaster_) {
            effect_broadcaster_->broadcast_cast_effect(caster, *skill);
        }

        result.success = true;
        if (event_bus_) {
            events::SkillCastEvent event;
            event.caster = caster;
            event.skill_id = skill_id;
            event.result = result;
            event_bus_->Publish(event);
        }
        return result;
    }

    std::vector<entt::entity> targets = collect_targets(
        registry_, caster, resolved_target, target_pos, *skill);

    if (targets.empty()) {
        return SkillCastResult::error(mir2::common::ErrorCode::TARGET_NOT_FOUND);
    }

    result.success = true;

    if (effect_broadcaster_) {
        effect_broadcaster_->broadcast_cast_effect(caster, *skill);
    }

    for (auto entity : targets) {
        auto* target_attributes = registry_.try_get<CharacterAttributesComponent>(entity);
        int before_hp = target_attributes ? target_attributes->hp : 0;

        apply_skill_effect(caster, entity, *skill, skill_level);

        TargetResult tr;
        tr.target = static_cast<uint32_t>(entity);

        if (target_attributes) {
            int after_hp = target_attributes->hp;
            if (skill->skill_type == mir2::common::SkillType::HEAL) {
                tr.healing_done = std::max(0, after_hp - before_hp);
            } else if (skill->skill_type == mir2::common::SkillType::PHYSICAL ||
                       skill->skill_type == mir2::common::SkillType::MAGICAL) {
                tr.damage_dealt = std::max(0, before_hp - after_hp);
            }
            tr.target_died = before_hp > 0 && after_hp <= 0;
        }

        if (!result.applied_effect &&
            (skill->skill_type == mir2::common::SkillType::BUFF ||
             skill->skill_type == mir2::common::SkillType::DEBUFF ||
             skill->dot_damage != 0)) {
            auto* effects = registry_.try_get<EffectListComponent>(entity);
            if (effects && !effects->effects.empty()) {
                result.applied_effect = effects->effects.back();
            }
        }

        result.targets.push_back(tr);
    }

    if (event_bus_) {
        events::SkillCastEvent event;
        event.caster = caster;
        event.skill_id = skill_id;
        event.result = result;
        event_bus_->Publish(event);
    }
    return result;
}

bool SkillSystem::is_skill_ready(entt::entity entity, uint32_t skill_id) const {
    auto* cooldowns = registry_.try_get<SkillCooldownComponent>(entity);
    if (!cooldowns) {
        return true;
    }

    auto it = cooldowns->cooldowns.find(skill_id);
    if (it == cooldowns->cooldowns.end()) {
        return true;
    }
    if (current_time_ms_ >= it->second) {
        cooldowns->cooldowns.erase(it);
        return true;
    }
    return false;
}

void SkillSystem::add_training_points(entt::entity entity, uint32_t skill_id, int points) {
    if (!registry_.valid(entity) || points == 0) {
        return;
    }

    const SkillTemplate* skill = SkillRegistry::instance().get_skill(skill_id);
    if (!skill) {
        return;
    }

    auto* skill_list = registry_.try_get<SkillListComponent>(entity);
    if (!skill_list) {
        return;
    }

    auto* learned = skill_list->get_skill(skill_id);
    if (!learned) {
        return;
    }

    const uint8_t old_level = learned->level;
    learned->train_points = std::max(0, learned->train_points + points);

    const auto* attributes = registry_.try_get<CharacterAttributesComponent>(entity);
    const int char_level = attributes ? attributes->level : 0;

    int max_level = static_cast<int>(skill->max_level);
    max_level = std::min(max_level, static_cast<int>(skill->train_points_req.size()) - 1);

    while (static_cast<int>(learned->level) < max_level) {
        const int next_level = static_cast<int>(learned->level) + 1;
        const int required_points = skill->train_points_req[static_cast<std::size_t>(next_level)];
        const int required_level = skill->train_level_req[static_cast<std::size_t>(next_level)];
        if (learned->train_points < required_points || char_level < required_level) {
            break;
        }
        learned->level = static_cast<uint8_t>(next_level);
    }

    dirty_tracker::mark_skills_dirty(registry_, entity);

    if (event_bus_ && learned->level != old_level) {
        events::SkillLevelUpEvent event;
        event.entity = entity;
        event.skill_id = skill_id;
        event.new_level = learned->level;
        event_bus_->Publish(event);
    }
}

void SkillSystem::update(int64_t current_time_ms) {
    current_time_ms_ = current_time_ms;

    auto view = registry_.view<CastingComponent>();
    for (auto entity : view) {
        auto& casting = view.get<CastingComponent>(entity);
        if (!casting.is_casting) {
            continue;
        }
        if (!casting.is_complete(current_time_ms_)) {
            continue;
        }

        const SkillTemplate* skill = SkillRegistry::instance().get_skill(casting.skill_id);
        if (!skill) {
            casting.cancel();
            continue;
        }

        int skill_level = 0;
        if (auto* skill_list = registry_.try_get<SkillListComponent>(entity)) {
            if (const auto* learned = skill_list->get_skill(casting.skill_id)) {
                skill_level = static_cast<int>(learned->level);
            }
        }

        entt::entity target = casting.target_entity;
        if (!is_valid_entity(registry_, target)) {
            target = entt::null;
        }

        std::vector<entt::entity> targets = collect_targets(
            registry_, entity, target, &casting.target_pos, *skill);

        for (auto target_entity : targets) {
            apply_skill_effect(entity, target_entity, *skill, skill_level);
        }

        casting.cancel();
    }
}

mir2::common::ErrorCode SkillSystem::validate_cast(entt::entity caster,
                                              const SkillTemplate& skill) const {
    if (!registry_.valid(caster)) {
        return mir2::common::ErrorCode::INVALID_ACTION;
    }

    const auto* attributes = registry_.try_get<CharacterAttributesComponent>(caster);
    if (!attributes) {
        return mir2::common::ErrorCode::INVALID_ACTION;
    }

    if (attributes->hp <= 0) {
        return mir2::common::ErrorCode::CHARACTER_DEAD;
    }

    if (skill.mp_cost > 0 && attributes->mp < skill.mp_cost) {
        return mir2::common::ErrorCode::INSUFFICIENT_MP;
    }

    if (skill.required_amulet != mir2::common::AmuletType::NONE) {
        const ItemComponent* amulet = get_equipped_item(registry_, caster,
                                                        mir2::common::EquipSlot::AMULET);
        if (!amulet) {
            return mir2::common::ErrorCode::ITEM_NOT_FOUND;
        }
        if (resolve_amulet_type(*amulet) != skill.required_amulet) {
            return mir2::common::ErrorCode::ITEM_NOT_FOUND;
        }
        if (skill.amulet_cost > 0 && amulet->durability < skill.amulet_cost) {
            return mir2::common::ErrorCode::ITEM_NOT_FOUND;
        }
    }

    if (!is_skill_ready(caster, skill.id)) {
        return mir2::common::ErrorCode::SKILL_ON_COOLDOWN;
    }

    const auto* casting = registry_.try_get<CastingComponent>(caster);
    if (casting && casting->is_casting) {
        return mir2::common::ErrorCode::INVALID_ACTION;
    }

    return mir2::common::ErrorCode::SUCCESS;
}

void SkillSystem::apply_skill_effect(entt::entity caster,
                                     entt::entity target,
                                     const SkillTemplate& skill,
                                     int skill_level) {
    if (!registry_.valid(caster) || !registry_.valid(target)) {
        return;
    }

    auto* caster_attributes = registry_.try_get<CharacterAttributesComponent>(caster);
    auto* target_attributes = registry_.try_get<CharacterAttributesComponent>(target);
    if (!caster_attributes || !target_attributes) {
        return;
    }

    switch (skill.skill_type) {
        case mir2::common::SkillType::PHYSICAL: {
            int damage = DamageCalculator::calculate_physical_damage(
                *caster_attributes, *target_attributes, skill, skill_level);
            apply_damage(caster, target, skill.id, damage);
            break;
        }
        case mir2::common::SkillType::MAGICAL: {
            const bool is_undead = target_attributes->life_attrib != 0;
            int damage = DamageCalculator::calculate_magic_damage(
                *caster_attributes, *target_attributes, skill, skill_level, is_undead);
            apply_damage(caster, target, skill.id, damage);
            break;
        }
        case mir2::common::SkillType::HEAL: {
            int healing = DamageCalculator::calculate_healing(
                *caster_attributes, skill, skill_level);
            apply_healing(caster, target, skill.id, healing);
            break;
        }
        case mir2::common::SkillType::BUFF:
        case mir2::common::SkillType::DEBUFF: {
            ActiveEffect effect;
            effect.skill_id = skill.id;
            effect.source_entity = static_cast<uint32_t>(caster);
            effect.category = (skill.skill_type == mir2::common::SkillType::BUFF)
                ? EffectCategory::STAT_BUFF
                : EffectCategory::STAT_DEBUFF;
            effect.value = skill.stat_modifier;
            effect.start_time_ms = current_time_ms_;
            if (skill.duration_ms > 0) {
                effect.end_time_ms = current_time_ms_ + skill.duration_ms;
            }
            effect.last_tick_ms = current_time_ms_;
            effect.tick_interval_ms = std::max(1, skill.dot_interval_ms);
            effect.shield_remaining = 0;

            auto& effects = registry_.get_or_emplace<EffectListComponent>(target);
            effects.add_effect(effect);
            break;
        }
        default:
            break;
    }

    if (skill.dot_damage != 0 && skill.duration_ms > 0) {
        ActiveEffect dot_effect;
        dot_effect.skill_id = skill.id;
        dot_effect.source_entity = static_cast<uint32_t>(caster);
        dot_effect.category = skill.dot_damage > 0
            ? EffectCategory::DAMAGE_OVER_TIME
            : EffectCategory::HEAL_OVER_TIME;
        dot_effect.value = skill.dot_damage;
        dot_effect.start_time_ms = current_time_ms_;
        dot_effect.end_time_ms = current_time_ms_ + skill.duration_ms;
        dot_effect.last_tick_ms = current_time_ms_;
        dot_effect.tick_interval_ms = std::max(1, skill.dot_interval_ms);

        auto& effects = registry_.get_or_emplace<EffectListComponent>(target);
        effects.add_effect(dot_effect);
    }

    if (effect_broadcaster_) {
        effect_broadcaster_->broadcast_hit_effect(caster, target, skill);
    }
}

void SkillSystem::apply_damage(entt::entity caster, entt::entity target, uint32_t skill_id,
                               int damage) {
    auto* attributes = registry_.try_get<CharacterAttributesComponent>(target);
    const int before_hp = attributes ? attributes->hp : 0;

    const int actual_damage = CombatSystem::TakeDamage(registry_, target, damage);
    if (actual_damage <= 0) {
        return;
    }

    if (event_bus_) {
        events::DamageDealtEvent event;
        event.source = caster;
        event.target = target;
        event.skill_id = skill_id;
        event.damage = actual_damage;
        event.is_critical = false;
        event_bus_->Publish(event);
    }

    if (attributes && before_hp > 0 && attributes->hp <= 0) {
        if (event_bus_) {
            events::EntityDeathEvent event;
            event.entity = target;
            event.killer = caster;
            event.killing_skill_id = skill_id;
            event_bus_->Publish(event);
        }
    }
}

void SkillSystem::apply_healing(entt::entity caster, entt::entity target, uint32_t skill_id,
                                int healing) {
    auto* attributes = registry_.try_get<CharacterAttributesComponent>(target);
    if (!attributes) {
        return;
    }

    if (healing <= 0 || attributes->hp <= 0) {
        return;
    }

    const int before_hp = attributes->hp;
    attributes->hp = std::min(attributes->max_hp, attributes->hp + healing);
    if (attributes->hp != before_hp) {
        dirty_tracker::mark_attributes_dirty(registry_, target);
        if (event_bus_) {
            events::HealingDoneEvent event;
            event.source = caster;
            event.target = target;
            event.skill_id = skill_id;
            event.healing = attributes->hp - before_hp;
            event_bus_->Publish(event);
        }
    }
}

bool SkillSystem::interrupt_casting(entt::entity entity) {
    if (!registry_.valid(entity)) {
        return false;
    }

    auto* casting = registry_.try_get<CastingComponent>(entity);
    if (!casting || !casting->is_casting) {
        return false;
    }

    if (!casting->can_be_interrupted) {
        return false;
    }

    casting->cancel();
    return true;
}

} // namespace mir2::ecs
