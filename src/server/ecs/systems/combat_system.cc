#include "ecs/systems/combat_system.h"

#include "ecs/components/effect_component.h"
#include "ecs/components/equipment_component.h"
#include "ecs/components/item_component.h"
#include "ecs/dirty_tracker.h"
#include "ecs/event_bus.h"
#include "ecs/events/combat_events.h"
#include "ecs/systems/equipment_bonus_system.h"
#include "ecs/systems/passive_skill_system.h"
#include "ecs/systems/spatial_query.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace mir2::ecs {

namespace {

legend2::combat::CombatRandom& get_combat_random() {
    static thread_local legend2::combat::CombatRandom random;
    return random;
}

mir2::common::Position to_position(const CharacterStateComponent& state) {
    return state.position;
}

int get_attack_range(const CombatComponent* combat, const legend2::CombatConfig& config) {
    const int range = combat ? combat->attack_range : config.default_melee_range;
    return std::max(0, range);
}

legend2::combat::DamageInput build_damage_input(const CharacterAttributesComponent& attacker,
                                                const CharacterAttributesComponent& defender,
                                                const CombatComponent* attacker_combat,
                                                const CombatComponent* defender_combat,
                                                const legend2::CombatConfig& config,
                                                const EquipmentBonus* attacker_equip = nullptr,
                                                const EquipmentBonus* defender_equip = nullptr,
                                                const AttributeModifiers* attacker_passive = nullptr) {
    legend2::combat::DamageInput input;

    // 基础攻击力 + 装备加成 + 被动技能加成
    input.attack = attacker.attack;
    if (attacker_equip) {
        input.attack += attacker_equip->attack_bonus;
    }
    if (attacker_passive) {
        input.attack += attacker_passive->attack_bonus;
    }

    // 基础防御力 + 装备加成
    input.defense = defender.defense;
    if (defender_equip) {
        input.defense += defender_equip->defense_bonus;
    }

    input.critical_chance = config.base_critical_chance +
        (attacker_combat ? attacker_combat->critical_chance : 0.0f);
    if (attacker_passive) {
        input.critical_chance += attacker_passive->critical_bonus;
    }

    input.miss_chance = config.base_miss_chance +
        (defender_combat ? defender_combat->evasion_chance : 0.0f);
    if (attacker_passive) {
        // 命中率加成降低闪避率
        input.miss_chance -= attacker_passive->hit_rate_bonus * 0.01f;
        input.miss_chance = std::max(0.0f, input.miss_chance);
    }
    return input;
}

bool is_valid_entity(const entt::registry& registry, entt::entity entity) {
    return entity != entt::null && registry.valid(entity);
}

int get_effective_attack_range(const CombatComponent* combat,
                               const legend2::CombatConfig& config,
                               const legend2::combat::AttackTypeModifier& modifier) {
    const int base_range = get_attack_range(combat, config);
    return std::max(base_range, modifier.range);
}

std::vector<entt::entity> collect_wide_hit_targets(entt::registry& registry,
                                                   entt::entity attacker,
                                                   const mir2::common::Position& center,
                                                   int aoe_radius) {
    std::vector<entt::entity> targets;
    if (!registry.valid(attacker)) {
        return targets;
    }

    const auto* attacker_state = registry.try_get<CharacterStateComponent>(attacker);
    const int radius = std::max(0, aoe_radius);
    constexpr float kSqrt2 = 1.41421356237f;
    const float query_radius = static_cast<float>(radius) * kSqrt2;

    SpatialQuery spatial_query(registry);
    const auto candidates = spatial_query.get_entities_in_radius(center, query_radius);
    for (auto entity : candidates) {
        if (entity == attacker) {
            continue;
        }

        const auto* state = registry.try_get<CharacterStateComponent>(entity);
        const auto* attributes = registry.try_get<CharacterAttributesComponent>(entity);
        if (!state || !attributes) {
            continue;
        }

        if (attributes->hp <= 0) {
            continue;
        }

        if (attacker_state && state->map_id != attacker_state->map_id) {
            continue;
        }

        const int dx = std::abs(state->position.x - center.x);
        const int dy = std::abs(state->position.y - center.y);
        if (dx <= radius && dy <= radius) {
            targets.push_back(entity);
        }
    }

    return targets;
}

// 检查玩家是否装备了特定Shape的戒指
bool has_ring_with_shape(const entt::registry& registry, entt::entity entity, int shape) {
    const auto* equipment = registry.try_get<EquipmentSlotComponent>(entity);
    if (!equipment) {
        return false;
    }

    // 检查左右戒指槽
    const auto check_ring = [&](entt::entity ring_entity) {
        if (!is_valid_entity(registry, ring_entity)) {
            return false;
        }
        const auto* item = registry.try_get<ItemComponent>(ring_entity);
        return item && item->shape == shape;
    };

    const entt::entity left_ring = equipment->slots[static_cast<size_t>(mir2::common::EquipSlot::RING_LEFT)];
    const entt::entity right_ring = equipment->slots[static_cast<size_t>(mir2::common::EquipSlot::RING_RIGHT)];

    return check_ring(left_ring) || check_ring(right_ring);
}

}  // namespace

CombatSystem::CombatSystem()
    : System(SystemPriority::kCombat),
      combat_group_{} {}

void CombatSystem::Update(entt::registry& registry, float delta_time) {
    if (!combat_group_) {
        // 使用 group 替代 view：owned 组件在内存中连续存储，适合战斗热路径遍历。
        // 单线程设计下缓存 group 安全（参见 THREADING.md）。
        combat_group_ = registry.group<CombatComponent>(
            entt::get<CharacterAttributesComponent, CharacterStateComponent>);
    }

    if (delta_time <= 0.0f) {
        return;
    }

    combat_group_.each([&](entt::entity entity,
                           CombatComponent& combat,
                           CharacterAttributesComponent& attributes,
                           CharacterStateComponent& state) {
        (void)combat;
        (void)state;

        if (attributes.hp <= 0) {
            return;
        }

        // 示例：持续伤害/回复的逻辑框架（DoT/HoT）。
        // TODO: 引入 PeriodicEffectComponent 或 Buff 系统，提供每实体的数值与累计器。
    });
}

int CombatSystem::TakeDamage(entt::registry& registry, entt::entity entity, int damage,
                             EventBus* event_bus) {
    auto* attributes = registry.try_get<CharacterAttributesComponent>(entity);
    if (!attributes) {
        return 0;
    }

    // 无效伤害或已死亡时不处理
    if (damage <= 0 || attributes->hp <= 0) {
        return 0;
    }

    // 确保至少造成1点伤害
    int actual_damage = std::max(1, damage);
    // HP不能低于0
    attributes->hp = std::max(0, attributes->hp - actual_damage);
    dirty_tracker::mark_attributes_dirty(registry, entity);

    return actual_damage;
}

legend2::DamageResult CombatSystem::TakeDamageWithCalc(entt::registry& registry,
                                                       entt::entity attacker,
                                                       entt::entity target,
                                                       const legend2::CombatConfig& config,
                                                       EventBus* event_bus) {
    auto* attacker_attributes = registry.try_get<CharacterAttributesComponent>(attacker);
    auto* target_attributes = registry.try_get<CharacterAttributesComponent>(target);
    if (!attacker_attributes || !target_attributes) {
        return legend2::DamageResult::miss();
    }

    if (target_attributes->hp <= 0) {
        return legend2::DamageResult::miss();
    }

    auto* attacker_combat = registry.try_get<CombatComponent>(attacker);
    auto* target_combat = registry.try_get<CombatComponent>(target);

    // 获取攻击者的被动技能加成
    PassiveSkillSystem passive_system(registry);
    const auto attacker_passive = passive_system.trigger_on_attack(attacker);

    const auto input = build_damage_input(*attacker_attributes, *target_attributes,
                                          attacker_combat, target_combat, config,
                                          nullptr, nullptr, &attacker_passive);
    const int raw_damage = input.attack - input.defense;

    auto& random = get_combat_random();
    const auto rolls = random.roll_damage(raw_damage, config);
    auto result = legend2::combat::DamageCalculator::calculate(input, config, rolls);

    if (!result.is_miss) {
        TakeDamage(registry, target, result.final_damage, event_bus);

        // 麻痹戒指效果(Shape:113) - 10%概率定身目标
        if (has_ring_with_shape(registry, attacker, 113)) {
            if (random.roll_chance() < 0.1f) {
                auto& effects = registry.get_or_emplace<EffectListComponent>(target);
                ActiveEffect stun_effect;
                stun_effect.category = EffectCategory::STUN;
                stun_effect.source_entity = static_cast<uint32_t>(attacker);
                stun_effect.start_time_ms = 0; // 需要从外部传入时间
                stun_effect.end_time_ms = 3000; // 3秒定身
                effects.add_effect(stun_effect);
            }
        }

        // 发布伤害事件
        if (event_bus) {
            events::DamageDealtEvent event;
            event.attacker = attacker;
            event.target = target;
            event.damage = result.final_damage;
            event.is_critical = result.is_critical;
            event.is_miss = false;
            event_bus->Publish(event);
        }
    }

    return result;
}

int CombatSystem::Heal(entt::registry& registry, entt::entity entity, int amount) {
    auto* attributes = registry.try_get<CharacterAttributesComponent>(entity);
    if (!attributes) {
        return 0;
    }

    // 无效治疗量或已死亡时不处理
    if (amount <= 0 || attributes->hp <= 0) {
        return 0;
    }

    int old_hp = attributes->hp;
    // HP不能超过最大值
    attributes->hp = std::min(attributes->max_hp, attributes->hp + amount);
    if (attributes->hp != old_hp) {
        dirty_tracker::mark_attributes_dirty(registry, entity);
    }

    return attributes->hp - old_hp;
}

int CombatSystem::RestoreMP(entt::registry& registry, entt::entity entity, int amount) {
    auto* attributes = registry.try_get<CharacterAttributesComponent>(entity);
    if (!attributes) {
        return 0;
    }

    if (amount <= 0) {
        return 0;
    }

    int old_mp = attributes->mp;
    // MP不能超过最大值
    attributes->mp = std::min(attributes->max_mp, attributes->mp + amount);
    if (attributes->mp != old_mp) {
        dirty_tracker::mark_attributes_dirty(registry, entity);
    }

    return attributes->mp - old_mp;
}

bool CombatSystem::ConsumeMP(entt::registry& registry, entt::entity entity, int amount) {
    auto* attributes = registry.try_get<CharacterAttributesComponent>(entity);
    if (!attributes) {
        return false;
    }

    // 消耗量为0或负数时直接成功
    if (amount <= 0) {
        return true;
    }

    // 检查MP是否足够
    if (attributes->mp < amount) {
        return false;
    }

    attributes->mp -= amount;
    dirty_tracker::mark_attributes_dirty(registry, entity);
    return true;
}

void CombatSystem::Die(entt::registry& registry, entt::entity entity, EventBus* event_bus) {
    auto* attributes = registry.try_get<CharacterAttributesComponent>(entity);
    if (!attributes) {
        return;
    }

    if (attributes->hp != 0) {
        // 复活戒指效果(Shape:114) - 死亡时自动复活
        if (has_ring_with_shape(registry, entity, 114)) {
            // 恢复30%的HP，避免死亡
            const int revive_hp = static_cast<int>(attributes->max_hp * 0.3);
            attributes->hp = revive_hp;
            dirty_tracker::mark_attributes_dirty(registry, entity);

            // TODO: 消耗复活戒指的耐久度
            return; // 复活成功，不执行死亡逻辑
        }

        attributes->hp = 0;
        dirty_tracker::mark_attributes_dirty(registry, entity);

        // 发布死亡事件
        if (event_bus) {
            events::EntityDeathEvent event;
            event.entity = entity;
            event_bus->Publish(event);
        }
    }
}

void CombatSystem::Respawn(entt::registry& registry, entt::entity entity,
                           const mir2::common::Position& pos, float hp_percent,
                           float mp_percent, EventBus* event_bus) {
    auto& state = registry.get_or_emplace<CharacterStateComponent>(entity);
    state.position = pos;

    auto* attributes = registry.try_get<CharacterAttributesComponent>(entity);
    if (!attributes) {
        return;
    }

    // 根据百分比恢复HP和MP，确保百分比在有效范围内
    attributes->hp = static_cast<int>(
        attributes->max_hp * std::clamp(hp_percent, 0.0f, 1.0f));
    attributes->mp = static_cast<int>(
        attributes->max_mp * std::clamp(mp_percent, 0.0f, 1.0f));

    // 确保复活后至少有1点HP
    if (attributes->hp <= 0) {
        attributes->hp = 1;
    }

    dirty_tracker::mark_attributes_dirty(registry, entity);

    // 发布复活事件
    if (event_bus) {
        events::EntityRespawnEvent event;
        event.entity = entity;
        event.position = pos;
        event.hp_percent = hp_percent;
        event.mp_percent = mp_percent;
        event_bus->Publish(event);
    }
}

legend2::AttackResult CombatSystem::ExecuteAttack(entt::registry& registry,
                                                  entt::entity attacker,
                                                  entt::entity target,
                                                  const legend2::CombatConfig& config,
                                                  EventBus* event_bus) {
    auto* attacker_attributes = registry.try_get<CharacterAttributesComponent>(attacker);
    auto* target_attributes = registry.try_get<CharacterAttributesComponent>(target);
    auto* attacker_state = registry.try_get<CharacterStateComponent>(attacker);
    auto* target_state = registry.try_get<CharacterStateComponent>(target);

    if (!attacker_attributes || !attacker_state) {
        return legend2::AttackResult::error(mir2::common::ErrorCode::CHARACTER_NOT_FOUND);
    }
    if (!target_attributes || !target_state) {
        return legend2::AttackResult::error(mir2::common::ErrorCode::TARGET_NOT_FOUND);
    }

    if (attacker_attributes->hp <= 0) {
        return legend2::AttackResult::error(mir2::common::ErrorCode::CHARACTER_DEAD);
    }
    if (target_attributes->hp <= 0) {
        return legend2::AttackResult::error(mir2::common::ErrorCode::TARGET_NOT_FOUND);
    }

    const auto* attacker_combat = registry.try_get<CombatComponent>(attacker);
    const int attack_range = get_attack_range(attacker_combat, config);
    const mir2::common::Position attacker_pos = to_position(*attacker_state);
    const mir2::common::Position target_pos = to_position(*target_state);

    if (!legend2::combat::RangeChecker::is_in_range(attacker_pos, target_pos, attack_range)) {
        return legend2::AttackResult::error(mir2::common::ErrorCode::TARGET_OUT_OF_RANGE);
    }

    auto damage = TakeDamageWithCalc(registry, attacker, target, config, event_bus);
    const bool target_died = target_attributes->hp <= 0;
    return legend2::AttackResult::ok(damage, target_died);
}

legend2::AttackResult CombatSystem::ProcessAttackWithType(entt::registry& registry,
                                                          entt::entity attacker,
                                                          entt::entity target,
                                                          const legend2::CombatConfig& config,
                                                          mir2::common::AttackType attack_type,
                                                          EventBus* event_bus) {
    auto* attacker_attributes = registry.try_get<CharacterAttributesComponent>(attacker);
    auto* target_attributes = registry.try_get<CharacterAttributesComponent>(target);
    auto* attacker_state = registry.try_get<CharacterStateComponent>(attacker);
    auto* target_state = registry.try_get<CharacterStateComponent>(target);

    if (!attacker_attributes || !attacker_state) {
        return legend2::AttackResult::error(mir2::common::ErrorCode::CHARACTER_NOT_FOUND);
    }
    if (!target_attributes || !target_state) {
        return legend2::AttackResult::error(mir2::common::ErrorCode::TARGET_NOT_FOUND);
    }

    if (attacker_attributes->hp <= 0) {
        return legend2::AttackResult::error(mir2::common::ErrorCode::CHARACTER_DEAD);
    }
    if (target_attributes->hp <= 0) {
        return legend2::AttackResult::error(mir2::common::ErrorCode::TARGET_NOT_FOUND);
    }

    const auto* attacker_combat = registry.try_get<CombatComponent>(attacker);
    const legend2::combat::AttackTypeModifier modifier =
        legend2::combat::get_attack_modifier(attack_type);
    const int attack_range = get_effective_attack_range(attacker_combat, config, modifier);
    const mir2::common::Position attacker_pos = to_position(*attacker_state);
    const mir2::common::Position target_pos = to_position(*target_state);

    if (!legend2::combat::RangeChecker::is_in_range(attacker_pos, target_pos, attack_range)) {
        return legend2::AttackResult::error(mir2::common::ErrorCode::TARGET_OUT_OF_RANGE);
    }

    std::vector<entt::entity> targets;
    if (modifier.is_aoe) {
        targets = collect_wide_hit_targets(registry, attacker, target_pos, modifier.aoe_radius);
    } else {
        if (is_valid_entity(registry, target)) {
            targets.push_back(target);
        }
    }

    if (targets.empty()) {
        return legend2::AttackResult::error(mir2::common::ErrorCode::TARGET_NOT_FOUND);
    }

    // 获取攻击者的被动技能加成
    PassiveSkillSystem passive_system(registry);
    const auto attacker_passive = passive_system.trigger_on_attack(attacker);

    legend2::DamageResult primary_damage = legend2::DamageResult::miss();
    bool primary_recorded = false;

    for (auto current_target : targets) {
        auto* current_attributes = registry.try_get<CharacterAttributesComponent>(current_target);
        if (!current_attributes || current_attributes->hp <= 0) {
            continue;
        }

        int total_final_damage = 0;
        int total_base_damage = 0;
        int total_variance = 0;
        bool any_critical = false;
        bool any_hit = false;

        for (int hit_index = 0; hit_index < modifier.hit_count; ++hit_index) {
            if (current_attributes->hp <= 0) {
                break;
            }

            auto* target_combat = registry.try_get<CombatComponent>(current_target);
            const auto input = build_damage_input(*attacker_attributes, *current_attributes,
                                                  attacker_combat, target_combat, config,
                                                  nullptr, nullptr, &attacker_passive);
            const int raw_damage = input.attack - input.defense;

            auto& random = get_combat_random();
            const auto rolls = random.roll_damage(raw_damage, config);
            auto hit_result = legend2::combat::DamageCalculator::calculate(input, config, rolls);

            if (hit_result.is_miss) {
                continue;
            }

            hit_result.final_damage = legend2::combat::apply_attack_modifier(
                hit_result.final_damage, modifier, attacker_attributes->hit_plus);
            if (hit_result.final_damage < 1) {
                hit_result.final_damage = 1;
            }

            TakeDamage(registry, current_target, hit_result.final_damage, event_bus);

            if (event_bus) {
                events::DamageDealtEvent event;
                event.attacker = attacker;
                event.target = current_target;
                event.damage = hit_result.final_damage;
                event.is_critical = hit_result.is_critical;
                event.is_miss = false;
                event_bus->Publish(event);
            }

            any_hit = true;
            any_critical = any_critical || hit_result.is_critical;
            total_final_damage += hit_result.final_damage;
            total_base_damage += hit_result.base_damage;
            total_variance += hit_result.variance;
        }

        if (current_target == target) {
            if (any_hit) {
                primary_damage.base_damage = total_base_damage;
                primary_damage.final_damage = total_final_damage;
                primary_damage.variance = total_variance;
                primary_damage.is_critical = any_critical;
                primary_damage.is_miss = false;
            } else {
                primary_damage = legend2::DamageResult::miss();
            }
            primary_recorded = true;
        }
    }

    if (!primary_recorded) {
        return legend2::AttackResult::error(mir2::common::ErrorCode::TARGET_NOT_FOUND);
    }

    const bool target_died = target_attributes->hp <= 0;
    return legend2::AttackResult::ok(primary_damage, target_died);
}

}  // namespace mir2::ecs
