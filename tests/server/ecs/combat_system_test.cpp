#include <gtest/gtest.h>

#include <entt/entt.hpp>

#include "ecs/components/character_components.h"
#include "ecs/dirty_tracker.h"
#include "ecs/systems/combat_system.h"

namespace {

entt::entity CreateEntityWithAttributes(entt::registry& registry) {
    auto entity = registry.create();
    auto& attributes = registry.emplace<mir2::ecs::CharacterAttributesComponent>(entity);
    attributes.max_hp = 10;
    attributes.hp = 10;
    attributes.max_mp = 5;
    attributes.mp = 5;
    return entity;
}

}  // namespace

TEST(CombatSystemDirtyTest, TakeDamageMarksDirtyFlag) {
    entt::registry registry;
    auto entity = CreateEntityWithAttributes(registry);

    mir2::ecs::CombatSystem::TakeDamage(registry, entity, 3);

    EXPECT_TRUE(mir2::ecs::dirty_tracker::is_dirty(registry, entity));
    auto* dirty = registry.try_get<mir2::ecs::DirtyComponent>(entity);
    ASSERT_NE(dirty, nullptr);
    EXPECT_TRUE(dirty->attributes_dirty);
}

TEST(CombatSystemDirtyTest, HealMarksDirtyFlag) {
    entt::registry registry;
    auto entity = CreateEntityWithAttributes(registry);
    registry.get<mir2::ecs::CharacterAttributesComponent>(entity).hp = 5;

    mir2::ecs::CombatSystem::Heal(registry, entity, 2);

    EXPECT_TRUE(mir2::ecs::dirty_tracker::is_dirty(registry, entity));
    auto* dirty = registry.try_get<mir2::ecs::DirtyComponent>(entity);
    ASSERT_NE(dirty, nullptr);
    EXPECT_TRUE(dirty->attributes_dirty);
}

TEST(CombatSystemDirtyTest, RestoreMPMarksDirtyFlag) {
    entt::registry registry;
    auto entity = CreateEntityWithAttributes(registry);
    registry.get<mir2::ecs::CharacterAttributesComponent>(entity).mp = 1;

    mir2::ecs::CombatSystem::RestoreMP(registry, entity, 2);

    EXPECT_TRUE(mir2::ecs::dirty_tracker::is_dirty(registry, entity));
    auto* dirty = registry.try_get<mir2::ecs::DirtyComponent>(entity);
    ASSERT_NE(dirty, nullptr);
    EXPECT_TRUE(dirty->attributes_dirty);
}

TEST(CombatSystemDirtyTest, ConsumeMPMarksDirtyFlag) {
    entt::registry registry;
    auto entity = CreateEntityWithAttributes(registry);

    bool consumed = mir2::ecs::CombatSystem::ConsumeMP(registry, entity, 3);

    EXPECT_TRUE(consumed);
    EXPECT_TRUE(mir2::ecs::dirty_tracker::is_dirty(registry, entity));
    auto* dirty = registry.try_get<mir2::ecs::DirtyComponent>(entity);
    ASSERT_NE(dirty, nullptr);
    EXPECT_TRUE(dirty->attributes_dirty);
}

TEST(CombatSystemDirtyTest, DieMarksDirtyFlag) {
    entt::registry registry;
    auto entity = CreateEntityWithAttributes(registry);

    mir2::ecs::CombatSystem::Die(registry, entity);

    EXPECT_TRUE(mir2::ecs::dirty_tracker::is_dirty(registry, entity));
    auto* dirty = registry.try_get<mir2::ecs::DirtyComponent>(entity);
    ASSERT_NE(dirty, nullptr);
    EXPECT_TRUE(dirty->attributes_dirty);
}

TEST(CombatSystemDirtyTest, RespawnMarksDirtyFlag) {
    entt::registry registry;
    auto entity = CreateEntityWithAttributes(registry);

    mir2::ecs::CombatSystem::Respawn(registry, entity, {3, 4}, 0.5f, 0.5f);

    EXPECT_TRUE(mir2::ecs::dirty_tracker::is_dirty(registry, entity));
    auto* dirty = registry.try_get<mir2::ecs::DirtyComponent>(entity);
    ASSERT_NE(dirty, nullptr);
    EXPECT_TRUE(dirty->attributes_dirty);
}
