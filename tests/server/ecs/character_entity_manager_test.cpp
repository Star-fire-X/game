#include <gtest/gtest.h>

#include <entt/entt.hpp>

#include "ecs/character_entity_manager.h"
#include "ecs/components/character_components.h"
#include "ecs/dirty_tracker.h"

namespace {

using mir2::ecs::CharacterAttributesComponent;
using mir2::ecs::CharacterEntityManager;
using mir2::ecs::DirtyComponent;
namespace dirty_tracker = mir2::ecs::dirty_tracker;

}  // namespace

TEST(CharacterEntityManagerDirtyTest, SaveIfDirtyReturnsNotDirtyWhenClean) {
    entt::registry registry;
    CharacterEntityManager manager(registry);
    manager.GetOrCreate(1);

    auto result = manager.SaveIfDirty(1);

    EXPECT_EQ(result, CharacterEntityManager::SaveResult::kNotDirty);
}

TEST(CharacterEntityManagerDirtyTest, SaveIfDirtyReturnsEntityNotFoundForInvalidId) {
    entt::registry registry;
    CharacterEntityManager manager(registry);

    auto result = manager.SaveIfDirty(999);

    EXPECT_EQ(result, CharacterEntityManager::SaveResult::kEntityNotFound);
}

TEST(CharacterEntityManagerDirtyTest, SaveIfDirtyClearsDirtyFlagOnSuccess) {
    entt::registry registry;
    CharacterEntityManager manager(registry);
    auto entity = manager.GetOrCreate(2);

    dirty_tracker::mark_attributes_dirty(registry, entity);
    ASSERT_TRUE(dirty_tracker::is_dirty(registry, entity));

    auto result = manager.SaveIfDirty(2);

    EXPECT_EQ(result, CharacterEntityManager::SaveResult::kSuccess);
    EXPECT_FALSE(dirty_tracker::is_dirty(registry, entity));
}

TEST(CharacterEntityManagerDirtyTest, SaveIfDirtyStoresUpdatedData) {
    entt::registry registry;
    CharacterEntityManager manager(registry);
    auto entity = manager.GetOrCreate(3);

    auto& attributes = registry.get<CharacterAttributesComponent>(entity);
    attributes.hp = 42;
    dirty_tracker::mark_attributes_dirty(registry, entity);

    auto result = manager.SaveIfDirty(3);
    auto stored = manager.GetStoredData(3);

    EXPECT_EQ(result, CharacterEntityManager::SaveResult::kSuccess);
    ASSERT_TRUE(stored.has_value());
    EXPECT_EQ(stored->stats.hp, 42);
}

TEST(CharacterEntityManagerDirtyTest, SaveIfDirtyReturnsNotDirtyWhenDirtyComponentHasNoFlags) {
    entt::registry registry;
    CharacterEntityManager manager(registry);
    auto entity = manager.GetOrCreate(4);

    registry.emplace<DirtyComponent>(entity);

    auto result = manager.SaveIfDirty(4);

    EXPECT_EQ(result, CharacterEntityManager::SaveResult::kNotDirty);
}

TEST(CharacterEntityManagerDirtyTest, SaveIfDirtyHandlesMultipleDirtyFlags) {
    entt::registry registry;
    CharacterEntityManager manager(registry);
    auto entity = manager.GetOrCreate(5);

    dirty_tracker::mark_state_dirty(registry, entity);
    dirty_tracker::mark_attributes_dirty(registry, entity);

    auto result = manager.SaveIfDirty(5);

    EXPECT_EQ(result, CharacterEntityManager::SaveResult::kSuccess);
    EXPECT_FALSE(dirty_tracker::is_dirty(registry, entity));
}

TEST(CharacterEntityManagerDirtyTest, SaveAllDirtyOnlySavesDirtyEntities) {
    entt::registry registry;
    CharacterEntityManager manager(registry);
    auto dirty_entity = manager.GetOrCreate(10);
    auto clean_entity = manager.GetOrCreate(11);

    auto clean_before = manager.GetStoredData(11);
    ASSERT_TRUE(clean_before.has_value());

    auto& dirty_attr = registry.get<CharacterAttributesComponent>(dirty_entity);
    dirty_attr.hp = 88;
    dirty_tracker::mark_attributes_dirty(registry, dirty_entity);

    auto& clean_attr = registry.get<CharacterAttributesComponent>(clean_entity);
    clean_attr.hp = 12;

    manager.SaveAllDirty();

    auto dirty_after = manager.GetStoredData(10);
    auto clean_after = manager.GetStoredData(11);

    ASSERT_TRUE(dirty_after.has_value());
    ASSERT_TRUE(clean_after.has_value());
    EXPECT_EQ(dirty_after->stats.hp, 88);
    EXPECT_EQ(clean_after->stats.hp, clean_before->stats.hp);
}

TEST(CharacterEntityManagerDirtyTest, SaveAllDirtyClearsDirtyFlagOnSuccess) {
    entt::registry registry;
    CharacterEntityManager manager(registry);
    auto entity = manager.GetOrCreate(12);

    dirty_tracker::mark_state_dirty(registry, entity);

    manager.SaveAllDirty();

    EXPECT_FALSE(dirty_tracker::is_dirty(registry, entity));
}

TEST(CharacterEntityManagerDirtyTest, SaveAllDirtySkipsEntitiesWithoutIdentity) {
    entt::registry registry;
    CharacterEntityManager manager(registry);
    auto entity = registry.create();

    registry.emplace<DirtyComponent>(entity).state_dirty = true;

    manager.SaveAllDirty();

    EXPECT_FALSE(manager.GetStoredData(1).has_value());
}

TEST(CharacterEntityManagerDirtyTest, UpdateUsesSaveIfDirty) {
    entt::registry registry;
    CharacterEntityManager manager(registry);
    manager.SetSaveIntervalSeconds(0.1f);
    auto entity = manager.GetOrCreate(20);

    auto& attributes = registry.get<CharacterAttributesComponent>(entity);
    attributes.hp = 77;
    dirty_tracker::mark_attributes_dirty(registry, entity);

    manager.Update(0.11f);

    auto stored = manager.GetStoredData(20);
    ASSERT_TRUE(stored.has_value());
    EXPECT_EQ(stored->stats.hp, 77);
    EXPECT_FALSE(dirty_tracker::is_dirty(registry, entity));
}
