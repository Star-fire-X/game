#include <gtest/gtest.h>

#include <cmath>

#include <entt/entt.hpp>

#include "ecs/components/character_components.h"
#include "ecs/components/effect_component.h"
#include "ecs/components/skill_component.h"
#include "ecs/components/skill_template_component.h"
#include "ecs/skill_registry.h"
#include "ecs/systems/damage_calculator.h"
#include "ecs/systems/skill_system.h"

namespace {

using mir2::ecs::ActiveEffect;
using mir2::ecs::CharacterAttributesComponent;
using mir2::ecs::CharacterIdentityComponent;
using mir2::ecs::DamageCalculator;
using mir2::ecs::EffectCategory;
using mir2::ecs::EffectListComponent;
using mir2::ecs::SkillCooldownComponent;
using mir2::ecs::SkillListComponent;
using mir2::ecs::SkillRegistry;
using mir2::ecs::SkillSystem;
using mir2::ecs::SkillTemplate;

struct SkillRegistryGuard {
    SkillRegistryGuard() { SkillRegistry::instance().clear(); }
    ~SkillRegistryGuard() { SkillRegistry::instance().clear(); }
};

SkillTemplate MakeSkillTemplate(uint32_t id,
                                mir2::common::CharacterClass required_class,
                                uint8_t required_level,
                                mir2::common::SkillType skill_type,
                                mir2::common::SkillTarget target_type) {
    SkillTemplate skill;
    skill.id = id;
    skill.name = "TestSkill";
    skill.required_class = required_class;
    skill.required_level = required_level;
    skill.skill_type = skill_type;
    skill.target_type = target_type;
    skill.duration_ms = 1000;
    skill.stat_modifier = 3;
    return skill;
}

entt::entity CreateCharacter(entt::registry& registry,
                             mir2::common::CharacterClass cls,
                             int level,
                             int hp,
                             int mp) {
    const auto entity = registry.create();
    auto& identity = registry.emplace<CharacterIdentityComponent>(entity);
    identity.char_class = cls;

    auto& attributes = registry.emplace<CharacterAttributesComponent>(entity);
    attributes.level = level;
    attributes.hp = hp;
    attributes.max_hp = hp;
    attributes.mp = mp;
    attributes.max_mp = mp;
    return entity;
}

}  // namespace

TEST(DamageCalculator, GetPower_MatchesDocFormula) {
    SkillTemplate skill;
    skill.min_power = 10;
    skill.train_lv = 3;
    skill.def_power = 4;

    const int skill_level = 2;
    const int expected = static_cast<int>(std::round(
        10.0 / (3.0 + 1.0) * (skill_level + 1.0))) + 4;

    EXPECT_EQ(DamageCalculator::get_power(skill, skill_level), expected);
}

TEST(DamageCalculator, GetPower13_GuaranteesOneThird) {
    const int base_value = 12;
    const int train_level = 100;
    const int skill_level = 0;

    const int result = DamageCalculator::get_power_13(base_value, skill_level, train_level);
    const int expected = static_cast<int>(std::round(
        (base_value * 2.0 / 3.0) / (train_level + 1.0) * (skill_level + 1.0) +
        base_value / 3.0));

    EXPECT_EQ(result, expected);
    EXPECT_GE(result, base_value / 3);
}

TEST(DamageCalculator, LuckCrit_MaxDamage) {
    const int base_power = 10;
    const int power_range = 7;
    const int luck = 9;
    const int max_damage = base_power + power_range;

    for (int i = 0; i < 20; ++i) {
        EXPECT_EQ(DamageCalculator::get_attack_power(base_power, power_range, luck), max_damage);
    }
}

TEST(DamageCalculator, PhysicalDamage_Basic) {
    entt::registry registry;
    const auto attacker = CreateCharacter(registry, mir2::common::CharacterClass::WARRIOR, 10, 100, 50);
    const auto defender = CreateCharacter(registry, mir2::common::CharacterClass::WARRIOR, 10, 100, 50);

    auto& attacker_attr = registry.get<CharacterAttributesComponent>(attacker);
    attacker_attr.attack = 15;
    attacker_attr.luck = 0;

    auto& defender_attr = registry.get<CharacterAttributesComponent>(defender);
    defender_attr.defense = 12;

    auto skill = MakeSkillTemplate(3000, mir2::common::CharacterClass::WARRIOR, 1,
                                   mir2::common::SkillType::PHYSICAL,
                                   mir2::common::SkillTarget::SINGLE_ENEMY);
    skill.min_power = 5;
    skill.max_power = 5;
    skill.def_power = 0;
    skill.train_lv = 0;

    const int damage = DamageCalculator::calculate_physical_damage(
        attacker_attr, defender_attr, skill, /*skill_level=*/0);
    int expected = attacker_attr.attack + skill.min_power - defender_attr.defense;
    if (expected < 1) {
        expected = 1;
    }

    EXPECT_EQ(damage, expected);
    EXPECT_GE(damage, 1);
}

TEST(DamageCalculator, MagicDamage_Basic) {
    entt::registry registry;
    const auto attacker = CreateCharacter(registry, mir2::common::CharacterClass::MAGE, 10, 80, 120);
    const auto defender = CreateCharacter(registry, mir2::common::CharacterClass::WARRIOR, 10, 120, 30);

    auto& attacker_attr = registry.get<CharacterAttributesComponent>(attacker);
    attacker_attr.magic_attack = 20;

    auto& defender_attr = registry.get<CharacterAttributesComponent>(defender);
    defender_attr.magic_defense = 8;

    auto skill = MakeSkillTemplate(3001, mir2::common::CharacterClass::MAGE, 1,
                                   mir2::common::SkillType::MAGICAL,
                                   mir2::common::SkillTarget::SINGLE_ENEMY);
    skill.min_power = 6;
    skill.max_power = 6;
    skill.def_power = 0;
    skill.train_lv = 0;

    const int damage = DamageCalculator::calculate_magic_damage(
        attacker_attr, defender_attr, skill, /*skill_level=*/0, /*is_undead=*/false);
    int expected = attacker_attr.magic_attack + skill.min_power - defender_attr.magic_defense;
    if (expected < 1) {
        expected = 1;
    }

    EXPECT_EQ(damage, expected);
    EXPECT_GE(damage, 1);
}

TEST(DamageCalculator, MagicDamage_UndeadBonus) {
    entt::registry registry;
    const auto attacker = CreateCharacter(registry, mir2::common::CharacterClass::MAGE, 10, 80, 120);
    const auto defender = CreateCharacter(registry, mir2::common::CharacterClass::WARRIOR, 10, 120, 30);

    auto& attacker_attr = registry.get<CharacterAttributesComponent>(attacker);
    attacker_attr.magic_attack = 18;

    auto& defender_attr = registry.get<CharacterAttributesComponent>(defender);
    defender_attr.magic_defense = 5;

    auto skill = MakeSkillTemplate(3002, mir2::common::CharacterClass::MAGE, 1,
                                   mir2::common::SkillType::MAGICAL,
                                   mir2::common::SkillTarget::SINGLE_ENEMY);
    skill.min_power = 2;
    skill.max_power = 2;
    skill.def_power = 0;
    skill.train_lv = 0;

    const int damage = DamageCalculator::calculate_magic_damage(
        attacker_attr, defender_attr, skill, /*skill_level=*/0, /*is_undead=*/true);
    const int magic_power = attacker_attr.magic_attack + skill.min_power;
    const int adjusted = static_cast<int>(std::round(magic_power * 1.5));
    int expected = adjusted - defender_attr.magic_defense;
    if (expected < 1) {
        expected = 1;
    }

    EXPECT_EQ(damage, expected);
}

TEST(DamageCalculator, Healing_ScalesWithSC) {
    entt::registry registry;
    const auto caster = CreateCharacter(registry, mir2::common::CharacterClass::TAOIST, 10, 70, 90);

    auto& caster_attr = registry.get<CharacterAttributesComponent>(caster);
    caster_attr.sc = 50;

    auto skill = MakeSkillTemplate(3003, mir2::common::CharacterClass::TAOIST, 1,
                                   mir2::common::SkillType::HEAL,
                                   mir2::common::SkillTarget::SINGLE_ALLY);
    skill.min_power = 10;
    skill.max_power = 10;
    skill.def_power = 0;
    skill.train_lv = 0;

    const int base_heal = skill.min_power;
    const int healing = DamageCalculator::calculate_healing(caster_attr, skill, /*skill_level=*/0);
    const int expected = static_cast<int>(std::round(base_heal * 1.5));

    EXPECT_EQ(healing, expected);
    EXPECT_GT(healing, base_heal);
}

TEST(DamageCalculator, ElementalResistance) {
    const int damage = 200;
    const int resistance = 25;
    const int expected = damage * (100 - resistance) / 100;

    EXPECT_EQ(DamageCalculator::apply_elemental_resistance(damage, resistance), expected);
    EXPECT_EQ(DamageCalculator::apply_elemental_resistance(damage, 100), 1);
}

TEST(SkillSystem, LearnSkill_Success) {
    SkillRegistryGuard guard;
    entt::registry registry;
    SkillSystem system(registry);

    const auto skill = MakeSkillTemplate(1001, mir2::common::CharacterClass::WARRIOR, 5,
                                         mir2::common::SkillType::BUFF,
                                         mir2::common::SkillTarget::SELF);
    SkillRegistry::instance().register_skill(skill);

    const auto entity = CreateCharacter(registry, mir2::common::CharacterClass::WARRIOR, 5, 100, 20);

    EXPECT_EQ(system.learn_skill(entity, skill.id), mir2::common::ErrorCode::SUCCESS);

    const auto* skill_list = registry.try_get<SkillListComponent>(entity);
    ASSERT_NE(skill_list, nullptr);
    EXPECT_TRUE(skill_list->has_skill(skill.id));
    EXPECT_EQ(skill_list->count, 1);
}

TEST(SkillSystem, LearnSkill_ClassRestriction) {
    SkillRegistryGuard guard;
    entt::registry registry;
    SkillSystem system(registry);

    const auto skill = MakeSkillTemplate(1002, mir2::common::CharacterClass::MAGE, 1,
                                         mir2::common::SkillType::BUFF,
                                         mir2::common::SkillTarget::SELF);
    SkillRegistry::instance().register_skill(skill);

    const auto entity = CreateCharacter(registry, mir2::common::CharacterClass::WARRIOR, 10, 100, 20);

    EXPECT_EQ(system.learn_skill(entity, skill.id),
              mir2::common::ErrorCode::CLASS_REQUIREMENT_NOT_MET);

    const auto* skill_list = registry.try_get<SkillListComponent>(entity);
    if (skill_list) {
        EXPECT_FALSE(skill_list->has_skill(skill.id));
    }
}

TEST(SkillSystem, LearnSkill_LevelRequirement) {
    SkillRegistryGuard guard;
    entt::registry registry;
    SkillSystem system(registry);

    const auto skill = MakeSkillTemplate(1003, mir2::common::CharacterClass::WARRIOR, 10,
                                         mir2::common::SkillType::BUFF,
                                         mir2::common::SkillTarget::SELF);
    SkillRegistry::instance().register_skill(skill);

    const auto entity = CreateCharacter(registry, mir2::common::CharacterClass::WARRIOR, 5, 100, 20);

    EXPECT_EQ(system.learn_skill(entity, skill.id),
              mir2::common::ErrorCode::LEVEL_REQUIREMENT_NOT_MET);
}

TEST(SkillSystem, LearnSkill_SlotFull) {
    SkillRegistryGuard guard;
    entt::registry registry;
    SkillSystem system(registry);

    const auto skill = MakeSkillTemplate(1004, mir2::common::CharacterClass::WARRIOR, 1,
                                         mir2::common::SkillType::BUFF,
                                         mir2::common::SkillTarget::SELF);
    SkillRegistry::instance().register_skill(skill);

    const auto entity = CreateCharacter(registry, mir2::common::CharacterClass::WARRIOR, 20, 100, 20);
    auto& skill_list = registry.emplace<SkillListComponent>(entity);

    for (int i = 0; i < SkillListComponent::MAX_SKILLS; ++i) {
        EXPECT_TRUE(skill_list.add_skill(2000u + static_cast<uint32_t>(i)));
    }
    EXPECT_EQ(skill_list.count, SkillListComponent::MAX_SKILLS);

    EXPECT_EQ(system.learn_skill(entity, skill.id), mir2::common::ErrorCode::INVALID_ACTION);
    EXPECT_FALSE(skill_list.has_skill(skill.id));
    EXPECT_EQ(skill_list.count, SkillListComponent::MAX_SKILLS);
}

TEST(SkillSystem, CastSkill_Success) {
    SkillRegistryGuard guard;
    entt::registry registry;
    SkillSystem system(registry);

    auto skill = MakeSkillTemplate(2001, mir2::common::CharacterClass::WARRIOR, 1,
                                   mir2::common::SkillType::BUFF,
                                   mir2::common::SkillTarget::SELF);
    skill.mp_cost = 3;
    skill.cooldown_ms = 500;
    SkillRegistry::instance().register_skill(skill);

    const auto caster = CreateCharacter(registry, mir2::common::CharacterClass::WARRIOR, 10, 50, 10);
    ASSERT_EQ(system.learn_skill(caster, skill.id), mir2::common::ErrorCode::SUCCESS);

    system.update(1000);
    const auto result = system.cast_skill(caster, skill.id);

    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.error_code, mir2::common::ErrorCode::SUCCESS);
    EXPECT_EQ(result.mp_consumed, skill.mp_cost);

    const auto& attributes = registry.get<CharacterAttributesComponent>(caster);
    EXPECT_EQ(attributes.mp, 7);

    ASSERT_EQ(result.targets.size(), 1u);
    EXPECT_EQ(result.targets[0].target, static_cast<uint32_t>(caster));

    const auto* effects = registry.try_get<EffectListComponent>(caster);
    ASSERT_NE(effects, nullptr);
    EXPECT_EQ(effects->effects.size(), 1u);

    ASSERT_TRUE(result.applied_effect.has_value());
    EXPECT_EQ(result.applied_effect->skill_id, skill.id);
}

TEST(SkillSystem, CastSkill_InsufficientMP) {
    SkillRegistryGuard guard;
    entt::registry registry;
    SkillSystem system(registry);

    auto skill = MakeSkillTemplate(2002, mir2::common::CharacterClass::WARRIOR, 1,
                                   mir2::common::SkillType::BUFF,
                                   mir2::common::SkillTarget::SELF);
    skill.mp_cost = 10;
    SkillRegistry::instance().register_skill(skill);

    const auto caster = CreateCharacter(registry, mir2::common::CharacterClass::WARRIOR, 10, 50, 3);
    ASSERT_EQ(system.learn_skill(caster, skill.id), mir2::common::ErrorCode::SUCCESS);

    system.update(1000);
    const auto result = system.cast_skill(caster, skill.id);

    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.error_code, mir2::common::ErrorCode::INSUFFICIENT_MP);
    EXPECT_EQ(registry.get<CharacterAttributesComponent>(caster).mp, 3);
}

TEST(SkillSystem, CastSkill_OnCooldown) {
    SkillRegistryGuard guard;
    entt::registry registry;
    SkillSystem system(registry);

    auto skill = MakeSkillTemplate(2003, mir2::common::CharacterClass::WARRIOR, 1,
                                   mir2::common::SkillType::BUFF,
                                   mir2::common::SkillTarget::SELF);
    skill.mp_cost = 1;
    skill.cooldown_ms = 1000;
    SkillRegistry::instance().register_skill(skill);

    const auto caster = CreateCharacter(registry, mir2::common::CharacterClass::WARRIOR, 10, 50, 10);
    ASSERT_EQ(system.learn_skill(caster, skill.id), mir2::common::ErrorCode::SUCCESS);

    system.update(1000);
    auto& cooldowns = registry.emplace<SkillCooldownComponent>(caster);
    cooldowns.start_cooldown(skill.id, 5000, 1000);

    const auto result = system.cast_skill(caster, skill.id);

    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.error_code, mir2::common::ErrorCode::SKILL_ON_COOLDOWN);
}

TEST(SkillListComponent, AddRemoveSkill) {
    SkillListComponent skill_list;

    EXPECT_TRUE(skill_list.add_skill(10));
    EXPECT_EQ(skill_list.count, 1);
    EXPECT_TRUE(skill_list.has_skill(10));

    EXPECT_FALSE(skill_list.add_skill(10));

    EXPECT_TRUE(skill_list.remove_skill(10));
    EXPECT_EQ(skill_list.count, 0);
    EXPECT_FALSE(skill_list.has_skill(10));
    EXPECT_FALSE(skill_list.remove_skill(10));
}

TEST(SkillCooldownComponent, CooldownTracking) {
    SkillCooldownComponent cooldowns;

    cooldowns.start_cooldown(1, 500, 1000);
    EXPECT_FALSE(cooldowns.is_ready(1, 1200));
    EXPECT_EQ(cooldowns.get_remaining_ms(1, 1200), 300);

    EXPECT_TRUE(cooldowns.is_ready(1, 1500));
    EXPECT_EQ(cooldowns.get_remaining_ms(1, 1500), 0);

    cooldowns.cleanup_expired(1600);
    EXPECT_TRUE(cooldowns.is_ready(1, 1600));
    EXPECT_TRUE(cooldowns.cooldowns.empty());
}

TEST(EffectListComponent, EffectManagement) {
    EffectListComponent effects;

    ActiveEffect buff;
    buff.skill_id = 1;
    buff.category = EffectCategory::STAT_BUFF;
    buff.end_time_ms = 1000;

    ActiveEffect dot;
    dot.skill_id = 2;
    dot.category = EffectCategory::DAMAGE_OVER_TIME;
    dot.end_time_ms = 2000;

    effects.add_effect(buff);
    effects.add_effect(dot);

    EXPECT_EQ(effects.effects.size(), 2u);

    effects.remove_effects_by_skill(1);
    ASSERT_EQ(effects.effects.size(), 1u);
    EXPECT_EQ(effects.effects[0].skill_id, 2u);

    ActiveEffect shield;
    shield.skill_id = 3;
    shield.category = EffectCategory::SHIELD;
    shield.end_time_ms = 0;
    effects.add_effect(shield);

    EXPECT_EQ(effects.effects.size(), 2u);

    effects.remove_expired(2000);
    ASSERT_EQ(effects.effects.size(), 1u);
    EXPECT_EQ(effects.effects[0].skill_id, 3u);
}
