#include <gtest/gtest.h>

#include <entt/entt.hpp>

#include "game/map/map_context_service.h"
#include "game/map/scene_manager.h"

namespace {

using mir2::game::map::MapContextService;
using mir2::game::map::MapInstance;
using mir2::game::map::SceneManager;

SceneManager::MapConfig BuildStaticMapConfig(int32_t map_id) {
  SceneManager::MapConfig config;
  config.map_id = map_id;
  config.width = 16;
  config.height = 16;
  config.load_walkability = false;
  return config;
}

TEST(MapContextServiceTest, BindRegistryAssociatesRegistryWithMap) {
  SceneManager scene_manager;
  auto config = BuildStaticMapConfig(/*map_id=*/1001);
  MapInstance* map = scene_manager.GetOrCreateMap(config);
  ASSERT_NE(map, nullptr);

  MapContextService service(scene_manager);
  entt::registry registry;

  service.BindRegistry(registry, /*map_id=*/1001);

  EXPECT_EQ(service.GetMap(static_cast<uint32_t>(1001)), map);
  EXPECT_EQ(service.GetMap(registry), map);

  auto* map_ctx = registry.ctx().find<MapInstance*>();
  ASSERT_NE(map_ctx, nullptr);
  EXPECT_EQ(*map_ctx, map);
}

TEST(MapContextServiceTest, GetMapByRegistryReturnsNullWhenUnbound) {
  SceneManager scene_manager;
  MapContextService service(scene_manager);
  entt::registry registry;

  EXPECT_EQ(service.GetMap(registry), nullptr);
}

TEST(MapContextServiceTest, BindRegistryPublishesNullForMissingMap) {
  SceneManager scene_manager;
  MapContextService service(scene_manager);
  entt::registry registry;

  service.BindRegistry(registry, /*map_id=*/4040);

  auto* map_ctx = registry.ctx().find<MapInstance*>();
  ASSERT_NE(map_ctx, nullptr);
  EXPECT_EQ(*map_ctx, nullptr);
  EXPECT_EQ(service.GetMap(registry), nullptr);
}

}  // namespace
