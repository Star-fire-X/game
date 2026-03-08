#include "logic/handler_context.h"

#include <entt/entt.hpp>

#include "ecs/components/entity_version_component.h"
#include "gtest/gtest.h"

namespace {

using mir2::ecs::EntityVersionComponent;
using mir2::logic::HandlerContext;

TEST(HandlerContextTest, ValidateCacheVersionReturnsFalseWithoutCache) {
  HandlerContext context;
  context.entity = entt::entity{1};
  context.entity_version = 1;
  EXPECT_FALSE(context.ValidateCacheVersion());
}

TEST(HandlerContextTest, ValidateCacheVersionReturnsFalseWithoutRequestedVersion) {
  entt::registry registry;
  const auto entity = registry.create();
  registry.emplace<EntityVersionComponent>(entity, 7);

  HandlerContext context;
  context.entity = entity;
  context.entity_version = 0;
  context.registry = &registry;
  context.world = reinterpret_cast<mir2::ecs::World*>(0x1);
  context.map_id = 100;
  EXPECT_FALSE(context.ValidateCacheVersion());
}

TEST(HandlerContextTest, ValidateCacheVersionReturnsFalseWithoutComponent) {
  entt::registry registry;
  const auto entity = registry.create();

  HandlerContext context;
  context.entity = entity;
  context.entity_version = 3;
  context.registry = &registry;
  context.world = reinterpret_cast<mir2::ecs::World*>(0x1);
  context.map_id = 100;
  EXPECT_FALSE(context.ValidateCacheVersion());
}

TEST(HandlerContextTest, ValidateCacheVersionReturnsTrueWhenVersionMatches) {
  entt::registry registry;
  const auto entity = registry.create();
  registry.emplace<EntityVersionComponent>(entity, 9);

  HandlerContext context;
  context.entity = entity;
  context.entity_version = 9;
  context.registry = &registry;
  context.world = reinterpret_cast<mir2::ecs::World*>(0x1);
  context.map_id = 100;
  EXPECT_TRUE(context.ValidateCacheVersion());
}

TEST(HandlerContextTest, ValidateCacheVersionReturnsFalseWhenVersionMismatch) {
  entt::registry registry;
  const auto entity = registry.create();
  registry.emplace<EntityVersionComponent>(entity, 10);

  HandlerContext context;
  context.entity = entity;
  context.entity_version = 11;
  context.registry = &registry;
  context.world = reinterpret_cast<mir2::ecs::World*>(0x1);
  context.map_id = 100;
  EXPECT_FALSE(context.ValidateCacheVersion());
}

TEST(HandlerContextTest, ValidateCacheVersionReturnsFalseAfterEntityDestroyed) {
  entt::registry registry;
  const auto entity = registry.create();
  registry.emplace<EntityVersionComponent>(entity, 3);

  HandlerContext context;
  context.entity = entity;
  context.entity_version = 3;
  context.registry = &registry;
  context.world = reinterpret_cast<mir2::ecs::World*>(0x1);
  context.map_id = 100;

  registry.destroy(entity);
  EXPECT_FALSE(context.ValidateCacheVersion());
}

}  // namespace
