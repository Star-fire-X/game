/**
 * @file map_instance_test.cpp
 * @brief MapInstance 单元测试
 */

#include "game/map/map_instance.h"

#include <gtest/gtest.h>

#include <vector>

using namespace mir2::game::map;

class MapInstanceTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // 创建一个 100x100 的测试地图
    map_ = std::make_unique<MapInstance>(1, 100, 100);
  }

  void TearDown() override {
    map_.reset();
  }

  std::unique_ptr<MapInstance> map_;
};

// 测试基本属性
TEST_F(MapInstanceTest, BasicProperties) {
  EXPECT_EQ(map_->GetMapId(), 1);
  EXPECT_EQ(map_->EntityCount(), 0);
}

// 测试添加实体
TEST_F(MapInstanceTest, AddEntity) {
  entt::entity entity1 = entt::entity{1};
  entt::entity entity2 = entt::entity{2};

  EXPECT_TRUE(map_->AddEntity(entity1, 10, 20));
  EXPECT_EQ(map_->EntityCount(), 1);
  EXPECT_TRUE(map_->HasEntity(entity1));

  EXPECT_TRUE(map_->AddEntity(entity2, 30, 40));
  EXPECT_EQ(map_->EntityCount(), 2);
  EXPECT_TRUE(map_->HasEntity(entity2));
}

// 测试重复添加实体
TEST_F(MapInstanceTest, AddEntityDuplicate) {
  entt::entity entity = entt::entity{1};

  EXPECT_TRUE(map_->AddEntity(entity, 10, 20));
  EXPECT_FALSE(map_->AddEntity(entity, 30, 40));  // 重复添加应失败
  EXPECT_EQ(map_->EntityCount(), 1);
}

// 测试移除实体
TEST_F(MapInstanceTest, RemoveEntity) {
  entt::entity entity1 = entt::entity{1};
  entt::entity entity2 = entt::entity{2};

  map_->AddEntity(entity1, 10, 20);
  map_->AddEntity(entity2, 30, 40);
  EXPECT_EQ(map_->EntityCount(), 2);

  EXPECT_TRUE(map_->RemoveEntity(entity1));
  EXPECT_EQ(map_->EntityCount(), 1);
  EXPECT_FALSE(map_->HasEntity(entity1));
  EXPECT_TRUE(map_->HasEntity(entity2));

  EXPECT_TRUE(map_->RemoveEntity(entity2));
  EXPECT_EQ(map_->EntityCount(), 0);
}

// 测试移除不存在的实体
TEST_F(MapInstanceTest, RemoveNonExistentEntity) {
  entt::entity entity = entt::entity{1};

  EXPECT_FALSE(map_->RemoveEntity(entity));  // 移除不存在的实体应失败
}

// 测试更新实体位置
TEST_F(MapInstanceTest, UpdateEntityPosition) {
  entt::entity entity = entt::entity{1};

  map_->AddEntity(entity, 10, 20);

  int32_t x, y;
  EXPECT_TRUE(map_->GetEntityPosition(entity, x, y));
  EXPECT_EQ(x, 10);
  EXPECT_EQ(y, 20);

  EXPECT_TRUE(map_->UpdateEntityPosition(entity, 30, 40));
  EXPECT_TRUE(map_->GetEntityPosition(entity, x, y));
  EXPECT_EQ(x, 30);
  EXPECT_EQ(y, 40);
}

// 测试更新不存在实体的位置
TEST_F(MapInstanceTest, UpdateNonExistentEntityPosition) {
  entt::entity entity = entt::entity{1};

  EXPECT_FALSE(map_->UpdateEntityPosition(entity, 30, 40));
}

// 测试获取视野内实体
TEST_F(MapInstanceTest, GetEntitiesInView) {
  entt::entity entity1 = entt::entity{1};
  entt::entity entity2 = entt::entity{2};
  entt::entity entity3 = entt::entity{3};

  // 添加实体到不同位置
  map_->AddEntity(entity1, 10, 10);
  map_->AddEntity(entity2, 15, 15);  // 靠近 entity1
  map_->AddEntity(entity3, 80, 80);  // 远离 entity1

  // 查询 (10, 10) 附近的实体
  auto entities = map_->GetEntitiesInView(10, 10);

  // 应该包含 entity1 和 entity2，但不包含 entity3
  EXPECT_GE(entities.size(), 2);
  EXPECT_TRUE(std::find(entities.begin(), entities.end(), entity1) != entities.end());
  EXPECT_TRUE(std::find(entities.begin(), entities.end(), entity2) != entities.end());
}

// 测试获取实体视野内的其他实体
TEST_F(MapInstanceTest, GetEntitiesInViewOf) {
  entt::entity entity1 = entt::entity{1};
  entt::entity entity2 = entt::entity{2};
  entt::entity entity3 = entt::entity{3};

  map_->AddEntity(entity1, 10, 10);
  map_->AddEntity(entity2, 15, 15);
  map_->AddEntity(entity3, 80, 80);

  // 查询 entity1 视野内的其他实体
  auto entities = map_->GetEntitiesInViewOf(entity1);

  // 应该包含 entity2，但不包含 entity1 自己和 entity3
  EXPECT_TRUE(std::find(entities.begin(), entities.end(), entity1) == entities.end());
  EXPECT_TRUE(std::find(entities.begin(), entities.end(), entity2) != entities.end());
}

// 测试清空地图
TEST_F(MapInstanceTest, Clear) {
  entt::entity entity1 = entt::entity{1};
  entt::entity entity2 = entt::entity{2};

  map_->AddEntity(entity1, 10, 10);
  map_->AddEntity(entity2, 20, 20);
  EXPECT_EQ(map_->EntityCount(), 2);

  map_->Clear();
  EXPECT_EQ(map_->EntityCount(), 0);
  EXPECT_FALSE(map_->HasEntity(entity1));
  EXPECT_FALSE(map_->HasEntity(entity2));
}

// 测试空地图
TEST_F(MapInstanceTest, EmptyMap) {
  EXPECT_EQ(map_->EntityCount(), 0);

  auto entities = map_->GetEntitiesInView(50, 50);
  EXPECT_EQ(entities.size(), 0);
}
