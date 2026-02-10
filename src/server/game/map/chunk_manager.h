/**
 * @file chunk_manager.h
 * @brief 地图分块管理器（占位框架）
 *
 * 负责按 Chunk 加载与卸载地图瓦片数据，为后续 AOI/事件扫描优化做准备。
 */

#ifndef MIR2_GAME_MAP_CHUNK_MANAGER_H_
#define MIR2_GAME_MAP_CHUNK_MANAGER_H_

#include <cstdint>
#include <unordered_map>
#include <vector>

#include "game/map/map_loader.h"

namespace mir2::game::map {

/**
 * @brief 地图 Chunk 管理器
 */
class ChunkManager {
 public:
  // Experimental module: not wired into the main map runtime pipeline yet.
  static constexpr bool kExperimental = true;
  static constexpr int32_t kChunkSize = 40;    // 40格 chunk
  static constexpr int32_t kActiveWindow = 3;  // 3x3 激活窗口

  /**
   * @brief Chunk 数据
   */
  struct Chunk {
    int32_t chunk_x = 0;
    int32_t chunk_y = 0;
    std::vector<TileInfo> tiles;
    bool loaded = false;
  };

  ChunkManager() = default;
  ~ChunkManager() = default;

  ChunkManager(const ChunkManager&) = delete;
  ChunkManager& operator=(const ChunkManager&) = delete;

  /**
   * @brief 加载指定 Chunk（占位实现）
   */
  void LoadChunk(int32_t chunk_x, int32_t chunk_y);

  /**
   * @brief 卸载指定 Chunk（占位实现）
   */
  void UnloadChunk(int32_t chunk_x, int32_t chunk_y);

  /**
   * @brief 更新实体所在的激活窗口（3x3）
   */
  void UpdateActiveWindow(int32_t entity_x, int32_t entity_y);

  /**
   * @brief 获取已加载的 Chunk 指针
   */
  const Chunk* GetChunk(int32_t chunk_x, int32_t chunk_y) const;

 private:
  struct ChunkId {
    int32_t x;
    int32_t y;

    bool operator==(const ChunkId& other) const {
      return x == other.x && y == other.y;
    }
  };

  struct ChunkIdHash {
    size_t operator()(const ChunkId& id) const {
      const uint64_t key =
          (static_cast<uint64_t>(static_cast<uint32_t>(id.x)) << 32) |
          static_cast<uint32_t>(id.y);
      return std::hash<uint64_t>()(key);
    }
  };

  static ChunkId ToChunkId(int32_t x, int32_t y) {
    return {x / kChunkSize, y / kChunkSize};
  }

  std::unordered_map<ChunkId, Chunk, ChunkIdHash> chunks_;
};

}  // namespace mir2::game::map

#endif  // MIR2_GAME_MAP_CHUNK_MANAGER_H_
