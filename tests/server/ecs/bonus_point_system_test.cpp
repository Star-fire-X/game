#include <gtest/gtest.h>

#include <entt/entt.hpp>

#include "ecs/components/character_components.h"
#include "ecs/components/bonus_point_component.h"
#include "ecs/systems/bonus_point_system.h"
#include "ecs/systems/growth_formulas.h"

namespace {

using mir2::ecs::BonusPointComponent;
using mir2::ecs::BonusPointSystem;
using mir2::ecs::CharacterAttributesComponent;
using mir2::ecs::CharacterIdentityComponent;
using mir2::ecs::GrowthFormulas;
using mir2::common::CharacterClass;

entt::entity CreateCharacter(entt::registry& registry, CharacterClass char_class, int level) {
    auto entity = registry.create();
    auto& identity = registry.emplace<CharacterIdentityComponent>(entity);
    identity.id = 1;
    identity.char_class = char_class;
    identity.name = "Test";

    auto& attrs = registry.emplace<CharacterAttributesComponent>(entity);
    attrs.level = level;
    attrs.max_hp = 100;
    attrs.hp = 100;
    attrs.max_mp = 50;
    attrs.mp = 50;
    return entity;
}

// ===== Available Points Calculation =====

TEST(BonusPointSystemTest, NoBonusPointsBelowLevel21) {
    entt::registry registry;
    auto entity = CreateCharacter(registry, CharacterClass::WARRIOR, 20);

    BonusPointSystem system(registry);
    system.RecalcAvailablePoints(entity);

    auto* bonus = registry.try_get<BonusPointComponent>(entity);
    ASSERT_NE(bonus, nullptr);
    EXPECT_EQ(bonus->total_available, 0);
    EXPECT_EQ(bonus->remaining(), 0);
}

TEST(BonusPointSystemTest, NoBonusPointsAtLevel1) {
    entt::registry registry;
    auto entity = CreateCharacter(registry, CharacterClass::WARRIOR, 1);

    BonusPointSystem system(registry);
    system.RecalcAvailablePoints(entity);

    auto* bonus = registry.try_get<BonusPointComponent>(entity);
    ASSERT_NE(bonus, nullptr);
    EXPECT_EQ(bonus->total_available, 0);
}

TEST(BonusPointSystemTest, WarriorLevel25Gets20Points) {
    entt::registry registry;
    auto entity = CreateCharacter(registry, CharacterClass::WARRIOR, 25);

    BonusPointSystem system(registry);
    system.RecalcAvailablePoints(entity);

    auto* bonus = registry.try_get<BonusPointComponent>(entity);
    ASSERT_NE(bonus, nullptr);
    // Warrior: 4 points/level * (25 - 20) = 20
    EXPECT_EQ(bonus->total_available, 20);
    EXPECT_EQ(bonus->remaining(), 20);
}

TEST(BonusPointSystemTest, MageLevel35Gets15Points) {
    entt::registry registry;
    auto entity = CreateCharacter(registry, CharacterClass::MAGE, 35);

    BonusPointSystem system(registry);
    system.RecalcAvailablePoints(entity);

    auto* bonus = registry.try_get<BonusPointComponent>(entity);
    ASSERT_NE(bonus, nullptr);
    // Mage: 1 point/level * (35 - 20) = 15
    EXPECT_EQ(bonus->total_available, 15);
}

TEST(BonusPointSystemTest, TaoistLevel35Gets15Points) {
    entt::registry registry;
    auto entity = CreateCharacter(registry, CharacterClass::TAOIST, 35);

    BonusPointSystem system(registry);
    system.RecalcAvailablePoints(entity);

    auto* bonus = registry.try_get<BonusPointComponent>(entity);
    ASSERT_NE(bonus, nullptr);
    // Taoist: 1 point/level * (35 - 20) = 15
    EXPECT_EQ(bonus->total_available, 15);
}

// ===== Point Allocation =====

TEST(BonusPointSystemTest, AllocatePointSucceeds) {
    entt::registry registry;
    auto entity = CreateCharacter(registry, CharacterClass::WARRIOR, 25);

    BonusPointSystem system(registry);
    system.RecalcAvailablePoints(entity);

    bool ok = system.AllocatePoint(entity, "dc");
    EXPECT_TRUE(ok);

    auto* bonus = registry.try_get<BonusPointComponent>(entity);
    ASSERT_NE(bonus, nullptr);
    EXPECT_EQ(bonus->allocated.dc, 1);
    EXPECT_EQ(bonus->remaining(), 19);
}

TEST(BonusPointSystemTest, AllocateMultipleAttributes) {
    entt::registry registry;
    auto entity = CreateCharacter(registry, CharacterClass::WARRIOR, 25);

    BonusPointSystem system(registry);
    system.RecalcAvailablePoints(entity);

    system.AllocatePoint(entity, "dc");
    system.AllocatePoint(entity, "mc");
    system.AllocatePoint(entity, "hp");
    system.AllocatePoint(entity, "hp");

    auto* bonus = registry.try_get<BonusPointComponent>(entity);
    ASSERT_NE(bonus, nullptr);
    EXPECT_EQ(bonus->allocated.dc, 1);
    EXPECT_EQ(bonus->allocated.mc, 1);
    EXPECT_EQ(bonus->allocated.hp, 2);
    EXPECT_EQ(bonus->remaining(), 16);
}

TEST(BonusPointSystemTest, AllocateFailsWhenNoPointsLeft) {
    entt::registry registry;
    // Level 21 warrior: 4 points available
    auto entity = CreateCharacter(registry, CharacterClass::WARRIOR, 21);

    BonusPointSystem system(registry);
    system.RecalcAvailablePoints(entity);

    auto* bonus = registry.try_get<BonusPointComponent>(entity);
    ASSERT_NE(bonus, nullptr);
    EXPECT_EQ(bonus->total_available, 4);

    // Spend all 4 points
    for (int i = 0; i < 4; ++i) {
        EXPECT_TRUE(system.AllocatePoint(entity, "dc"));
    }

    // 5th should fail
    EXPECT_FALSE(system.AllocatePoint(entity, "dc"));
    EXPECT_EQ(bonus->allocated.dc, 4);
}

TEST(BonusPointSystemTest, AllocateFailsForInvalidAttribute) {
    entt::registry registry;
    auto entity = CreateCharacter(registry, CharacterClass::WARRIOR, 25);

    BonusPointSystem system(registry);
    system.RecalcAvailablePoints(entity);

    EXPECT_FALSE(system.AllocatePoint(entity, "invalid"));
    EXPECT_FALSE(system.AllocatePoint(entity, ""));
    EXPECT_FALSE(system.AllocatePoint(entity, "gold"));
}

// ===== Reset =====

TEST(BonusPointSystemTest, ResetRestoresAllPoints) {
    entt::registry registry;
    auto entity = CreateCharacter(registry, CharacterClass::WARRIOR, 25);

    BonusPointSystem system(registry);
    system.RecalcAvailablePoints(entity);

    system.AllocatePoint(entity, "dc");
    system.AllocatePoint(entity, "mc");
    system.AllocatePoint(entity, "hp");

    auto* bonus = registry.try_get<BonusPointComponent>(entity);
    ASSERT_NE(bonus, nullptr);
    EXPECT_EQ(bonus->remaining(), 17);

    system.ResetPoints(entity);

    EXPECT_EQ(bonus->allocated.dc, 0);
    EXPECT_EQ(bonus->allocated.mc, 0);
    EXPECT_EQ(bonus->allocated.hp, 0);
    EXPECT_EQ(bonus->remaining(), 20);
}

TEST(BonusPointSystemTest, ResetNoComponentDoesNotCrash) {
    entt::registry registry;
    auto entity = CreateCharacter(registry, CharacterClass::WARRIOR, 25);

    BonusPointSystem system(registry);
    // No BonusPointComponent yet
    system.ResetPoints(entity);
    // Should not crash
}

// ===== All valid attribute names =====

TEST(BonusPointSystemTest, AllAttributeNamesWork) {
    entt::registry registry;
    // Level 61 warrior: 4 * 41 = 164 points
    auto entity = CreateCharacter(registry, CharacterClass::WARRIOR, 61);

    BonusPointSystem system(registry);
    system.RecalcAvailablePoints(entity);

    const char* names[] = {"dc", "mc", "sc", "ac", "mac", "hp", "mp", "hit", "speed"};
    for (const char* name : names) {
        EXPECT_TRUE(system.AllocatePoint(entity, name)) << "Failed for: " << name;
    }

    auto* bonus = registry.try_get<BonusPointComponent>(entity);
    ASSERT_NE(bonus, nullptr);
    EXPECT_EQ(bonus->allocated.total_spent(), 9);
}

}  // namespace
