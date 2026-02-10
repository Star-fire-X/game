#include <gtest/gtest.h>

#include <atomic>
#include <thread>
#include <vector>

#include "game/map/map_instance.h"

namespace {

using mir2::game::map::MapInstance;

void WaitForStart(const std::atomic<bool>& start) {
  while (!start.load(std::memory_order_acquire)) {
    std::this_thread::yield();
  }
}

TEST(MapInstanceConcurrencyTest, ConcurrentAddRemoveKeepsEntityStateConsistent) {
  constexpr int32_t kMapSize = 128;
  constexpr int32_t kRounds = 800;
  constexpr int32_t kEntityCount = 48;

  MapInstance map(/*map_id=*/1, kMapSize, kMapSize);

  std::vector<entt::entity> entities;
  entities.reserve(kEntityCount);
  for (int32_t i = 0; i < kEntityCount; ++i) {
    entities.push_back(static_cast<entt::entity>(i + 1));
  }

  std::atomic<bool> start{false};

  std::thread add_thread([&]() {
    WaitForStart(start);
    for (int32_t round = 0; round < kRounds; ++round) {
      for (int32_t i = 0; i < kEntityCount; ++i) {
        const int32_t x = (round + i) % kMapSize;
        const int32_t y = (round * 3 + i * 5) % kMapSize;
        (void)map.AddEntity(entities[static_cast<size_t>(i)], x, y);
      }
    }
  });

  std::thread remove_thread([&]() {
    WaitForStart(start);
    for (int32_t round = 0; round < kRounds; ++round) {
      for (int32_t i = 0; i < kEntityCount; ++i) {
        if (((round + i) & 1) == 0) {
          (void)map.RemoveEntity(entities[static_cast<size_t>(i)]);
        }
      }
    }
  });

  start.store(true, std::memory_order_release);
  add_thread.join();
  remove_thread.join();

  size_t counted_entities = 0;
  for (entt::entity entity : entities) {
    const bool has_entity = map.HasEntity(entity);
    int32_t x = 0;
    int32_t y = 0;
    const bool has_position = map.GetEntityPosition(entity, x, y);

    EXPECT_EQ(has_entity, has_position);
    if (has_entity) {
      ++counted_entities;
      EXPECT_TRUE(map.IsValidPosition(x, y));
    }
  }

  EXPECT_EQ(map.EntityCount(), counted_entities);
}

TEST(MapInstanceConcurrencyTest, ConcurrentMoveAndToggleDoesNotLeaveGhostState) {
  constexpr int32_t kMapSize = 96;
  constexpr int32_t kRounds = 3000;

  MapInstance map(/*map_id=*/2, kMapSize, kMapSize);
  const entt::entity entity = static_cast<entt::entity>(1001);
  ASSERT_TRUE(map.AddEntity(entity, 1, 1));

  std::atomic<bool> start{false};

  std::thread move_thread([&]() {
    WaitForStart(start);
    for (int32_t i = 0; i < kRounds; ++i) {
      const int32_t x = i % kMapSize;
      const int32_t y = (i * 7) % kMapSize;
      if (!map.UpdateEntityPosition(entity, x, y)) {
        (void)map.AddEntity(entity, x, y);
      }
    }
  });

  std::thread toggle_thread([&]() {
    WaitForStart(start);
    for (int32_t i = 0; i < kRounds; ++i) {
      if ((i & 1) == 0) {
        (void)map.RemoveEntity(entity);
      } else {
        const int32_t x = (i * 3) % kMapSize;
        const int32_t y = (i * 11) % kMapSize;
        (void)map.AddEntity(entity, x, y);
      }
    }
  });

  start.store(true, std::memory_order_release);
  move_thread.join();
  toggle_thread.join();

  int32_t x = 0;
  int32_t y = 0;
  const bool has_entity = map.HasEntity(entity);
  const bool has_position = map.GetEntityPosition(entity, x, y);

  EXPECT_EQ(has_entity, has_position);
  EXPECT_LE(map.EntityCount(), static_cast<size_t>(1));
  if (has_entity) {
    EXPECT_TRUE(map.IsValidPosition(x, y));
  }
}

}  // namespace
