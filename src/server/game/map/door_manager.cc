/**
 * @file door_manager.cc
 * @brief 门管理器实现
 */

#include "game/map/door_manager.h"

#include <algorithm>

namespace mir2::game::map {
namespace {

constexpr int32_t kOpenRangeX = 20;
constexpr int32_t kOpenRangeY = 20;
constexpr int32_t kCloseRangeX = 18;
constexpr int32_t kCloseRangeY = 20;

bool IsTriggerInRange(int32_t trigger_x, int32_t trigger_y, int32_t tile_x,
                      int32_t tile_y, int32_t range_x, int32_t range_y) {
  const int64_t dx = static_cast<int64_t>(trigger_x) - tile_x;
  const int64_t dy = static_cast<int64_t>(trigger_y) - tile_y;
  return dx >= -range_x && dx <= range_x && dy >= -range_y &&
         dy <= range_y;
}

}  // namespace

int64_t DoorManager::MakeTileKey(int32_t x, int32_t y) {
  return (static_cast<int64_t>(y) << 32) | static_cast<uint32_t>(x);
}

void DoorManager::BuildDoorIndex(int32_t width, int32_t height,
                                 const std::vector<TileInfo>& tiles) {
  doors_.clear();
  tile_to_door_.clear();

  if (width <= 0 || height <= 0) {
    return;
  }

  const size_t expected_size =
      static_cast<size_t>(width) * static_cast<size_t>(height);
  if (expected_size == 0 || tiles.empty()) {
    return;
  }

  const size_t tile_count = std::min(expected_size, tiles.size());
  for (size_t index = 0; index < tile_count; ++index) {
    const TileInfo& tile = tiles[index];
    if (tile.door_index == 0) {
      continue;
    }

    const int32_t x =
        static_cast<int32_t>(index % static_cast<size_t>(width));
    const int32_t y =
        static_cast<int32_t>(index / static_cast<size_t>(width));
    const uint32_t door_id = static_cast<uint32_t>(tile.door_index);

    auto& door = doors_[door_id];
    door.door_id = door_id;
    door.tiles.emplace_back(x, y);
    tile_to_door_[MakeTileKey(x, y)] = door_id;
  }
}

bool DoorManager::HasDoor(int32_t x, int32_t y) const {
  return tile_to_door_.find(MakeTileKey(x, y)) != tile_to_door_.end();
}

bool DoorManager::IsDoorOpen(int32_t x, int32_t y) const {
  auto tile_it = tile_to_door_.find(MakeTileKey(x, y));
  if (tile_it == tile_to_door_.end()) {
    return false;
  }

  auto door_it = doors_.find(tile_it->second);
  if (door_it == doors_.end()) {
    return false;
  }

  return door_it->second.is_open;
}

bool DoorManager::OpenDoor(uint32_t door_id, int32_t trigger_x,
                           int32_t trigger_y) {
  auto it = doors_.find(door_id);
  if (it == doors_.end()) {
    return false;
  }

  for (const auto& tile : it->second.tiles) {
    if (IsTriggerInRange(trigger_x, trigger_y, tile.first, tile.second,
                         kOpenRangeX, kOpenRangeY)) {
      it->second.is_open = true;
      return true;
    }
  }

  return false;
}

bool DoorManager::CloseDoor(uint32_t door_id, int32_t trigger_x,
                            int32_t trigger_y) {
  auto it = doors_.find(door_id);
  if (it == doors_.end()) {
    return false;
  }

  for (const auto& tile : it->second.tiles) {
    if (IsTriggerInRange(trigger_x, trigger_y, tile.first, tile.second,
                         kCloseRangeX, kCloseRangeY)) {
      it->second.is_open = false;
      return true;
    }
  }

  return false;
}

bool DoorManager::LockDoor(uint32_t door_id, const std::string& key) {
  auto it = doors_.find(door_id);
  if (it == doors_.end()) {
    return false;
  }

  it->second.is_locked = true;
  it->second.lock_key = key;
  return true;
}

bool DoorManager::UnlockDoor(uint32_t door_id) {
  auto it = doors_.find(door_id);
  if (it == doors_.end()) {
    return false;
  }

  it->second.is_locked = false;
  it->second.lock_key.clear();
  return true;
}

}  // namespace mir2::game::map
