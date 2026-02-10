/**
 * @file teleport_system_test.cpp
 * @brief TeleportSystem 单元测试
 */

#include "ecs/systems/teleport_system.h"
#include "ecs/components/character_components.h"
#include "ecs/event_bus.h"
#include "ecs/events/map_events.h"
#include "game/map/scene_manager.h"

#include <gtest/gtest.h>

using namespace mir2::ecs;
using namespace mir2::game::map;

class TeleportSystemTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // 创建两个测试地图
    SceneManager::MapConfig map1_config{1, 100, 100};
    SceneManager::MapConfig map2_config{2, 200, 200};
    map1_config.load_walkability = false;
    map2_config.load_walkability = false;

    scene_manager_.CreateMap(map1_config);
    scene_manager_.CreateMap(map2_config);

    // 创建传送系统
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

// 测试跨地图传送成功
TEST_F(TeleportSystemTest, CrossMapTeleportSuccess) {
  // 创建实体并添加到地图1
  auto entity = registry_.create();
  registry_.emplace<CharacterStateComponent>(entity, uint32_t{1}, mir2::common::Position{10, 10});
  scene_manager_.AddEntityToMap(1, entity, 10, 10);

  // 请求传送到地图2
  TeleportCommand cmd{entity, 2, 50, 50};
  teleport_system_->RequestTeleport(cmd);

  // 执行系统更新
  teleport_system_->Update(registry_, 0.0f);

  // 验证实体已在地图2
  auto* map2 = scene_manager_.GetMapByEntity(entity);
  ASSERT_NE(map2, nullptr);
  EXPECT_EQ(map2->GetMapId(), 2);

  // 验证组件状态已更新
  auto& state = registry_.get<CharacterStateComponent>(entity);
  EXPECT_EQ(state.map_id, 2);
  EXPECT_EQ(state.position.x, 50);
  EXPECT_EQ(state.position.y, 50);
}

// 测试跨地图传送事件发布
TEST_F(TeleportSystemTest, CrossMapTeleportPublishesMapChangeEvent) {
  auto entity = registry_.create();
  registry_.emplace<CharacterStateComponent>(entity, uint32_t{1}, mir2::common::Position{10, 10});
  scene_manager_.AddEntityToMap(1, entity, 10, 10);

  events::MapChangeEvent captured{};
  int publish_count = 0;
  event_bus_.Subscribe<events::MapChangeEvent>(
      [&](events::MapChangeEvent& event) {
        captured = event;
        ++publish_count;
      });

  TeleportCommand cmd{entity, 2, 50, 50};
  teleport_system_->RequestTeleport(cmd);
  teleport_system_->Update(registry_, 0.0f);

  EXPECT_EQ(publish_count, 1);
  EXPECT_EQ(captured.entity, entity);
  EXPECT_EQ(captured.old_map_id, 1);
  EXPECT_EQ(captured.new_map_id, 2);
  EXPECT_EQ(captured.new_x, 50);
  EXPECT_EQ(captured.new_y, 50);
}

// 测试同地图传送（应优化为移动）
TEST_F(TeleportSystemTest, SameMapTeleport) {
  auto entity = registry_.create();
  registry_.emplace<CharacterStateComponent>(entity, uint32_t{1}, mir2::common::Position{10, 10});
  scene_manager_.AddEntityToMap(1, entity, 10, 10);

  // 请求同地图传送
  TeleportCommand cmd{entity, 1, 30, 30};
  teleport_system_->RequestTeleport(cmd);
  teleport_system_->Update(registry_, 0.0f);

  // 验证仍在地图1
  auto* map = scene_manager_.GetMapByEntity(entity);
  EXPECT_EQ(map->GetMapId(), 1);

  // 验证位置已更新
  auto& state = registry_.get<CharacterStateComponent>(entity);
  EXPECT_EQ(state.position.x, 30);
  EXPECT_EQ(state.position.y, 30);
}

// 测试同地图非法坐标传送：状态不应被污染
TEST_F(TeleportSystemTest, SameMapTeleportInvalidPositionKeepsState) {
  auto entity = registry_.create();
  registry_.emplace<CharacterStateComponent>(
      entity, uint32_t{1}, mir2::common::Position{10, 10});
  scene_manager_.AddEntityToMap(1, entity, 10, 10);

  // 地图1尺寸为 100x100，(150,150) 非法
  TeleportCommand cmd{entity, 1, 150, 150};
  teleport_system_->RequestTeleport(cmd);
  teleport_system_->Update(registry_, 0.0f);

  auto* map = scene_manager_.GetMapByEntity(entity);
  ASSERT_NE(map, nullptr);
  EXPECT_EQ(map->GetMapId(), 1);

  int32_t x = 0;
  int32_t y = 0;
  ASSERT_TRUE(map->GetEntityPosition(entity, x, y));
  EXPECT_EQ(x, 10);
  EXPECT_EQ(y, 10);

  const auto& state = registry_.get<CharacterStateComponent>(entity);
  EXPECT_EQ(state.map_id, 1);
  EXPECT_EQ(state.position.x, 10);
  EXPECT_EQ(state.position.y, 10);
}

// 测试传送到不存在的地图
TEST_F(TeleportSystemTest, TeleportToNonExistentMap) {
  auto entity = registry_.create();
  registry_.emplace<CharacterStateComponent>(entity, uint32_t{1}, mir2::common::Position{10, 10});
  scene_manager_.AddEntityToMap(1, entity, 10, 10);

  // 请求传送到不存在的地图999
  TeleportCommand cmd{entity, 999, 50, 50};
  teleport_system_->RequestTeleport(cmd);
  teleport_system_->Update(registry_, 0.0f);

  // 验证实体仍在原地图
  auto* map = scene_manager_.GetMapByEntity(entity);
  EXPECT_EQ(map->GetMapId(), 1);

  auto& state = registry_.get<CharacterStateComponent>(entity);
  EXPECT_EQ(state.map_id, 1);
}

// 测试传送到非法坐标
TEST_F(TeleportSystemTest, TeleportToInvalidPosition) {
  auto entity = registry_.create();
  registry_.emplace<CharacterStateComponent>(entity, uint32_t{1}, mir2::common::Position{10, 10});
  scene_manager_.AddEntityToMap(1, entity, 10, 10);

  // 请求传送到超出边界的坐标
  TeleportCommand cmd{entity, 2, 300, 300};  // 地图2是200x200
  teleport_system_->RequestTeleport(cmd);
  teleport_system_->Update(registry_, 0.0f);

  // 验证传送失败，实体仍在原地图
  auto* map = scene_manager_.GetMapByEntity(entity);
  EXPECT_EQ(map->GetMapId(), 1);
}
