/**
 * @file map_instance_test.cpp
 * @brief MapInstance 单元测试
 */

#include "game/map/map_instance.h"

#include <gtest/gtest.h>

#include <algorithm>
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

TEST_F(MapInstanceTest, AOIEventsAreDeferredUntilDispatch) {
  entt::entity watcher = entt::entity{1};
  entt::entity target = entt::entity{2};

  std::vector<AOIEventType> event_types;
  map_->SetAOICallback([&](AOIEventType event_type,
                           entt::entity watcher_entity,
                           entt::entity target_entity,
                           int32_t x,
                           int32_t y) {
    (void)watcher_entity;
    (void)target_entity;
    (void)x;
    (void)y;
    event_types.push_back(event_type);
  });

  ASSERT_TRUE(map_->AddEntity(watcher, 10, 10));
  ASSERT_TRUE(map_->AddEntity(target, 11, 10));

  EXPECT_TRUE(event_types.empty());
  EXPECT_GT(map_->PendingAOIEventCount(), 0u);

  const size_t dispatched = map_->DispatchPendingAOIEvents();
  EXPECT_EQ(dispatched, event_types.size());
  EXPECT_EQ(dispatched, 2u);
  EXPECT_EQ(map_->PendingAOIEventCount(), 0u);
}

TEST_F(MapInstanceTest, AOIQueueOverflowDoesNotDropDeltas) {
  constexpr int32_t kBurstEntities = 100;

  size_t delivered_events = 0;
  map_->SetAOICallback([&](AOIEventType event_type,
                           entt::entity watcher_entity,
                           entt::entity target_entity,
                           int32_t x,
                           int32_t y) {
    (void)event_type;
    (void)watcher_entity;
    (void)target_entity;
    (void)x;
    (void)y;
    ++delivered_events;
  });

  const entt::entity watcher = entt::entity{1};
  ASSERT_TRUE(map_->AddEntity(watcher, 10, 10));

  for (int32_t i = 0; i < kBurstEntities; ++i) {
    const entt::entity target = static_cast<entt::entity>(100 + i);
    const int32_t x = 10 + (i % 10);
    const int32_t y = 10 + (i / 10);
    ASSERT_TRUE(map_->AddEntity(target, x, y));
  }

  const size_t expected_events =
      static_cast<size_t>(kBurstEntities) *
      static_cast<size_t>(kBurstEntities + 1);
  EXPECT_GT(map_->PendingAOIEventCount(), 8192u);

  const size_t dispatched_events = map_->DispatchPendingAOIEvents();
  EXPECT_EQ(dispatched_events, expected_events);
  EXPECT_EQ(delivered_events, expected_events);
  EXPECT_EQ(map_->PendingAOIEventCount(), 0u);
}

TEST_F(MapInstanceTest, AOICappedDispatchDrainsOverflowBeforeBudgetExhaustion) {
  constexpr int32_t kWatcherCount = 64;
  constexpr size_t kRingCapacity = 8192;
  constexpr size_t kDispatchCap = 4096;
  constexpr int32_t kMoveIterations = 128;

  std::vector<AOIEventType> first_batch_event_types;
  map_->SetAOICallback([&](AOIEventType event_type,
                           entt::entity watcher_entity,
                           entt::entity target_entity,
                           int32_t x,
                           int32_t y) {
    (void)watcher_entity;
    (void)target_entity;
    (void)x;
    (void)y;
    first_batch_event_types.push_back(event_type);
  });

  const entt::entity mover = entt::entity{1};
  ASSERT_TRUE(map_->AddEntity(mover, 10, 10));
  for (int32_t i = 0; i < kWatcherCount; ++i) {
    const entt::entity watcher = static_cast<entt::entity>(100 + i);
    ASSERT_TRUE(map_->AddEntity(watcher, 12 + (i % 8), 12 + (i / 8)));
  }

  // Reset queue to keep only synthetic move/leave burst below.
  map_->DispatchPendingAOIEvents();
  first_batch_event_types.clear();

  for (int32_t i = 0; i < kMoveIterations; ++i) {
    const int32_t x = (i % 2 == 0) ? 10 : 11;
    ASSERT_TRUE(map_->UpdateEntityPosition(mover, x, 10));
  }

  ASSERT_TRUE(map_->RemoveEntity(mover));
  ASSERT_GT(map_->PendingAOIEventCount(), kRingCapacity);

  const size_t dispatched = map_->DispatchPendingAOIEvents(kDispatchCap);
  EXPECT_EQ(dispatched, kDispatchCap);
  EXPECT_EQ(first_batch_event_types.size(), dispatched);
  EXPECT_GT(std::count(first_batch_event_types.begin(),
                       first_batch_event_types.end(),
                       AOIEventType::kLeave),
            0);
}

TEST_F(MapInstanceTest, AOILeaveEventUsesLeaverLatestCoordinates) {
  entt::entity watcher = entt::entity{1};
  entt::entity leaver = entt::entity{2};

  struct CapturedEvent {
    AOIEventType type;
    entt::entity watcher;
    entt::entity target;
    int32_t x;
    int32_t y;
  };

  std::vector<CapturedEvent> events;
  map_->SetAOICallback([&](AOIEventType event_type,
                           entt::entity watcher_entity,
                           entt::entity target_entity,
                           int32_t x,
                           int32_t y) {
    events.push_back({event_type, watcher_entity, target_entity, x, y});
  });

  ASSERT_TRUE(map_->AddEntity(watcher, 10, 10));
  ASSERT_TRUE(map_->AddEntity(leaver, 11, 10));
  map_->DispatchPendingAOIEvents();

  events.clear();
  ASSERT_TRUE(map_->UpdateEntityPosition(leaver, 90, 90));
  map_->DispatchPendingAOIEvents();

  auto leave_it = std::find_if(
      events.begin(), events.end(), [&](const CapturedEvent& event) {
        return event.type == AOIEventType::kLeave &&
               event.watcher == watcher &&
               event.target == leaver;
      });
  ASSERT_TRUE(leave_it != events.end());
  EXPECT_EQ(leave_it->x, 90);
  EXPECT_EQ(leave_it->y, 90);
}
