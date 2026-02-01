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
  explicit TempMapFile(const std::string& filename)
      : path_(std::filesystem::temp_directory_path() / filename) {}

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
