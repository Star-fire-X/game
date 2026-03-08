/**
 * @file scene_manager_reload_test.cpp
 * @brief SceneManager 地图重载测试
 */

#include "game/map/scene_manager.h"

#include <gtest/gtest.h>

using namespace mir2::game::map;

TEST(SceneManagerReloadTest, ReloadMapRestoresEntities) {
  SceneManager scene_manager;
  SceneManager::MapConfig config{};
  config.map_id = 101;
  config.width = 64;
  config.height = 64;
  config.grid_size = 20;
  config.load_walkability = false;

  ASSERT_NE(scene_manager.CreateMap(config), nullptr);

  entt::entity entity1 = entt::entity{1};
  entt::entity entity2 = entt::entity{2};

  EXPECT_TRUE(scene_manager.AddEntityToMap(config.map_id, entity1, 10, 20));
  EXPECT_TRUE(scene_manager.AddEntityToMap(config.map_id, entity2, 30, 40));

  EXPECT_TRUE(scene_manager.ReloadMap(config.map_id));

  auto* map = scene_manager.GetMap(config.map_id);
  ASSERT_NE(map, nullptr);
  EXPECT_EQ(scene_manager.MapCount(), 1);

  EXPECT_TRUE(map->HasEntity(entity1));
  EXPECT_TRUE(map->HasEntity(entity2));

  int32_t x = 0;
  int32_t y = 0;
  EXPECT_TRUE(map->GetEntityPosition(entity1, x, y));
  EXPECT_EQ(x, 10);
  EXPECT_EQ(y, 20);

  EXPECT_TRUE(map->GetEntityPosition(entity2, x, y));
  EXPECT_EQ(x, 30);
  EXPECT_EQ(y, 40);

  EXPECT_EQ(scene_manager.GetMapByEntity(entity1), map);
  EXPECT_EQ(scene_manager.GetMapByEntity(entity2), map);
}
