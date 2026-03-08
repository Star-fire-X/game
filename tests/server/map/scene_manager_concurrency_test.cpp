#include <gtest/gtest.h>

#include <atomic>
#include <thread>
#include <vector>

#include "game/map/scene_manager.h"

namespace {

using mir2::game::map::SceneManager;
using mir2::game::map::MapInstance;

void WaitForStart(const std::atomic<bool>& start) {
  while (!start.load(std::memory_order_acquire)) {
    std::this_thread::yield();
  }
}

TEST(SceneManagerConcurrencyTest, ConcurrentReloadAndEntityUpdatesStayConsistent) {
  constexpr int32_t kMapId = 101;
  constexpr int32_t kMapSize = 64;
  constexpr int32_t kEntityCount = 32;
  constexpr int32_t kMoveRounds = 2000;
  constexpr int32_t kReloadRounds = 200;

  SceneManager scene_manager;
  SceneManager::MapConfig config{kMapId, kMapSize, kMapSize};
  config.load_walkability = false;
  ASSERT_NE(scene_manager.CreateMap(config), nullptr);

  std::vector<entt::entity> entities;
  entities.reserve(kEntityCount);
  for (int32_t i = 0; i < kEntityCount; ++i) {
    const auto entity = static_cast<entt::entity>(i + 1);
    entities.push_back(entity);
    ASSERT_TRUE(scene_manager.AddEntityToMap(
        kMapId,
        entity,
        i % kMapSize,
        (i * 3) % kMapSize));
  }

  std::atomic<bool> start{false};
  std::atomic<bool> reload_ok{true};

  std::thread move_thread([&]() {
    WaitForStart(start);
    for (int32_t i = 0; i < kMoveRounds; ++i) {
      const auto entity = entities[static_cast<size_t>(i % kEntityCount)];
      const int32_t x = (i * 5) % kMapSize;
      const int32_t y = (i * 7) % kMapSize;
      if (!scene_manager.UpdateEntityPosition(entity, x, y)) {
        (void)scene_manager.AddEntityToMap(kMapId, entity, x, y);
      }
    }
  });

  std::thread reload_thread([&]() {
    WaitForStart(start);
    for (int32_t i = 0; i < kReloadRounds; ++i) {
      if (!scene_manager.ReloadMap(kMapId)) {
        reload_ok.store(false, std::memory_order_release);
      }
    }
  });

  start.store(true, std::memory_order_release);
  move_thread.join();
  reload_thread.join();

  EXPECT_TRUE(reload_ok.load(std::memory_order_acquire));

  auto* map = scene_manager.GetMap(kMapId);
  ASSERT_NE(map, nullptr);

  for (entt::entity entity : entities) {
    const auto map_id = scene_manager.TryGetEntityMapId(entity);
    if (!map_id.has_value()) {
      continue;
    }
    EXPECT_EQ(*map_id, kMapId);
    EXPECT_TRUE(map->HasEntity(entity));
  }
}

TEST(SceneManagerConcurrencyTest, ConcurrentDestroyRecreateAndMovementNoCrash) {
  constexpr int32_t kMapId = 202;
  constexpr int32_t kMapSize = 48;
  constexpr int32_t kUpdateRounds = 3000;
  constexpr int32_t kRecreateRounds = 250;

  SceneManager scene_manager;
  SceneManager::MapConfig config{kMapId, kMapSize, kMapSize};
  config.load_walkability = false;
  ASSERT_NE(scene_manager.CreateMap(config), nullptr);

  const entt::entity entity = static_cast<entt::entity>(9001);
  ASSERT_TRUE(scene_manager.AddEntityToMap(kMapId, entity, 1, 1));

  std::atomic<bool> start{false};
  std::atomic<bool> mapping_ok{true};

  std::thread update_thread([&]() {
    WaitForStart(start);
    for (int32_t i = 0; i < kUpdateRounds; ++i) {
      const int32_t x = i % kMapSize;
      const int32_t y = (i * 11) % kMapSize;
      if (!scene_manager.UpdateEntityPosition(entity, x, y)) {
        (void)scene_manager.AddEntityToMap(kMapId, entity, x, y);
      }

      const auto map_id = scene_manager.TryGetEntityMapId(entity);
      if (map_id.has_value() && *map_id != kMapId) {
        mapping_ok.store(false, std::memory_order_release);
      }
    }
  });

  std::thread recreate_thread([&]() {
    WaitForStart(start);
    for (int32_t i = 0; i < kRecreateRounds; ++i) {
      (void)scene_manager.DestroyMap(kMapId);
      (void)scene_manager.CreateMap(config);
      (void)scene_manager.AddEntityToMap(
          kMapId,
          entity,
          i % kMapSize,
          (i * 13) % kMapSize);
    }
  });

  start.store(true, std::memory_order_release);
  update_thread.join();
  recreate_thread.join();

  EXPECT_TRUE(mapping_ok.load(std::memory_order_acquire));
  EXPECT_NE(scene_manager.GetMap(kMapId), nullptr);
}

TEST(SceneManagerConcurrencyTest, ConcurrentGetOrCreateMapCreatesSingleInstance) {
  constexpr int32_t kMapId = 303;
  constexpr int32_t kMapSize = 128;
  constexpr int32_t kThreadCount = 24;

  SceneManager scene_manager;
  SceneManager::MapConfig config{kMapId, kMapSize, kMapSize};
  config.load_walkability = false;

  std::atomic<bool> start{false};
  std::vector<MapInstance*> results(static_cast<size_t>(kThreadCount), nullptr);
  std::vector<std::thread> workers;
  workers.reserve(kThreadCount);

  for (int32_t i = 0; i < kThreadCount; ++i) {
    workers.emplace_back([&, i]() {
      WaitForStart(start);
      results[static_cast<size_t>(i)] = scene_manager.GetOrCreateMap(config);
    });
  }

  start.store(true, std::memory_order_release);
  for (auto& worker : workers) {
    worker.join();
  }

  MapInstance* first = results.front();
  ASSERT_NE(first, nullptr);
  for (MapInstance* map : results) {
    EXPECT_EQ(map, first);
  }
  EXPECT_EQ(scene_manager.MapCount(), 1u);
}

}  // namespace
