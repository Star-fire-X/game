/**
 * @file cross_map_test.cpp
 * @brief 跨地图传送集成测试
 */

#include "ecs/systems/teleport_system.h"
#include "ecs/components/character_components.h"
#include "ecs/event_bus.h"
#include "game/map/scene_manager.h"

#include <gtest/gtest.h>

using namespace mir2::ecs;
using namespace mir2::game::map;

class CrossMapTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // 创建三个测试地图
    SceneManager::MapConfig map1{1, 500, 500};
    SceneManager::MapConfig map2{2, 300, 300};
    SceneManager::MapConfig map3{3, 200, 200};

    scene_manager_.CreateMap(map1);
    scene_manager_.CreateMap(map2);
    scene_manager_.CreateMap(map3);

    teleport_system_ = std::make_unique<TeleportSystem>(scene_manager_, event_bus_);
  }

  void TearDown() override {
    teleport_system_.reset();
    scene_manager_.Clear();
  }

  entt::registry registry_;
  EventBus event_bus_{registry_};
  SceneManager scene_manager_;
  std::unique_ptr<TeleportSystem> teleport_system_;
};

// 测试角色跨地图传送后状态一致性
TEST_F(CrossMapTest, StateConsistencyAfterTeleport) {
  auto entity = registry_.create();
  auto& identity = registry_.emplace<CharacterIdentityComponent>(entity);
  identity.id = 10001;
  identity.name = "TestPlayer";

  auto& state = registry_.emplace<CharacterStateComponent>(entity);
  state.map_id = 1;
  state.position = {100, 100};

  scene_manager_.AddEntityToMap(1, entity, 100, 100);

  // 传送到地图2
  TeleportCommand cmd{entity, 2, 50, 50};
  teleport_system_->RequestTeleport(cmd);
  teleport_system_->Update(registry_, 0.0f);

  // 验证状态一致性
  EXPECT_EQ(state.map_id, 2);
  EXPECT_EQ(state.position.x, 50);
  EXPECT_EQ(state.position.y, 50);
  EXPECT_EQ(identity.name, "TestPlayer");
}

// 测试100次传送无状态丢失
TEST_F(CrossMapTest, MultipleTeleportsNoDataLoss) {
  auto entity = registry_.create();
  auto& identity = registry_.emplace<CharacterIdentityComponent>(entity);
  identity.id = 10001;
  identity.name = "TestPlayer";

  auto& state = registry_.emplace<CharacterStateComponent>(entity);
  state.map_id = 1;
  state.position = {100, 100};

  scene_manager_.AddEntityToMap(1, entity, 100, 100);

  // 执行100次传送
  for (int i = 0; i < 100; ++i) {
    int target_map = (i % 2) + 1;  // 在地图1和2之间切换
    TeleportCommand cmd{entity, target_map, 50 + i, 50 + i};
    teleport_system_->RequestTeleport(cmd);
    teleport_system_->Update(registry_, 0.0f);
  }

  // 验证数据完整性
  EXPECT_EQ(identity.id, 10001);
  EXPECT_EQ(identity.name, "TestPlayer");
  EXPECT_TRUE(registry_.valid(entity));
}
