/**
 * @file door_manager.h
 * @brief 门管理器
 *
 * 负责构建门索引并提供开关/锁定状态查询。
 */

#ifndef MIR2_GAME_MAP_DOOR_MANAGER_H_
#define MIR2_GAME_MAP_DOOR_MANAGER_H_

#include <cstdint>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "game/map/map_loader.h"

namespace mir2::game::map {

struct DoorInfo {
  uint32_t door_id = 0;
  std::vector<std::pair<int32_t, int32_t>> tiles;  // 门占用的格子列表
  bool is_open = false;
  bool is_locked = false;
  std::string lock_key;  // 需要的钥匙物品名
};

class DoorManager {
 public:
  void BuildDoorIndex(int32_t width, int32_t height,
                      const std::vector<TileInfo>& tiles);
  bool HasDoor(int32_t x, int32_t y) const;
  bool IsDoorOpen(int32_t x, int32_t y) const;
  bool OpenDoor(uint32_t door_id, int32_t trigger_x, int32_t trigger_y);
  bool CloseDoor(uint32_t door_id, int32_t trigger_x, int32_t trigger_y);
  bool LockDoor(uint32_t door_id, const std::string& key);
  bool UnlockDoor(uint32_t door_id);

 private:
  static int64_t MakeTileKey(int32_t x, int32_t y);

  std::unordered_map<uint32_t, DoorInfo> doors_;
  std::unordered_map<int64_t, uint32_t> tile_to_door_;  // (y<<32|x) -> door_id
};

}  // namespace mir2::game::map

#endif  // MIR2_GAME_MAP_DOOR_MANAGER_H_
