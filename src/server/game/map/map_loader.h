/**
 * @file map_loader.h
 * @brief 服务器端地图瓦片数据加载器
 *
 * 负责解析 .map 文件并提供可行走性数据。
 */

#ifndef MIR2_GAME_MAP_MAP_LOADER_H_
#define MIR2_GAME_MAP_MAP_LOADER_H_

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace mir2::game::map {

/**
 * @brief 地图可行走性数据
 *
 * walkable 按行优先顺序存储。
 */
struct MapWalkability {
  int32_t width = 0;
  int32_t height = 0;
  std::vector<uint8_t> walkable;

  bool IsWalkable(int32_t x, int32_t y) const;
};

/**
 * @brief 地图瓦片数据
 */
struct TileInfo {
  uint16_t bk_img = 0;     ///< 背景图像索引
  uint16_t fr_img = 0;     ///< 前景图像索引
  uint8_t door_index = 0;  ///< 门索引（0 表示无门）
  uint8_t door_offset = 0; ///< 门偏移
  uint8_t area = 0;        ///< 区域标识
  uint8_t light = 0;       ///< 光源等级
};

/**
 * @brief 完整地图瓦片数据
 *
 * tiles 按行优先顺序存储。
 */
struct MapTileData {
  int32_t width = 0;
  int32_t height = 0;
  std::vector<TileInfo> tiles;
  std::vector<uint8_t> walkable;

  bool IsWalkable(int32_t x, int32_t y) const;
  bool IsFlyable(int32_t x, int32_t y) const;
  const TileInfo* GetTile(int32_t x, int32_t y) const;
};

/**
 * @brief .map 文件加载器
 */
class MapLoader {
 public:
  MapLoader() = default;
  ~MapLoader() = default;

  MapLoader(const MapLoader&) = delete;
  MapLoader& operator=(const MapLoader&) = delete;

  /**
   * @brief 加载 .map 文件并解析可行走性数据
   *
   * 内部会复用 LoadFull 的解析结果，只投影 walkability 字段，
   * 以避免与完整瓦片解析出现规则分叉。
   *
   * @param map_path .map 文件路径
   * @return 成功返回 MapWalkability，否则返回 std::nullopt
   */
  std::optional<MapWalkability> Load(const std::string& map_path) const;

  /**
   * @brief 加载 .map 文件并解析完整瓦片数据
   *
   * @param map_path .map 文件路径
   * @return 成功返回 MapTileData，否则返回 std::nullopt
   */
  std::optional<MapTileData> LoadFull(const std::string& map_path) const;
};

}  // namespace mir2::game::map

#endif  // MIR2_GAME_MAP_MAP_LOADER_H_
