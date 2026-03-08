/**
 * @file map_loader.cc
 * @brief 服务器端地图瓦片数据加载器实现
 */

#include "game/map/map_loader.h"

#include <cstdlib>
#include <array>
#include <filesystem>
#include <fstream>
#include <system_error>

namespace mir2::game::map {
namespace {

// Legend2 地图文件格式常量
constexpr int kMapMetadataSize = 48;
constexpr int kMapBaseHeaderSize = 4 + kMapMetadataSize;
constexpr int kMapTileMinSize = 12;
constexpr uint16_t kCheckKey = 0xAA38;
constexpr size_t kMapTitleMaxLength = 20;

struct MapFormat {
  int header_size;
  int tile_size;
};

constexpr std::array<MapFormat, 5> kMapFormats = {{
    {52, 20},
    {52, 12},
    {64, 12},
    {8092, 12},
    {9652, 12},
}};

std::optional<MapFormat> DetectMapFormat(int64_t file_size, int32_t width,
                                         int32_t height) {
  if (width <= 0 || height <= 0) {
    return std::nullopt;
  }

  const int64_t tile_count =
      static_cast<int64_t>(width) * static_cast<int64_t>(height);
  if (tile_count <= 0) {
    return std::nullopt;
  }

  for (const auto& fmt : kMapFormats) {
    const int64_t expected = static_cast<int64_t>(fmt.header_size) +
                             tile_count * static_cast<int64_t>(fmt.tile_size);
    if (expected == file_size) {
      return fmt;
    }
  }

  constexpr std::array<int, 4> kTileSizes = {12, 20, 16, 14};
  for (int tile_size : kTileSizes) {
    const int64_t header_size =
        file_size - tile_count * static_cast<int64_t>(tile_size);
    if (header_size >= kMapBaseHeaderSize && header_size <= file_size) {
      return MapFormat{static_cast<int>(header_size), tile_size};
    }
  }

  return std::nullopt;
}

std::string ExtractTitle(const std::array<uint8_t, kMapBaseHeaderSize>& header) {
  constexpr size_t kTitleOffset = 4;
  if (kTitleOffset >= header.size()) {
    return {};
  }

  const uint8_t length = header[kTitleOffset];
  if (length > 0 && length <= kMapTitleMaxLength &&
      kTitleOffset + 1 + length <= header.size()) {
    return std::string(
        reinterpret_cast<const char*>(header.data() + kTitleOffset + 1),
        length);
  }

  size_t actual_length = 0;
  while (actual_length < kMapTitleMaxLength &&
         kTitleOffset + actual_length < header.size()) {
    if (header[kTitleOffset + actual_length] == 0) {
      break;
    }
    ++actual_length;
  }

  if (actual_length == 0) {
    return {};
  }

  return std::string(
      reinterpret_cast<const char*>(header.data() + kTitleOffset),
      actual_length);
}

std::string NormalizeMapName(const std::string& value) {
  std::string result;
  result.reserve(value.size());
  for (unsigned char c : value) {
    if (c >= 'a' && c <= 'z') {
      result.push_back(static_cast<char>(c - 'a' + 'A'));
    } else {
      result.push_back(static_cast<char>(c));
    }
  }
  return result;
}

bool IsEncryptedMapName(const std::string& name) {
  return name == "LABY01" || name == "LABY02" || name == "LABY03" ||
         name == "LABY04" || name == "SNAKE";
}

bool ShouldDecryptMap(const std::string& map_path, const std::string& title) {
  const std::string stem =
      NormalizeMapName(std::filesystem::path(map_path).stem().string());
  if (IsEncryptedMapName(stem)) {
    return true;
  }

  if (!title.empty() &&
      IsEncryptedMapName(NormalizeMapName(title))) {
    return true;
  }

  return false;
}

std::optional<std::filesystem::path> ResolveAllowedMapBase() {
  std::error_code cwd_ec;
  const std::filesystem::path cwd = std::filesystem::current_path(cwd_ec);
  if (cwd_ec) {
    return std::nullopt;
  }

  std::filesystem::path configured_base;
  if (const char* env_base = std::getenv("LEGEND2_MAP_BASE_PATH");
      env_base != nullptr && env_base[0] != '\0') {
    configured_base = std::filesystem::path(env_base);
    if (!configured_base.is_absolute()) {
      configured_base = (cwd / configured_base).lexically_normal();
    }
  } else {
    configured_base = cwd / "Map";
  }

  std::error_code canonical_ec;
  const std::filesystem::path canonical_base =
      std::filesystem::canonical(configured_base, canonical_ec);
  if (canonical_ec) {
    return std::nullopt;
  }

  return canonical_base;
}

const std::optional<std::filesystem::path>& GetAllowedMapBase() {
  // 仅解析一次，避免运行时 current_path() 变更引入校验基准漂移。
  static const std::optional<std::filesystem::path> kAllowedBase =
      ResolveAllowedMapBase();
  return kAllowedBase;
}

bool IsPathWithinBase(const std::filesystem::path& path,
                      const std::filesystem::path& base) {
  std::error_code rel_ec;
  const std::filesystem::path rel_path =
      std::filesystem::relative(path, base, rel_ec);
  if (rel_ec || rel_path.empty() || rel_path.is_absolute()) {
    return false;
  }

  for (const auto& component : rel_path) {
    if (component == "..") {
      return false;
    }
  }

  return true;
}

/**
 * @brief 验证地图文件路径安全性
 *
 * 防止路径遍历攻击，确保路径在允许的 Map/ 目录内。
 *
 * @param map_path 输入路径
 * @param out_validated_path 输出验证后的规范化路径
 * @return true 如果路径安全有效
 */
bool ValidateMapPath(const std::string& map_path,
                     std::filesystem::path& out_validated_path) {
  if (map_path.empty()) {
    return false;
  }

  const auto& allowed_base = GetAllowedMapBase();
  if (!allowed_base.has_value()) {
    return false;
  }

  std::filesystem::path path(map_path);
  std::error_code abs_ec;
  if (!path.is_absolute()) {
    path = std::filesystem::absolute(path, abs_ec);
    if (abs_ec) {
      return false;
    }
  }

  std::error_code canonical_ec;
  path = std::filesystem::canonical(path, canonical_ec);
  if (canonical_ec) {
    return false;
  }

  if (!IsPathWithinBase(path, *allowed_base)) {
    return false;
  }

  out_validated_path = std::move(path);
  return true;
}

bool ReadHeader(std::ifstream& file, int32_t& width, int32_t& height,
                std::string* title) {
  std::array<uint8_t, kMapBaseHeaderSize> header{};
  file.read(reinterpret_cast<char*>(header.data()),
            static_cast<std::streamsize>(header.size()));
  if (!file.good()) {
    return false;
  }

  auto read_le16 = [](const uint8_t* data) -> uint16_t {
    return static_cast<uint16_t>(data[0]) |
           (static_cast<uint16_t>(data[1]) << 8);
  };

  auto is_valid_dim = [](uint16_t w, uint16_t h) {
    return w > 0 && w <= 2000 && h > 0 && h <= 2000;
  };

  const uint16_t raw_width = read_le16(&header[0]);
  const uint16_t raw_height = read_le16(&header[2]);

  const uint16_t enc_width = read_le16(&header[31]);
  const uint16_t key = read_le16(&header[33]);
  const uint16_t enc_height = read_le16(&header[35]);
  if (key == kCheckKey) {
    const uint16_t decoded_width = static_cast<uint16_t>(enc_width ^ key);
    const uint16_t decoded_height = static_cast<uint16_t>(enc_height ^ key);
    if (is_valid_dim(decoded_width, decoded_height)) {
      width = static_cast<int32_t>(decoded_width);
      height = static_cast<int32_t>(decoded_height);
      if (title) {
        title->clear();
      }
      return true;
    }
  }

  if (!is_valid_dim(raw_width, raw_height)) {
    return false;
  }

  width = static_cast<int32_t>(raw_width);
  height = static_cast<int32_t>(raw_height);
  if (title) {
    *title = ExtractTitle(header);
  }
  return true;
}

bool IsTileWalkable(uint16_t background, uint16_t object, uint8_t door_index,
                    uint8_t door_offset) {
  const bool blocked_by_background = (background & 0x8000) != 0;
  const bool blocked_by_object = (object & 0x8000) != 0;
  const bool has_door = (door_index & 0x80) != 0 && (door_index & 0x7F) != 0;
  const bool door_closed = has_door && (door_offset & 0x80) == 0;
  return !(blocked_by_background || blocked_by_object || door_closed);
}

bool ReadTiles(std::ifstream& file, MapTileData& map, int tile_stride) {
  if (tile_stride < kMapTileMinSize) {
    return false;
  }

  const int64_t tile_count =
      static_cast<int64_t>(map.width) * static_cast<int64_t>(map.height);
  if (tile_count <= 0) {
    return false;
  }

  map.tiles.assign(static_cast<size_t>(tile_count), TileInfo{});

  std::vector<uint8_t> tile_buffer(static_cast<size_t>(tile_stride));
  for (int64_t i = 0; i < tile_count; ++i) {
    file.read(reinterpret_cast<char*>(tile_buffer.data()), tile_stride);
    if (!file.good()) {
      return false;
    }

    // MIR2 map tile stream is column-major (x outer, y inner). We normalize to
    // row-major storage: index = y * width + x.
    const int32_t tile_x = static_cast<int32_t>(i / map.height);
    const int32_t tile_y = static_cast<int32_t>(i % map.height);
    // Use size_t intermediates to avoid int32 overflow when computing index.
    const size_t out_index =
        static_cast<size_t>(tile_y) * static_cast<size_t>(map.width) +
        static_cast<size_t>(tile_x);

    TileInfo& tile = map.tiles[out_index];
    tile.bk_img = static_cast<uint16_t>(tile_buffer[0]) |
                  (static_cast<uint16_t>(tile_buffer[1]) << 8);
    tile.fr_img = static_cast<uint16_t>(tile_buffer[4]) |
                  (static_cast<uint16_t>(tile_buffer[5]) << 8);
    tile.door_index = tile_buffer[6];
    tile.door_offset = tile_buffer[7];
    tile.area = tile_buffer[10];
    tile.light = tile_buffer[11];
  }

  return true;
}

void DecryptTiles(std::vector<TileInfo>& tiles) {
  for (auto& tile : tiles) {
    tile.bk_img = static_cast<uint16_t>(tile.bk_img ^ kCheckKey);
    tile.fr_img = static_cast<uint16_t>(tile.fr_img ^ kCheckKey);
  }
}

}  // namespace

bool MapTileData::IsWalkable(int32_t x, int32_t y) const {
  if (x < 0 || y < 0 || x >= width || y >= height) {
    return false;
  }
  // Use size_t intermediates to avoid int32 overflow when computing index.
  const size_t index = static_cast<size_t>(y) * static_cast<size_t>(width) +
                       static_cast<size_t>(x);
  if (index >= walkable.size()) {
    return false;
  }
  return walkable[index] != 0;
}

bool MapTileData::IsFlyable(int32_t x, int32_t y) const {
  const TileInfo* tile = GetTile(x, y);
  if (!tile) {
    return false;
  }
  return (tile->fr_img & 0x8000) == 0;
}

const TileInfo* MapTileData::GetTile(int32_t x, int32_t y) const {
  if (x < 0 || y < 0 || x >= width || y >= height) {
    return nullptr;
  }
  // Use size_t intermediates to avoid int32 overflow when computing index.
  const size_t index = static_cast<size_t>(y) * static_cast<size_t>(width) +
                       static_cast<size_t>(x);
  if (index >= tiles.size()) {
    return nullptr;
  }
  return &tiles[index];
}

bool MapWalkability::IsWalkable(int32_t x, int32_t y) const {
  if (x < 0 || y < 0 || x >= width || y >= height) {
    return false;
  }
  // Use size_t intermediates to avoid int32 overflow when computing index.
  const size_t index = static_cast<size_t>(y) * static_cast<size_t>(width) +
                       static_cast<size_t>(x);
  if (index >= walkable.size()) {
    return false;
  }
  return walkable[index] != 0;
}

std::optional<MapWalkability> MapLoader::Load(
    const std::string& map_path) const {
  // Keep a single parser path (LoadFull) as source of truth for walkability.
  auto full = LoadFull(map_path);
  if (!full) {
    return std::nullopt;
  }

  MapWalkability map;
  map.width = full->width;
  map.height = full->height;
  map.walkable = std::move(full->walkable);
  return map;
}

std::optional<MapTileData> MapLoader::LoadFull(
    const std::string& map_path) const {
  // Path validation to prevent path traversal attacks
  std::filesystem::path path;
  if (!ValidateMapPath(map_path, path)) {
    return std::nullopt;
  }

  std::ifstream file(path, std::ios::binary);
  if (!file.is_open()) {
    return std::nullopt;
  }

  std::error_code ec;
  const int64_t file_size =
      static_cast<int64_t>(std::filesystem::file_size(path, ec));
  if (ec || file_size < kMapBaseHeaderSize) {
    return std::nullopt;
  }

  MapTileData map;
  int32_t width = 0;
  int32_t height = 0;
  std::string title;
  if (!ReadHeader(file, width, height, &title)) {
    return std::nullopt;
  }

  constexpr int64_t kMaxTileCount = 100000000;  // 100M tiles max (10000x10000)
  const int64_t tile_count = static_cast<int64_t>(width) * height;
  if (tile_count <= 0 || tile_count > kMaxTileCount) {
    return std::nullopt;
  }

  auto format_opt = DetectMapFormat(file_size, width, height);
  if (!format_opt) {
    return std::nullopt;
  }

  map.width = width;
  map.height = height;

  file.seekg(format_opt->header_size, std::ios::beg);
  if (!file.good()) {
    return std::nullopt;
  }

  if (!ReadTiles(file, map, format_opt->tile_size)) {
    return std::nullopt;
  }

  if (ShouldDecryptMap(path.string(), title)) {
    DecryptTiles(map.tiles);
  }

  map.walkable.assign(static_cast<size_t>(tile_count), 0);
  for (int64_t i = 0; i < tile_count; ++i) {
    const TileInfo& tile = map.tiles[static_cast<size_t>(i)];
    const bool is_walkable =
        IsTileWalkable(tile.bk_img, tile.fr_img, tile.door_index, tile.door_offset);
    map.walkable[static_cast<size_t>(i)] =
        static_cast<uint8_t>(is_walkable ? 1 : 0);
  }

  return map;
}

}  // namespace mir2::game::map
