#include <gtest/gtest.h>

#include <entt/entt.hpp>

#include "ecs/components/character_components.h"
#include "ecs/systems/attribute_recalc_system.h"
#include "ecs/systems/growth_formulas.h"

namespace {

using mir2::ecs::AttributeRecalcSystem;
using mir2::ecs::CharacterAttributesComponent;
using mir2::ecs::CharacterIdentityComponent;
using mir2::ecs::GrowthFormulas;
using mir2::ecs::NakedAbility;
using mir2::common::CharacterClass;

entt::entity CreateCharacter(entt::registry& registry, CharacterClass char_class, int level) {
    auto entity = registry.create();
    auto& identity = registry.emplace<CharacterIdentityComponent>(entity);
    identity.id = 1;
    identity.char_class = char_class;
    identity.name = "Test";

    auto& attrs = registry.emplace<CharacterAttributesComponent>(entity);
    attrs.level = level;
    attrs.hp = 999;
    attrs.mp = 999;
    return entity;
}

// ===== RecalcAll sets base attributes correctly =====

TEST(AttributeRecalcSystemTest, WarriorLevel35Attributes) {
    entt::registry registry;
    auto entity = CreateCharacter(registry, CharacterClass::WARRIOR, 35);

    AttributeRecalcSystem::RecalcAll(registry, entity);

    auto& attrs = registry.get<CharacterAttributesComponent>(entity);
    NakedAbility expected = GrowthFormulas::CalcLevelAbility(CharacterClass::WARRIOR, 35);

    EXPECT_EQ(attrs.max_hp, expected.max_hp);
    EXPECT_EQ(attrs.max_mp, expected.max_mp);
    EXPECT_EQ(attrs.attack, expected.dc_hi);
    EXPECT_EQ(attrs.defense, expected.ac_hi);
    EXPECT_EQ(attrs.magic_attack, expected.mc_hi);
    EXPECT_EQ(attrs.magic_defense, expected.mac_hi);
    EXPECT_EQ(attrs.max_weight, expected.max_weight);
    EXPECT_EQ(attrs.max_wear_weight, expected.max_wear_weight);
    EXPECT_EQ(attrs.max_hand_weight, expected.max_hand_weight);
}

TEST(AttributeRecalcSystemTest, MageLevel35Attributes) {
    entt::registry registry;
    auto entity = CreateCharacter(registry, CharacterClass::MAGE, 35);

    AttributeRecalcSystem::RecalcAll(registry, entity);

    auto& attrs = registry.get<CharacterAttributesComponent>(entity);
    NakedAbility expected = GrowthFormulas::CalcLevelAbility(CharacterClass::MAGE, 35);

    EXPECT_EQ(attrs.max_hp, expected.max_hp);
    EXPECT_EQ(attrs.max_mp, expected.max_mp);
    EXPECT_EQ(attrs.magic_attack, expected.mc_hi);
}

TEST(AttributeRecalcSystemTest, TaoistLevel35Attributes) {
    entt::registry registry;
    auto entity = CreateCharacter(registry, CharacterClass::TAOIST, 35);

    AttributeRecalcSystem::RecalcAll(registry, entity);

    auto& attrs = registry.get<CharacterAttributesComponent>(entity);
    NakedAbility expected = GrowthFormulas::CalcLevelAbility(CharacterClass::TAOIST, 35);

    EXPECT_EQ(attrs.max_hp, expected.max_hp);
    EXPECT_EQ(attrs.max_mp, expected.max_mp);
    EXPECT_EQ(attrs.sc, expected.sc_hi);
}

// ===== HP/MP clamping =====

TEST(AttributeRecalcSystemTest, ClampsHPToMax) {
    entt::registry registry;
    auto entity = CreateCharacter(registry, CharacterClass::WARRIOR, 10);
    auto& attrs = registry.get<CharacterAttributesComponent>(entity);
    attrs.hp = 999999;
    attrs.mp = 999999;

    AttributeRecalcSystem::RecalcAll(registry, entity);

    EXPECT_LE(attrs.hp, attrs.max_hp);
    EXPECT_LE(attrs.mp, attrs.max_mp);
}

TEST(AttributeRecalcSystemTest, DoesNotIncreaseHP) {
    entt::registry registry;
    auto entity = CreateCharacter(registry, CharacterClass::WARRIOR, 10);
    auto& attrs = registry.get<CharacterAttributesComponent>(entity);
    attrs.hp = 5;
    attrs.mp = 3;

    AttributeRecalcSystem::RecalcAll(registry, entity);

    // HP/MP should not be increased, only clamped down
    EXPECT_EQ(attrs.hp, 5);
    EXPECT_EQ(attrs.mp, 3);
}

// ===== Class ordering in RecalcAll =====

TEST(AttributeRecalcSystemTest, WarriorHasHighestHP) {
    entt::registry registry;
    auto warrior = CreateCharacter(registry, CharacterClass::WARRIOR, 35);
    auto mage = CreateCharacter(registry, CharacterClass::MAGE, 35);
    auto taoist = CreateCharacter(registry, CharacterClass::TAOIST, 35);

    AttributeRecalcSystem::RecalcAll(registry, warrior);
    AttributeRecalcSystem::RecalcAll(registry, mage);
    AttributeRecalcSystem::RecalcAll(registry, taoist);

    auto& w = registry.get<CharacterAttributesComponent>(warrior);
    auto& m = registry.get<CharacterAttributesComponent>(mage);
    auto& t = registry.get<CharacterAttributesComponent>(taoist);

    EXPECT_GT(w.max_hp, t.max_hp);
    EXPECT_GT(t.max_hp, m.max_hp);
}

TEST(AttributeRecalcSystemTest, WarriorHasHighestWeight) {
    entt::registry registry;
    auto warrior = CreateCharacter(registry, CharacterClass::WARRIOR, 35);
    auto mage = CreateCharacter(registry, CharacterClass::MAGE, 35);
    auto taoist = CreateCharacter(registry, CharacterClass::TAOIST, 35);

    AttributeRecalcSystem::RecalcAll(registry, warrior);
    AttributeRecalcSystem::RecalcAll(registry, mage);
    AttributeRecalcSystem::RecalcAll(registry, taoist);

    auto& w = registry.get<CharacterAttributesComponent>(warrior);
    auto& m = registry.get<CharacterAttributesComponent>(mage);
    auto& t = registry.get<CharacterAttributesComponent>(taoist);

    EXPECT_GT(w.max_weight, t.max_weight);
    EXPECT_GT(t.max_weight, m.max_weight);
}

// ===== Missing identity gracefully handled =====

TEST(AttributeRecalcSystemTest, NoIdentityDoesNotCrash) {
    entt::registry registry;
    auto entity = registry.create();
    registry.emplace<CharacterAttributesComponent>(entity);

    // Should not crash when identity is missing
    AttributeRecalcSystem::RecalcAll(registry, entity);
}

TEST(AttributeRecalcSystemTest, NoAttributesDoesNotCrash) {
    entt::registry registry;
    auto entity = registry.create();
    registry.emplace<CharacterIdentityComponent>(entity);

    // Should not crash when attributes is missing
    AttributeRecalcSystem::RecalcAll(registry, entity);
}

}  // namespace
