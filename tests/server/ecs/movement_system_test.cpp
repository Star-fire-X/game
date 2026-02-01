#include <gtest/gtest.h>

#include <entt/entt.hpp>

#include "common/types.h"
#include "ecs/components/character_components.h"
#include "ecs/dirty_tracker.h"
#include "ecs/systems/movement_system.h"

namespace {

using mir2::ecs::DirtyComponent;
namespace dirty_tracker = mir2::ecs::dirty_tracker;

}  // namespace

TEST(MovementSystemDirtyTest, SetPositionMarksDirtyFlag) {
    entt::registry registry;
    auto entity = registry.create();

    mir2::ecs::MovementSystem::SetPosition(registry, entity, 5, 6);

    EXPECT_TRUE(dirty_tracker::is_dirty(registry, entity));
    auto* dirty = registry.try_get<DirtyComponent>(entity);
    ASSERT_NE(dirty, nullptr);
    EXPECT_TRUE(dirty->state_dirty);
}

TEST(MovementSystemDirtyTest, SetMapIdMarksDirtyFlag) {
    entt::registry registry;
    auto entity = registry.create();

    mir2::ecs::MovementSystem::SetMapId(registry, entity, 9);

    EXPECT_TRUE(dirty_tracker::is_dirty(registry, entity));
    auto* dirty = registry.try_get<DirtyComponent>(entity);
    ASSERT_NE(dirty, nullptr);
    EXPECT_TRUE(dirty->state_dirty);
}

TEST(MovementSystemDirtyTest, SetDirectionDoesNotMarkDirty) {
    entt::registry registry;
    auto entity = registry.create();

    mir2::ecs::MovementSystem::SetDirection(registry, entity, mir2::common::Direction::UP);

    EXPECT_FALSE(dirty_tracker::is_dirty(registry, entity));
}

TEST(MovementSystemDirtyTest, MultipleSetPositionKeepsDirtyFlag) {
    entt::registry registry;
    auto entity = registry.create();

    mir2::ecs::MovementSystem::SetPosition(registry, entity, 1, 2);
    mir2::ecs::MovementSystem::SetPosition(registry, entity, 3, 4);

    auto* dirty = registry.try_get<DirtyComponent>(entity);
    ASSERT_NE(dirty, nullptr);
    EXPECT_TRUE(dirty->state_dirty);
}
