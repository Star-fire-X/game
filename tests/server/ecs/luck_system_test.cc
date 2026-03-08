#include <gtest/gtest.h>

#include <entt/entt.hpp>

#include "ecs/components/character_components.h"
#include "ecs/systems/luck_system.h"
#include "ecs/dirty_tracker.h"

namespace {

using mir2::ecs::CharacterAttributesComponent;
using mir2::ecs::DirtyComponent;
using mir2::ecs::LuckSystem;

entt::entity CreateEntityWithLuck(entt::registry& registry, int body_luck) {
    auto entity = registry.create();
    auto& attrs = registry.emplace<CharacterAttributesComponent>(entity);
    attrs.body_luck = body_luck;
    return entity;
}

}  // namespace

TEST(LuckSystemTest, NoDecayBeforeInterval) {
    entt::registry registry;
    LuckSystem system;

    auto entity = CreateEntityWithLuck(registry, 10);

    system.Update(registry, 0.9f);

    const auto& attrs = registry.get<CharacterAttributesComponent>(entity);
    EXPECT_EQ(attrs.body_luck, 10);
}

TEST(LuckSystemTest, DecaysBodyLuckByOnePerTick) {
    entt::registry registry;
    LuckSystem system;

    auto entity = CreateEntityWithLuck(registry, 5);

    system.Update(registry, 1.0f);

    const auto& attrs = registry.get<CharacterAttributesComponent>(entity);
    EXPECT_EQ(attrs.body_luck, 4);
}

TEST(LuckSystemTest, DoesNotDecayBelowZero) {
    entt::registry registry;
    LuckSystem system;

    auto entity = CreateEntityWithLuck(registry, 0);

    system.Update(registry, 1.0f);

    const auto& attrs = registry.get<CharacterAttributesComponent>(entity);
    EXPECT_EQ(attrs.body_luck, 0);

    // No DirtyComponent should be emplaced when nothing changed
    auto* dirty = registry.try_get<DirtyComponent>(entity);
    EXPECT_EQ(dirty, nullptr);
}

TEST(LuckSystemTest, MultipleEntitiesAllDecay) {
    entt::registry registry;
    LuckSystem system;

    auto e1 = CreateEntityWithLuck(registry, 10);
    auto e2 = CreateEntityWithLuck(registry, 3);
    auto e3 = CreateEntityWithLuck(registry, 1);

    system.Update(registry, 1.0f);

    EXPECT_EQ(registry.get<CharacterAttributesComponent>(e1).body_luck, 9);
    EXPECT_EQ(registry.get<CharacterAttributesComponent>(e2).body_luck, 2);
    EXPECT_EQ(registry.get<CharacterAttributesComponent>(e3).body_luck, 0);
}

TEST(LuckSystemTest, MultipleTicksDecayMultipleTimes) {
    entt::registry registry;
    LuckSystem system;

    auto entity = CreateEntityWithLuck(registry, 10);

    // Each Update with 1.0f triggers exactly one tick
    system.Update(registry, 1.0f);
    system.Update(registry, 1.0f);
    system.Update(registry, 1.0f);

    const auto& attrs = registry.get<CharacterAttributesComponent>(entity);
    EXPECT_EQ(attrs.body_luck, 7);
}

TEST(LuckSystemTest, DirtyFlagSetOnDecay) {
    entt::registry registry;
    LuckSystem system;

    auto entity = CreateEntityWithLuck(registry, 5);

    system.Update(registry, 1.0f);

    auto* dirty = registry.try_get<DirtyComponent>(entity);
    ASSERT_NE(dirty, nullptr);
    EXPECT_TRUE(dirty->attributes_dirty);
}

TEST(LuckSystemTest, NoDirtyFlagWhenAlreadyZero) {
    entt::registry registry;
    LuckSystem system;

    auto entity = CreateEntityWithLuck(registry, 0);

    system.Update(registry, 1.0f);

    auto* dirty = registry.try_get<DirtyComponent>(entity);
    EXPECT_EQ(dirty, nullptr);
}
