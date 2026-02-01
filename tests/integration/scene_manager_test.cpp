/**
 * @file scene_manager_test.cpp
 * @brief SceneManager 集成测试
 */

#include "game/map/scene_manager.h"

#include <gtest/gtest.h>

#include <vector>

using namespace mir2::game::map;

class SceneManagerTest : public ::testing::Test {
 protected:
  void SetUp() override {
    scene_manager_ = std::make_unique<SceneManager>();
  }

  void TearDown() override {
    scene_manager_.reset();
  }

  std::unique_ptr<SceneManager> scene_manager_;
};

// 测试创建地图
TEST_F(SceneManagerTest, CreateMap) {
  SceneManager::MapConfig config{1, 100, 100};

  auto* map = scene_manager_->CreateMap(config);
  ASSERT_NE(map, nullptr);
  EXPECT_EQ(map->GetMapId(), 1);
  EXPECT_EQ(scene_manager_->MapCount(), 1);
}

// 测试重复创建地图
TEST_F(SceneManagerTest, CreateMapDuplicate) {
  SceneManager::MapConfig config{1, 100, 100};

  auto* map1 = scene_manager_->CreateMap(config);
  ASSERT_NE(map1, nullptr);

  auto* map2 = scene_manager_->CreateMap(config);
  EXPECT_EQ(map2, nullptr);  // 重复创建应失败
  EXPECT_EQ(scene_manager_->MapCount(), 1);
}

// 测试获取或创建地图
TEST_F(SceneManagerTest, GetOrCreateMap) {
  SceneManager::MapConfig config{1, 100, 100};

  auto* map1 = scene_manager_->GetOrCreateMap(config);
  ASSERT_NE(map1, nullptr);
  EXPECT_EQ(scene_manager_->MapCount(), 1);

  auto* map2 = scene_manager_->GetOrCreateMap(config);
  EXPECT_EQ(map1, map2);  // 应返回同一个实例
  EXPECT_EQ(scene_manager_->MapCount(), 1);
}

// 测试获取地图
TEST_F(SceneManagerTest, GetMap) {
  SceneManager::MapConfig config{1, 100, 100};
  scene_manager_->CreateMap(config);

  auto* map = scene_manager_->GetMap(1);
  ASSERT_NE(map, nullptr);
  EXPECT_EQ(map->GetMapId(), 1);

  auto* non_existent = scene_manager_->GetMap(999);
  EXPECT_EQ(non_existent, nullptr);
}

// 测试添加实体到地图
TEST_F(SceneManagerTest, AddEntityToMap) {
  SceneManager::MapConfig config{1, 100, 100};
  scene_manager_->CreateMap(config);

  entt::entity entity = entt::entity{1};
  EXPECT_TRUE(scene_manager_->AddEntityToMap(1, entity, 10, 20));

  auto* map = scene_manager_->GetMap(1);
  EXPECT_TRUE(map->HasEntity(entity));
}

// 测试添加实体到不存在的地图
TEST_F(SceneManagerTest, AddEntityToNonExistentMap) {
  entt::entity entity = entt::entity{1};
  EXPECT_FALSE(scene_manager_->AddEntityToMap(999, entity, 10, 20));
}

// 测试通过实体查找地图
TEST_F(SceneManagerTest, GetMapByEntity) {
  SceneManager::MapConfig config{1, 100, 100};
  scene_manager_->CreateMap(config);

  entt::entity entity = entt::entity{1};
  scene_manager_->AddEntityToMap(1, entity, 10, 20);

  auto* map = scene_manager_->GetMapByEntity(entity);
  ASSERT_NE(map, nullptr);
  EXPECT_EQ(map->GetMapId(), 1);

  entt::entity non_existent = entt::entity{999};
  EXPECT_EQ(scene_manager_->GetMapByEntity(non_existent), nullptr);
}

// 测试从地图移除实体
TEST_F(SceneManagerTest, RemoveEntityFromMap) {
  SceneManager::MapConfig config{1, 100, 100};
  scene_manager_->CreateMap(config);

  entt::entity entity = entt::entity{1};
  scene_manager_->AddEntityToMap(1, entity, 10, 20);

  EXPECT_TRUE(scene_manager_->RemoveEntityFromMap(entity));
  EXPECT_EQ(scene_manager_->GetMapByEntity(entity), nullptr);

  auto* map = scene_manager_->GetMap(1);
  EXPECT_FALSE(map->HasEntity(entity));
}

// 测试更新实体位置
TEST_F(SceneManagerTest, UpdateEntityPosition) {
  SceneManager::MapConfig config{1, 100, 100};
  scene_manager_->CreateMap(config);

  entt::entity entity = entt::entity{1};
  scene_manager_->AddEntityToMap(1, entity, 10, 20);

  EXPECT_TRUE(scene_manager_->UpdateEntityPosition(entity, 30, 40));

  auto* map = scene_manager_->GetMap(1);
  int32_t x, y;
  EXPECT_TRUE(map->GetEntityPosition(entity, x, y));
  EXPECT_EQ(x, 30);
  EXPECT_EQ(y, 40);
}

// 测试多地图管理
TEST_F(SceneManagerTest, MultipleMapManagement) {
  SceneManager::MapConfig config1{1, 100, 100};
  SceneManager::MapConfig config2{2, 200, 200};

  scene_manager_->CreateMap(config1);
  scene_manager_->CreateMap(config2);

  EXPECT_EQ(scene_manager_->MapCount(), 2);

  auto* map1 = scene_manager_->GetMap(1);
  auto* map2 = scene_manager_->GetMap(2);
  ASSERT_NE(map1, nullptr);
  ASSERT_NE(map2, nullptr);
  EXPECT_NE(map1, map2);
}

// 测试实体跨地图移动
TEST_F(SceneManagerTest, EntityCrossMapMovement) {
  SceneManager::MapConfig config1{1, 100, 100};
  SceneManager::MapConfig config2{2, 200, 200};

  scene_manager_->CreateMap(config1);
  scene_manager_->CreateMap(config2);

  entt::entity entity = entt::entity{1};

  // 添加到地图1
  scene_manager_->AddEntityToMap(1, entity, 10, 20);
  EXPECT_EQ(scene_manager_->GetMapByEntity(entity)->GetMapId(), 1);

  // 移动到地图2（自动从地图1移除）
  scene_manager_->AddEntityToMap(2, entity, 50, 60);
  EXPECT_EQ(scene_manager_->GetMapByEntity(entity)->GetMapId(), 2);

  auto* map1 = scene_manager_->GetMap(1);
  auto* map2 = scene_manager_->GetMap(2);
  EXPECT_FALSE(map1->HasEntity(entity));
  EXPECT_TRUE(map2->HasEntity(entity));
}

// 测试销毁地图
TEST_F(SceneManagerTest, DestroyMap) {
  SceneManager::MapConfig config{1, 100, 100};
  scene_manager_->CreateMap(config);

  entt::entity entity = entt::entity{1};
  scene_manager_->AddEntityToMap(1, entity, 10, 20);

  EXPECT_TRUE(scene_manager_->DestroyMap(1));
  EXPECT_EQ(scene_manager_->MapCount(), 0);
  EXPECT_EQ(scene_manager_->GetMap(1), nullptr);
  EXPECT_EQ(scene_manager_->GetMapByEntity(entity), nullptr);
}
