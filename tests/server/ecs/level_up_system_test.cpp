#include <gtest/gtest.h>

#include <entt/entt.hpp>

#include "ecs/components/character_components.h"
#include "ecs/dirty_tracker.h"
#include "ecs/systems/level_up_system.h"

namespace {

entt::entity CreateEntity(entt::registry& registry, mir2::common::CharacterClass char_class) {
    auto entity = registry.create();
    auto& identity = registry.emplace<mir2::ecs::CharacterIdentityComponent>(entity);
    identity.id = 1;
    identity.char_class = char_class;
    identity.gender = mir2::common::Gender::MALE;
    identity.name = "Test";

    auto& attributes = registry.emplace<mir2::ecs::CharacterAttributesComponent>(entity);
    attributes.level = 1;
    attributes.experience = 0;
    attributes.max_hp = 50;
    attributes.max_mp = 30;
    attributes.hp = 50;
    attributes.mp = 30;
    return entity;
}

}  // namespace

TEST(LevelUpSystemDirtyTest, LevelUpMarksDirtyFlag) {
    entt::registry registry;
    auto entity = CreateEntity(registry, mir2::common::CharacterClass::WARRIOR);
    registry.get<mir2::ecs::CharacterAttributesComponent>(entity).experience = 100;

    mir2::ecs::LevelUpSystem system;
    system.Update(registry, 0.0f);

    EXPECT_TRUE(mir2::ecs::dirty_tracker::is_dirty(registry, entity));
    auto* dirty = registry.try_get<mir2::ecs::DirtyComponent>(entity);
    ASSERT_NE(dirty, nullptr);
    EXPECT_TRUE(dirty->attributes_dirty);
}

TEST(LevelUpSystemDirtyTest, NoLevelUpDoesNotMarkDirty) {
    entt::registry registry;
    auto entity = CreateEntity(registry, mir2::common::CharacterClass::MAGE);
    registry.get<mir2::ecs::CharacterAttributesComponent>(entity).experience = 10;

    mir2::ecs::LevelUpSystem system;
    system.Update(registry, 0.0f);

    EXPECT_FALSE(mir2::ecs::dirty_tracker::is_dirty(registry, entity));
}

TEST(LevelUpSystemDirtyTest, MultipleLevelUpKeepsDirtyFlag) {
    entt::registry registry;
    auto entity = CreateEntity(registry, mir2::common::CharacterClass::TAOIST);
    registry.get<mir2::ecs::CharacterAttributesComponent>(entity).experience = 500;

    mir2::ecs::LevelUpSystem system;
    system.Update(registry, 0.0f);

    EXPECT_TRUE(mir2::ecs::dirty_tracker::is_dirty(registry, entity));
    auto& attributes = registry.get<mir2::ecs::CharacterAttributesComponent>(entity);
    EXPECT_GT(attributes.level, 1);
}
