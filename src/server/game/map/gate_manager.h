/**
 * @file gate_manager.h
 * @brief 传送门管理器
 */

#ifndef MIR2_GAME_MAP_GATE_MANAGER_H_
#define MIR2_GAME_MAP_GATE_MANAGER_H_

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace mir2::game::map {

struct GateInfo {
  uint32_t gate_id;
  std::string source_map;
  int32_t source_x, source_y;
  std::string target_map;
  int32_t target_x, target_y;
  bool require_item = false;
  int32_t required_item_id = 0;
};

class GateManager {
 public:
  /**
   * @brief 添加传送门配置
   */
  void AddGate(const GateInfo& gate);

  /**
   * @brief 检查坐标是否触发传送门
   */
  std::optional<GateInfo> CheckGateTrigger(const std::string& map_id,
                                           int32_t x,
                                           int32_t y) const;

  /**
   * @brief 检查坐标是否触发传送门（数值地图ID热路径）
   */
  std::optional<GateInfo> CheckGateTrigger(int32_t map_id,
                                           int32_t x,
                                           int32_t y) const;

  /**
   * @brief 从配置文件加载传送门
   */
 void LoadFromConfig(const std::string& config_path);

 private:
  struct CoordKey {
    std::string map_id;
    int32_t x = 0;
    int32_t y = 0;

    bool operator==(const CoordKey& other) const {
      return map_id == other.map_id && x == other.x && y == other.y;
    }
  };

  struct CoordKeyHash {
    size_t operator()(const CoordKey& key) const {
      size_t seed = std::hash<std::string>{}(key.map_id);
      seed ^= std::hash<int32_t>{}(key.x) + 0x9e3779b9U + (seed << 6) + (seed >> 2);
      seed ^= std::hash<int32_t>{}(key.y) + 0x9e3779b9U + (seed << 6) + (seed >> 2);
      return seed;
    }
  };

  struct NumericCoordKey {
    int32_t map_id = 0;
    int32_t x = 0;
    int32_t y = 0;

    bool operator==(const NumericCoordKey& other) const {
      return map_id == other.map_id && x == other.x && y == other.y;
    }
  };

  struct NumericCoordKeyHash {
    size_t operator()(const NumericCoordKey& key) const {
      size_t seed = std::hash<int32_t>{}(key.map_id);
      seed ^= std::hash<int32_t>{}(key.x) + 0x9e3779b9U + (seed << 6) + (seed >> 2);
      seed ^= std::hash<int32_t>{}(key.y) + 0x9e3779b9U + (seed << 6) + (seed >> 2);
      return seed;
    }
  };

  std::vector<GateInfo> gates_;
  std::unordered_map<std::string, std::vector<size_t>> map_gates_;  // map_id -> gate indices
  std::unordered_map<CoordKey, size_t, CoordKeyHash> coord_index_;
  std::unordered_map<NumericCoordKey, size_t, NumericCoordKeyHash>
      numeric_coord_index_;

  static CoordKey MakeCoordKey(const std::string& map_id, int32_t x, int32_t y) {
    return CoordKey{map_id, x, y};
  }

  static NumericCoordKey MakeNumericCoordKey(int32_t map_id, int32_t x, int32_t y) {
    return NumericCoordKey{map_id, x, y};
  }
};

}  // namespace mir2::game::map

#endif  // MIR2_GAME_MAP_GATE_MANAGER_H_
