#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "client/resource/resource_loader.h"

namespace {

using mir2::client::LRUCache;
using mir2::client::MapData;
using mir2::client::MapLoader;
using mir2::client::MapTile;
using mir2::client::Sprite;
using mir2::client::WilArchive;

void WriteLE16(std::vector<uint8_t>& data, size_t offset, uint16_t value) {
    data[offset] = static_cast<uint8_t>(value & 0xFFu);
    data[offset + 1] = static_cast<uint8_t>((value >> 8) & 0xFFu);
}

void WriteLE32(std::vector<uint8_t>& data, size_t offset, uint32_t value) {
    data[offset] = static_cast<uint8_t>(value & 0xFFu);
    data[offset + 1] = static_cast<uint8_t>((value >> 8) & 0xFFu);
    data[offset + 2] = static_cast<uint8_t>((value >> 16) & 0xFFu);
    data[offset + 3] = static_cast<uint8_t>((value >> 24) & 0xFFu);
}

std::filesystem::path MakeTempPath(const std::string& suffix) {
    static std::atomic<uint64_t> counter{0};
    const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    const uint64_t id = counter.fetch_add(1);
    const std::string name = "mir2_resource_test_" + std::to_string(now) + "_" + std::to_string(id) + suffix;
    return std::filesystem::temp_directory_path() / name;
}

bool WriteFile(const std::filesystem::path& path, const std::vector<uint8_t>& data) {
    std::ofstream file(path, std::ios::binary);
    if (!file.is_open()) {
        return false;
    }
    if (!data.empty()) {
        file.write(reinterpret_cast<const char*>(data.data()),
                   static_cast<std::streamsize>(data.size()));
    }
    return file.good();
}

class TempFile {
public:
    TempFile(const std::string& suffix, const std::vector<uint8_t>& data)
        : path_(MakeTempPath(suffix)), ok_(WriteFile(path_, data)) {}

    ~TempFile() {
        std::error_code ec;
        std::filesystem::remove(path_, ec);
    }

    const std::filesystem::path& path() const { return path_; }
    bool ok() const { return ok_; }

private:
    std::filesystem::path path_;
    bool ok_ = false;
};

struct TempWilWixFiles {
    std::filesystem::path wil_path;
    std::filesystem::path wix_path;
    bool ok = false;

    TempWilWixFiles(const std::vector<uint8_t>& wil_data,
                    const std::vector<uint8_t>& wix_data) {
        const auto stem = MakeTempPath("");
        wil_path = stem;
        wil_path += ".wil";
        wix_path = stem;
        wix_path += ".WIX";
        ok = WriteFile(wil_path, wil_data) && WriteFile(wix_path, wix_data);
    }

    ~TempWilWixFiles() {
        std::error_code ec;
        std::filesystem::remove(wil_path, ec);
        std::filesystem::remove(wix_path, ec);
    }
};

std::vector<uint8_t> MakeLegacyWixHeader(int32_t index_count, int32_t ver_flag) {
    std::vector<uint8_t> data(mir2::client::WIX_TITLE_SIZE + 2 * sizeof(int32_t), 0);
    data[0] = 40;
    const char* signature = "WEMADE";
    std::memcpy(data.data() + 1, signature, std::strlen(signature));
    WriteLE32(data, mir2::client::WIX_TITLE_SIZE, static_cast<uint32_t>(index_count));
    WriteLE32(data, mir2::client::WIX_TITLE_SIZE + sizeof(int32_t), static_cast<uint32_t>(ver_flag));
    return data;
}

std::vector<uint8_t> MakeIlibWil(int32_t image_count, int32_t color_count) {
    constexpr size_t kIlibHeaderSize = 36 + 5 * sizeof(int32_t);
    std::vector<uint8_t> data(kIlibHeaderSize + 256 * 4, 0);
    const char* signature = "#ILIB";
    std::memcpy(data.data(), signature, std::strlen(signature));
    WriteLE32(data, 36, 1u);
    WriteLE32(data, 40, 0u);
    WriteLE32(data, 44, static_cast<uint32_t>(image_count));
    WriteLE32(data, 48, static_cast<uint32_t>(color_count));
    WriteLE32(data, 52, 0u);
    return data;
}

std::vector<uint8_t> MakeMapFileBytes(int width, int height, int header_size,
                                      int tile_size,
                                      const std::vector<uint8_t>& tile_data) {
    const size_t tile_count = static_cast<size_t>(width) * static_cast<size_t>(height);
    std::vector<uint8_t> data(static_cast<size_t>(header_size) + tile_count * static_cast<size_t>(tile_size), 0);
    WriteLE16(data, 0, static_cast<uint16_t>(width));
    WriteLE16(data, 2, static_cast<uint16_t>(height));
    if (!tile_data.empty()) {
        const size_t copy_size = std::min(tile_data.size(), static_cast<size_t>(tile_size));
        std::copy_n(tile_data.begin(), copy_size, data.begin() + header_size);
    }
    return data;
}

}  // namespace

TEST(LRUCacheTest, BasicPutGet) {
    LRUCache<int, std::string> cache(2);
    cache.put(1, "one");
    cache.put(2, "two");

    auto value = cache.get(1);
    ASSERT_TRUE(value.has_value());
    EXPECT_EQ(*value, "one");
    EXPECT_EQ(cache.size(), 2u);
}

TEST(LRUCacheTest, EvictsLRUWhenFull) {
    LRUCache<int, int> cache(2);
    cache.put(1, 10);
    cache.put(2, 20);
    cache.put(3, 30);

    EXPECT_FALSE(cache.contains(1));
    EXPECT_TRUE(cache.contains(2));
    EXPECT_TRUE(cache.contains(3));
}

TEST(LRUCacheTest, GetUpdatesAccessOrder) {
    LRUCache<int, int> cache(2);
    cache.put(1, 10);
    cache.put(2, 20);

    ASSERT_TRUE(cache.get(1).has_value());
    cache.put(3, 30);

    EXPECT_TRUE(cache.contains(1));
    EXPECT_FALSE(cache.contains(2));
    EXPECT_TRUE(cache.contains(3));
}

TEST(LRUCacheTest, RemoveWorks) {
    LRUCache<int, int> cache(2);
    cache.put(1, 10);
    cache.put(2, 20);

    EXPECT_TRUE(cache.remove(1));
    EXPECT_FALSE(cache.remove(1));
    EXPECT_FALSE(cache.get(1).has_value());
    EXPECT_EQ(cache.size(), 1u);
}

TEST(LRUCacheTest, SetCapacityEvictsExcess) {
    LRUCache<int, int> cache(3);
    cache.put(1, 10);
    cache.put(2, 20);
    cache.put(3, 30);

    cache.set_capacity(2);

    EXPECT_EQ(cache.size(), 2u);
    EXPECT_FALSE(cache.contains(1));
    EXPECT_TRUE(cache.contains(2));
    EXPECT_TRUE(cache.contains(3));
}

TEST(LRUCacheTest, ZeroCapacityBecomesOne) {
    LRUCache<int, int> cache(0);
    EXPECT_EQ(cache.capacity(), 1u);

    cache.put(1, 10);
    cache.put(2, 20);

    EXPECT_EQ(cache.capacity(), 1u);
    EXPECT_EQ(cache.size(), 1u);
    EXPECT_FALSE(cache.contains(1));
    EXPECT_TRUE(cache.contains(2));

    cache.set_capacity(0);
    EXPECT_EQ(cache.capacity(), 1u);
}

TEST(SpriteTest, IsValidChecksPixelCount) {
    Sprite sprite;
    sprite.width = 2;
    sprite.height = 2;
    sprite.pixels.assign(4, 0xFF00FF00);

    EXPECT_TRUE(sprite.is_valid());

    sprite.pixels.resize(3);
    EXPECT_FALSE(sprite.is_valid());
}

TEST(SpriteTest, SerializeDeserializeRoundTrip) {
    Sprite sprite;
    sprite.width = 2;
    sprite.height = 1;
    sprite.offset_x = -3;
    sprite.offset_y = 5;
    sprite.pixels = {0xFF112233, 0xFF445566};

    const std::string data = sprite.serialize();
    auto decoded = Sprite::deserialize(data);

    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(*decoded, sprite);
}

TEST(MapTileTest, IsWalkableWithBlockingBackground) {
    MapTile tile;
    tile.background = 0x8000;

    EXPECT_FALSE(tile.is_walkable());
}

TEST(MapTileTest, IsWalkableWithBlockingObject) {
    MapTile tile;
    tile.object = 0x8000;

    EXPECT_FALSE(tile.is_walkable());
}

TEST(MapTileTest, IsWalkableWithClosedDoor) {
    MapTile tile;
    tile.door_index = static_cast<uint8_t>(0x80 | 0x01);
    tile.door_offset = 0x00;

    EXPECT_FALSE(tile.is_walkable());
}

TEST(MapTileTest, HasPortal) {
    MapTile tile;
    tile.door_index = static_cast<uint8_t>(0x80 | 0x02);

    EXPECT_TRUE(tile.has_portal());

    tile.door_index = 0x01;
    EXPECT_FALSE(tile.has_portal());
}

TEST(MapDataTest, GetTileOutOfBounds) {
    MapData map;
    map.width = 2;
    map.height = 2;
    map.tiles.resize(4);

    EXPECT_EQ(map.get_tile(-1, 0), nullptr);
    EXPECT_EQ(map.get_tile(2, 0), nullptr);
    EXPECT_EQ(map.get_tile(0, 2), nullptr);
}

TEST(MapDataTest, IsValidChecksSize) {
    MapData map;
    map.width = 2;
    map.height = 2;
    map.tiles.resize(4);

    EXPECT_TRUE(map.is_valid());

    map.tiles.resize(3);
    EXPECT_FALSE(map.is_valid());
}

TEST(MapDataTest, BuildWalkabilityMatrix) {
    MapData map;
    map.width = 2;
    map.height = 2;
    map.tiles.resize(4);

    map.tiles[0].background = 0x8000;  // (0,0) blocked
    map.tiles[1].object = 0x8000;      // (1,0) blocked
    map.tiles[2].door_index = static_cast<uint8_t>(0x80 | 0x01);  // (0,1) closed door
    map.tiles[2].door_offset = 0x00;

    auto matrix = map.build_walkability_matrix();
    ASSERT_EQ(matrix.size(), 2u);
    ASSERT_EQ(matrix[0].size(), 2u);
    ASSERT_EQ(matrix[1].size(), 2u);

    EXPECT_FALSE(matrix[0][0]);
    EXPECT_FALSE(matrix[0][1]);
    EXPECT_FALSE(matrix[1][0]);
    EXPECT_TRUE(matrix[1][1]);
}

TEST(ResourceLoaderConstantsTest, MaxSpriteAreaValue) {
    EXPECT_EQ(mir2::client::MAX_SPRITE_AREA, static_cast<size_t>(4096u * 4096u));
}

TEST(ResourceLoaderConstantsTest, MaxImageCountValue) {
    EXPECT_EQ(mir2::client::MAX_IMAGE_COUNT, 100000);
}

TEST(MapLoaderTest, DetectsKnownFormat) {
    std::vector<uint8_t> tile_data(12, 0);
    WriteLE16(tile_data, 0, 0x1234);
    WriteLE16(tile_data, 4, 0x8000);

    const auto data = MakeMapFileBytes(1, 1, 52, 12, tile_data);
    TempFile map_file(".map", data);
    ASSERT_TRUE(map_file.ok());

    MapLoader loader;
    auto map = loader.load(map_file.path().string());
    ASSERT_TRUE(map.has_value());
    EXPECT_EQ(map->width, 1);
    EXPECT_EQ(map->height, 1);
    ASSERT_EQ(map->tiles.size(), 1u);
    EXPECT_EQ(map->tiles[0].background, 0x1234);
    EXPECT_EQ(map->tiles[0].object, 0x8000);
}

TEST(MapLoaderTest, DetectsFallbackFormat) {
    std::vector<uint8_t> tile_data(12, 0);
    WriteLE16(tile_data, 0, 0x0001);
    WriteLE16(tile_data, 4, 0x00FF);

    const auto data = MakeMapFileBytes(1, 1, 58, 12, tile_data);
    TempFile map_file(".map", data);
    ASSERT_TRUE(map_file.ok());

    MapLoader loader;
    auto map = loader.load(map_file.path().string());
    ASSERT_TRUE(map.has_value());
    ASSERT_EQ(map->tiles.size(), 1u);
    EXPECT_EQ(map->tiles[0].background, 0x0001);
    EXPECT_EQ(map->tiles[0].object, 0x00FF);
}

TEST(WilArchiveTest, RejectsInvalidWixIndexCount) {
    auto wix_data = MakeLegacyWixHeader(mir2::client::MAX_IMAGE_COUNT + 1, 1);
    TempWilWixFiles files({0x00}, wix_data);
    ASSERT_TRUE(files.ok);

    WilArchive archive;
    EXPECT_FALSE(archive.load(files.wil_path.string()));
}

TEST(WilArchiveTest, RejectsInvalidWilSignature) {
    auto wix_data = MakeLegacyWixHeader(0, 1);
    std::vector<uint8_t> wil_data(mir2::client::WIL_TITLE_SIZE + 4 * sizeof(int32_t), 0);
    TempWilWixFiles files(wil_data, wix_data);
    ASSERT_TRUE(files.ok);

    WilArchive archive;
    EXPECT_FALSE(archive.load(files.wil_path.string()));
}

TEST(WilArchiveTest, GetSpriteReturnsNulloptForZeroOffset) {
    auto wix_data = MakeLegacyWixHeader(1, 1);
    wix_data.resize(wix_data.size() + sizeof(int32_t), 0);
    auto wil_data = MakeIlibWil(1, 256);

    TempWilWixFiles files(wil_data, wix_data);
    ASSERT_TRUE(files.ok);

    WilArchive archive;
    ASSERT_TRUE(archive.load(files.wil_path.string()));
    EXPECT_FALSE(archive.get_sprite(0).has_value());
}
