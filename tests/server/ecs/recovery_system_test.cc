#include <gtest/gtest.h>

#include <algorithm>

#include <entt/entt.hpp>

#include "ecs/components/character_components.h"
#include "ecs/systems/recovery_system.h"
#include "ecs/dirty_tracker.h"

namespace {

using mir2::ecs::CharacterAttributesComponent;
using mir2::ecs::DirtyComponent;
using mir2::ecs::RecoverySystem;

entt::entity CreateLivingEntity(entt::registry& registry,
                                int max_hp, int hp,
                                int max_mp, int mp) {
    auto entity = registry.create();
    auto& attrs = registry.emplace<CharacterAttributesComponent>(entity);
    attrs.max_hp = max_hp;
    attrs.hp = hp;
    attrs.max_mp = max_mp;
    attrs.mp = mp;
    return entity;
}

}  // namespace

TEST(RecoverySystemTest, NoRecoveryBeforeInterval) {
    entt::registry registry;
    RecoverySystem system;

    auto entity = CreateLivingEntity(registry, 100, 50, 100, 50);

    system.Update(registry, 3.9f);

    const auto& attrs = registry.get<CharacterAttributesComponent>(entity);
    EXPECT_EQ(attrs.hp, 50);
    EXPECT_EQ(attrs.mp, 50);
}

TEST(RecoverySystemTest, HPRecoverAfterInterval) {
    entt::registry registry;
    RecoverySystem system;

    auto entity = CreateLivingEntity(registry, 100, 50, 100, 100);

    system.Update(registry, 4.0f);

    const auto& attrs = registry.get<CharacterAttributesComponent>(entity);
    int expected_recovery = std::max(1, 100 / 50);  // = 2
    EXPECT_EQ(attrs.hp, 50 + expected_recovery);
}

TEST(RecoverySystemTest, MPRecoverAfterInterval) {
    entt::registry registry;
    RecoverySystem system;

    auto entity = CreateLivingEntity(registry, 100, 100, 200, 80);

    system.Update(registry, 4.0f);

    const auto& attrs = registry.get<CharacterAttributesComponent>(entity);
    int expected_recovery = std::max(1, 200 / 50);  // = 4
    EXPECT_EQ(attrs.mp, 80 + expected_recovery);
}

TEST(RecoverySystemTest, DeadEntityDoesNotRecover) {
    entt::registry registry;
    RecoverySystem system;

    auto entity = CreateLivingEntity(registry, 100, 0, 100, 50);

    system.Update(registry, 4.0f);

    const auto& attrs = registry.get<CharacterAttributesComponent>(entity);
    EXPECT_EQ(attrs.hp, 0);
    EXPECT_EQ(attrs.mp, 50);
}

TEST(RecoverySystemTest, FullHPDoesNotOverheal) {
    entt::registry registry;
    RecoverySystem system;

    auto entity = CreateLivingEntity(registry, 100, 100, 100, 50);

    system.Update(registry, 4.0f);

    const auto& attrs = registry.get<CharacterAttributesComponent>(entity);
    EXPECT_EQ(attrs.hp, 100);
}

TEST(RecoverySystemTest, FullMPDoesNotOverrestore) {
    entt::registry registry;
    RecoverySystem system;

    auto entity = CreateLivingEntity(registry, 100, 50, 100, 100);

    system.Update(registry, 4.0f);

    const auto& attrs = registry.get<CharacterAttributesComponent>(entity);
    EXPECT_EQ(attrs.mp, 100);
}

TEST(RecoverySystemTest, IncHealthAddsToRecovery) {
    entt::registry registry;
    RecoverySystem system;

    auto entity = CreateLivingEntity(registry, 100, 50, 100, 100);
    auto& attrs = registry.get<CharacterAttributesComponent>(entity);
    attrs.inc_health = 10;

    system.Update(registry, 4.0f);

    int base_recovery = std::max(1, 100 / 50);  // = 2
    EXPECT_EQ(attrs.hp, 50 + base_recovery + 10);
    EXPECT_EQ(attrs.inc_health, 0);
}

TEST(RecoverySystemTest, IncSpellAddsToRecovery) {
    entt::registry registry;
    RecoverySystem system;

    auto entity = CreateLivingEntity(registry, 100, 100, 200, 80);
    auto& attrs = registry.get<CharacterAttributesComponent>(entity);
    attrs.inc_spell = 5;

    system.Update(registry, 4.0f);

    int base_recovery = std::max(1, 200 / 50);  // = 4
    EXPECT_EQ(attrs.mp, 80 + base_recovery + 5);
    EXPECT_EQ(attrs.inc_spell, 0);
}

TEST(RecoverySystemTest, HPClampedToMax) {
    entt::registry registry;
    RecoverySystem system;

    // HP is 99 with max 100, recovery of 2 would push to 101 without clamping
    auto entity = CreateLivingEntity(registry, 100, 99, 100, 100);

    system.Update(registry, 4.0f);

    const auto& attrs = registry.get<CharacterAttributesComponent>(entity);
    EXPECT_EQ(attrs.hp, 100);
}

TEST(RecoverySystemTest, AttributesDirtySetOnRecovery) {
    entt::registry registry;
    RecoverySystem system;

    auto entity = CreateLivingEntity(registry, 100, 50, 100, 50);

    system.Update(registry, 4.0f);

    auto* dirty = registry.try_get<DirtyComponent>(entity);
    ASSERT_NE(dirty, nullptr);
    EXPECT_TRUE(dirty->attributes_dirty);
}

TEST(RecoverySystemTest, AccumulatorCarriesOverExcess) {
    entt::registry registry;
    RecoverySystem system;

    auto entity = CreateLivingEntity(registry, 1000, 500, 100, 100);

    // First call: 5.0f -> accumulator 5.0, tick fires, accumulator becomes 1.0
    system.Update(registry, 5.0f);

    const auto& attrs = registry.get<CharacterAttributesComponent>(entity);
    int recovery = std::max(1, 1000 / 50);  // = 20
    EXPECT_EQ(attrs.hp, 500 + recovery);

    // Second call: 3.0f -> accumulator 1.0 + 3.0 = 4.0, tick fires again
    system.Update(registry, 3.0f);
    EXPECT_EQ(attrs.hp, 500 + recovery * 2);
}
