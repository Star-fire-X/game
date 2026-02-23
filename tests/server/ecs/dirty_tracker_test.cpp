#include <gtest/gtest.h>

#include <entt/entt.hpp>

#include "ecs/components/character_components.h"
#include "ecs/dirty_tracker.h"

namespace {

using mir2::ecs::DirtyComponent;
namespace dirty_tracker = mir2::ecs::dirty_tracker;

}  // namespace

TEST(DirtyTrackerTest, MarkIdentityDirtyCreatesDirtyComponent) {
    entt::registry registry;
    auto entity = registry.create();

    dirty_tracker::mark_identity_dirty(registry, entity);

    auto* dirty = registry.try_get<DirtyComponent>(entity);
    ASSERT_NE(dirty, nullptr);
    EXPECT_TRUE(dirty->identity_dirty);
    EXPECT_TRUE(dirty_tracker::is_dirty(registry, entity));
}

TEST(DirtyTrackerTest, MarkAttributesDirtyCreatesDirtyComponent) {
    entt::registry registry;
    auto entity = registry.create();

    dirty_tracker::mark_attributes_dirty(registry, entity);

    auto* dirty = registry.try_get<DirtyComponent>(entity);
    ASSERT_NE(dirty, nullptr);
    EXPECT_TRUE(dirty->attributes_dirty);
    EXPECT_TRUE(dirty_tracker::is_dirty(registry, entity));
}

TEST(DirtyTrackerTest, MarkStateDirtyCreatesDirtyComponent) {
    entt::registry registry;
    auto entity = registry.create();

    dirty_tracker::mark_state_dirty(registry, entity);

    auto* dirty = registry.try_get<DirtyComponent>(entity);
    ASSERT_NE(dirty, nullptr);
    EXPECT_TRUE(dirty->state_dirty);
    EXPECT_TRUE(dirty_tracker::is_dirty(registry, entity));
}

TEST(DirtyTrackerTest, MarkItemsDirtyCreatesDirtyComponent) {
    entt::registry registry;
    auto entity = registry.create();

    dirty_tracker::mark_items_dirty(registry, entity);

    auto* dirty = registry.try_get<DirtyComponent>(entity);
    ASSERT_NE(dirty, nullptr);
    EXPECT_TRUE(dirty->items_dirty);
    EXPECT_TRUE(dirty_tracker::is_dirty(registry, entity));
}

TEST(DirtyTrackerTest, MarkItemsDirtyKeepsOtherDirtyFlagsClean) {
    entt::registry registry;
    auto entity = registry.create();

    dirty_tracker::mark_items_dirty(registry, entity);

    auto* dirty = registry.try_get<DirtyComponent>(entity);
    ASSERT_NE(dirty, nullptr);
    EXPECT_TRUE(dirty->items_dirty);
    EXPECT_FALSE(dirty->identity_dirty);
    EXPECT_FALSE(dirty->attributes_dirty);
    EXPECT_FALSE(dirty->state_dirty);
    EXPECT_FALSE(dirty->equipment_dirty);
    EXPECT_FALSE(dirty->skills_dirty);
    EXPECT_TRUE(dirty_tracker::is_dirty(registry, entity));
}

TEST(DirtyTrackerTest, MarkEquipmentAndSkillsDirty) {
    entt::registry registry;
    auto entity = registry.create();

    dirty_tracker::mark_equipment_dirty(registry, entity);
    dirty_tracker::mark_skills_dirty(registry, entity);

    auto* dirty = registry.try_get<DirtyComponent>(entity);
    ASSERT_NE(dirty, nullptr);
    EXPECT_TRUE(dirty->equipment_dirty);
    EXPECT_TRUE(dirty->skills_dirty);
    EXPECT_TRUE(dirty_tracker::is_dirty(registry, entity));
}

TEST(DirtyTrackerTest, IsDirtyReturnsFalseForCleanEntity) {
    entt::registry registry;
    auto entity = registry.create();

    EXPECT_FALSE(dirty_tracker::is_dirty(registry, entity));
}

TEST(DirtyTrackerTest, IsDirtyReturnsFalseForEmptyFlags) {
    entt::registry registry;
    auto entity = registry.create();
    registry.emplace<DirtyComponent>(entity);

    EXPECT_FALSE(dirty_tracker::is_dirty(registry, entity));
}

TEST(DirtyTrackerTest, IsDirtyReturnsTrueWhenAnyFlagSet) {
    entt::registry registry;
    auto entity = registry.create();
    auto& dirty = registry.emplace<DirtyComponent>(entity);
    dirty.state_dirty = true;

    EXPECT_TRUE(dirty_tracker::is_dirty(registry, entity));
}

TEST(DirtyTrackerTest, ClearDirtyRemovesComponent) {
    entt::registry registry;
    auto entity = registry.create();
    dirty_tracker::mark_attributes_dirty(registry, entity);

    dirty_tracker::clear_dirty(registry, entity);

    EXPECT_FALSE(registry.all_of<DirtyComponent>(entity));
    EXPECT_FALSE(dirty_tracker::is_dirty(registry, entity));
}
