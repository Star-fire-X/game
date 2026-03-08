/**
 * @file map_loader_test.cpp
 * @brief MapLoader 单元测试
 */

#include "game/map/map_loader.h"

#include <gtest/gtest.h>

#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

struct TileDef {
  uint16_t background = 0;
  uint16_t object = 0;
  uint8_t door_index = 0;
  uint8_t door_offset = 0;
};

class TempMapFile {
 public:
  explicit TempMapFile(const std::string& filename) {
    const std::filesystem::path map_dir = std::filesystem::current_path() / "Map";
    std::error_code ec;
    std::filesystem::create_directories(map_dir, ec);
    path_ = map_dir / filename;
  }

  ~TempMapFile() { std::error_code ec; std::filesystem::remove(path_, ec); }

  const std::filesystem::path& path() const { return path_; }

 private:
  std::filesystem::path path_;
};

TempMapFile WriteTestMapFile(int32_t width, int32_t height,
                             const std::vector<TileDef>& tiles) {
  const auto stamp =
      static_cast<long long>(std::chrono::steady_clock::now()
                                 .time_since_epoch()
                                 .count());
  TempMapFile temp("map_loader_test_" + std::to_string(stamp) + ".map");
  std::ofstream out(temp.path(), std::ios::binary);
  EXPECT_TRUE(out.is_open());

  std::vector<uint8_t> header(52, 0);
  header[0] = static_cast<uint8_t>(width & 0xFF);
  header[1] = static_cast<uint8_t>((width >> 8) & 0xFF);
  header[2] = static_cast<uint8_t>(height & 0xFF);
  header[3] = static_cast<uint8_t>((height >> 8) & 0xFF);
  out.write(reinterpret_cast<const char*>(header.data()),
            static_cast<std::streamsize>(header.size()));

  if (tiles.size() != static_cast<size_t>(width * height)) {
    ADD_FAILURE() << "tiles size mismatch";
    return temp;
  }

  std::array<uint8_t, 12> buffer{};
  for (int32_t x = 0; x < width; ++x) {
    for (int32_t y = 0; y < height; ++y) {
      const TileDef& tile = tiles[static_cast<size_t>(y * width + x)];
      buffer.fill(0);
      buffer[0] = static_cast<uint8_t>(tile.background & 0xFF);
      buffer[1] = static_cast<uint8_t>((tile.background >> 8) & 0xFF);
      buffer[4] = static_cast<uint8_t>(tile.object & 0xFF);
      buffer[5] = static_cast<uint8_t>((tile.object >> 8) & 0xFF);
      buffer[6] = tile.door_index;
      buffer[7] = tile.door_offset;
      out.write(reinterpret_cast<const char*>(buffer.data()),
                static_cast<std::streamsize>(buffer.size()));
    }
  }

  return temp;
}

}  // namespace

TEST(MapLoaderTest, LoadWalkability) {
  constexpr int32_t kWidth = 3;
  constexpr int32_t kHeight = 2;
  std::vector<TileDef> tiles(static_cast<size_t>(kWidth * kHeight));
  tiles[0].background = 0x8000;  // (0,0) blocked by background
  tiles[1].object = 0x8000;      // (1,0) blocked by object
  tiles[2].door_index = 0x81;    // (2,0) door exists
  tiles[2].door_offset = 0x00;   // closed
  tiles[5].door_index = 0x81;    // (2,1) door exists
  tiles[5].door_offset = 0x80;   // open

  TempMapFile temp = WriteTestMapFile(kWidth, kHeight, tiles);

  mir2::game::map::MapLoader loader;
  auto map = loader.Load(temp.path().string());
  ASSERT_TRUE(map.has_value());
  EXPECT_EQ(map->width, kWidth);
  EXPECT_EQ(map->height, kHeight);

  EXPECT_FALSE(map->IsWalkable(0, 0));
  EXPECT_FALSE(map->IsWalkable(1, 0));
  EXPECT_FALSE(map->IsWalkable(2, 0));
  EXPECT_TRUE(map->IsWalkable(0, 1));
  EXPECT_TRUE(map->IsWalkable(1, 1));
  EXPECT_TRUE(map->IsWalkable(2, 1));
  EXPECT_FALSE(map->IsWalkable(-1, 0));
  EXPECT_FALSE(map->IsWalkable(0, 2));
}

TEST(MapLoaderTest, LoadFullWalkabilityMatchesLoadRules) {
  constexpr int32_t kWidth = 3;
  constexpr int32_t kHeight = 2;
  std::vector<TileDef> tiles(static_cast<size_t>(kWidth * kHeight));
  tiles[0].background = 0x8000;  // (0,0) blocked by background
  tiles[1].object = 0x8000;      // (1,0) blocked by object
  tiles[2].door_index = 0x81;    // (2,0) door exists
  tiles[2].door_offset = 0x00;   // closed
  tiles[5].door_index = 0x81;    // (2,1) door exists
  tiles[5].door_offset = 0x80;   // open

  TempMapFile temp = WriteTestMapFile(kWidth, kHeight, tiles);

  mir2::game::map::MapLoader loader;
  auto walkability_only = loader.Load(temp.path().string());
  ASSERT_TRUE(walkability_only.has_value());

  auto full = loader.LoadFull(temp.path().string());
  ASSERT_TRUE(full.has_value());
  EXPECT_EQ(full->width, kWidth);
  EXPECT_EQ(full->height, kHeight);

  for (int32_t y = 0; y < kHeight; ++y) {
    for (int32_t x = 0; x < kWidth; ++x) {
      EXPECT_EQ(full->IsWalkable(x, y), walkability_only->IsWalkable(x, y))
          << "Mismatch at (" << x << "," << y << ")";
    }
  }

  EXPECT_FALSE(full->IsWalkable(0, 0));
  EXPECT_FALSE(full->IsWalkable(1, 0));
  EXPECT_FALSE(full->IsWalkable(2, 0));
  EXPECT_TRUE(full->IsWalkable(2, 1));
}

TEST(MapLoaderTest, RejectsInvalidFile) {
  TempMapFile temp("map_loader_invalid.map");
  std::ofstream out(temp.path(), std::ios::binary);
  ASSERT_TRUE(out.is_open());
  const uint8_t data[2] = {0, 0};
  out.write(reinterpret_cast<const char*>(data),
            static_cast<std::streamsize>(sizeof(data)));
  out.close();

  mir2::game::map::MapLoader loader;
  auto map = loader.Load(temp.path().string());
  EXPECT_FALSE(map.has_value());
}

TEST(MapLoaderTest, LoadFullMapsColumnMajorInputToExpectedCoordinates) {
  constexpr int32_t kWidth = 3;
  constexpr int32_t kHeight = 2;
  std::vector<TileDef> tiles(static_cast<size_t>(kWidth * kHeight));

  // tiles[y * width + x]
  tiles[0].background = 100;  // (0,0)
  tiles[1].background = 101;  // (1,0)
  tiles[2].background = 102;  // (2,0)
  tiles[3].background = 200;  // (0,1)
  tiles[4].background = 201;  // (1,1)
  tiles[5].background = 202;  // (2,1)

  TempMapFile temp = WriteTestMapFile(kWidth, kHeight, tiles);

  mir2::game::map::MapLoader loader;
  auto full = loader.LoadFull(temp.path().string());
  ASSERT_TRUE(full.has_value());

  const auto* tile_00 = full->GetTile(0, 0);
  const auto* tile_10 = full->GetTile(1, 0);
  const auto* tile_20 = full->GetTile(2, 0);
  const auto* tile_01 = full->GetTile(0, 1);
  const auto* tile_11 = full->GetTile(1, 1);
  const auto* tile_21 = full->GetTile(2, 1);

  ASSERT_NE(tile_00, nullptr);
  ASSERT_NE(tile_10, nullptr);
  ASSERT_NE(tile_20, nullptr);
  ASSERT_NE(tile_01, nullptr);
  ASSERT_NE(tile_11, nullptr);
  ASSERT_NE(tile_21, nullptr);

  EXPECT_EQ(tile_00->bk_img, 100);
  EXPECT_EQ(tile_10->bk_img, 101);
  EXPECT_EQ(tile_20->bk_img, 102);
  EXPECT_EQ(tile_01->bk_img, 200);
  EXPECT_EQ(tile_11->bk_img, 201);
  EXPECT_EQ(tile_21->bk_img, 202);
}
