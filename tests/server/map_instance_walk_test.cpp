/**
 * @file map_instance_walk_test.cpp
 * @brief MapInstance walkability 单元测试
 */

#include "game/map/map_instance.h"

#include <gtest/gtest.h>

#include <atomic>
#include <thread>
#include <vector>

using namespace mir2::game::map;

TEST(MapInstanceWalkTest, WalkabilityAndBounds) {
  std::vector<uint8_t> walkability = {1, 0, 1, 1};
  MapInstance map(1, 2, 2, AOIManager::kDefaultGridSize,
                  std::move(walkability));

  EXPECT_TRUE(map.IsWalkable(0, 0));
  EXPECT_FALSE(map.IsWalkable(1, 0));
  EXPECT_TRUE(map.IsWalkable(0, 1));
  EXPECT_TRUE(map.IsWalkable(1, 1));

  EXPECT_FALSE(map.IsWalkable(-1, 0));
  EXPECT_FALSE(map.IsWalkable(0, -1));
  EXPECT_FALSE(map.IsWalkable(2, 0));
  EXPECT_FALSE(map.IsWalkable(0, 2));
}

TEST(MapInstanceWalkTest, ConcurrentReadsConsistent) {
  std::vector<uint8_t> walkability = {1, 0, 1, 1, 0, 1};
  MapInstance map(2, 3, 2, AOIManager::kDefaultGridSize,
                  std::move(walkability));

  std::atomic<bool> ok{true};
  auto worker = [&]() {
    for (int i = 0; i < 1000; ++i) {
      if (!map.IsWalkable(0, 0) || map.IsWalkable(1, 0) ||
          !map.IsWalkable(2, 0) || !map.IsWalkable(0, 1) ||
          map.IsWalkable(1, 1) || !map.IsWalkable(2, 1)) {
        ok.store(false);
        return;
      }
    }
  };

  std::vector<std::thread> threads;
  threads.reserve(8);
  for (int i = 0; i < 8; ++i) {
    threads.emplace_back(worker);
  }
  for (auto& thread : threads) {
    thread.join();
  }

  EXPECT_TRUE(ok.load());
}

TEST(MapInstanceWalkTest, TileDataWalkabilityIsSingleSourceOfTruth) {
  MapTileData tile_data;
  tile_data.width = 2;
  tile_data.height = 2;
  tile_data.tiles.assign(4, TileInfo{});
  tile_data.walkable = {0, 0, 0, 0};

  std::vector<uint8_t> fallback_walkability = {1, 1, 1, 1};
  MapInstance map(3, 2, 2, AOIManager::kDefaultGridSize,
                  std::move(fallback_walkability), std::move(tile_data));

  // 当存在 tile_data 时，MapInstance 应该只使用 tile_data.walkable。
  EXPECT_FALSE(map.IsWalkable(0, 0));
  EXPECT_FALSE(map.IsWalkable(1, 1));

  map.SetWalkable(1, 1, true);
  EXPECT_TRUE(map.IsWalkable(1, 1));
  EXPECT_FALSE(map.IsWalkable(0, 0));
}
