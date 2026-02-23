/**
 * @file aoi_manager.cc
 * @brief AOI 管理器实现
 */

#include "game/map/aoi_manager.h"

#include <algorithm>

namespace mir2::game::map {
namespace {

struct PendingAOIEvent {
  AOIEventType type = AOIEventType::kMove;
  uint64_t watcher_id = 0;
  uint64_t target_id = 0;
  int32_t x = 0;
  int32_t y = 0;
};

template <typename GridArrayT, typename GetEntitiesFn, typename VisitEntityFn>
void CollectSurroundingEntities(const GridArrayT& surrounding,
                                size_t surrounding_count,
                                const GetEntitiesFn& get_entities,
                                const VisitEntityFn& visit_entity) {
  for (size_t i = 0; i < surrounding_count; ++i) {
    const auto* entities = get_entities(surrounding[i]);
    if (entities == nullptr) {
      continue;
    }
    for (uint64_t entity_id : *entities) {
      visit_entity(entity_id);
    }
  }
}

template <typename GridArrayT, typename ContainsGridFn, typename EmitOnGridFn>
void EmitGridDeltaEvents(const GridArrayT& source_grids,
                         size_t source_count,
                         const GridArrayT& target_grids,
                         size_t target_count,
                         const ContainsGridFn& contains_grid,
                         const EmitOnGridFn& emit_on_grid) {
  for (size_t i = 0; i < source_count; ++i) {
    const auto& grid = source_grids[i];
    if (contains_grid(target_grids, target_count, grid)) {
      continue;
    }
    emit_on_grid(grid);
  }
}

void DispatchAOIEvents(const AOIManager::AOICallback& callback,
                       const std::vector<PendingAOIEvent>& events) {
  if (!callback) {
    return;
  }

  for (const auto& event : events) {
    callback(event.type, event.watcher_id, event.target_id, event.x, event.y);
  }
}

}  // namespace

AOIManager::AOIManager(int32_t map_width, int32_t map_height, int32_t grid_size)
    : map_width_(map_width),
      map_height_(map_height),
      grid_size_(grid_size),
      callback_(nullptr) {
  // 计算格子数量（向上取整）
  grid_count_x_ = (map_width + grid_size - 1) / grid_size;
  grid_count_y_ = (map_height + grid_size - 1) / grid_size;
}

void AOIManager::Enter(uint64_t entity_id, int32_t x, int32_t y) {
  std::vector<PendingAOIEvent> pending_events;
  AOICallback callback_snapshot;
  {
    std::unique_lock<std::shared_mutex> lock(mutex_);

    // 检查是否已存在
    if (entities_.find(entity_id) != entities_.end()) {
      return;
    }

    // 计算所在格子
    GridId grid = ToGridId(x, y);

    // 存储实体位置
    entities_[entity_id] = {x, y, grid};

    // 添加到格子
    AddToGrid(entity_id, grid);

    // 获取视野范围内的格子
    GridArray surrounding{};
    const size_t surrounding_count = FillSurroundingGrids(grid, surrounding);

    // 收集视野内事件，避免锁内执行用户回调。
    CollectSurroundingEntities(
        surrounding, surrounding_count,
        [this](const GridId& candidate_grid) {
          return GetEntitiesInGrid(candidate_grid);
        },
        [&](uint64_t other_id) {
          if (other_id == entity_id) {
            return;
          }
          pending_events.push_back(
              PendingAOIEvent{AOIEventType::kEnter, other_id, entity_id, x, y});
          auto it = entities_.find(other_id);
          if (it != entities_.end()) {
            pending_events.push_back(PendingAOIEvent{
                AOIEventType::kEnter, entity_id, other_id, it->second.x,
                it->second.y});
          }
        });

    callback_snapshot = callback_;
  }

  DispatchAOIEvents(callback_snapshot, pending_events);
}

void AOIManager::Leave(uint64_t entity_id) {
  std::vector<PendingAOIEvent> pending_events;
  AOICallback callback_snapshot;
  {
    std::unique_lock<std::shared_mutex> lock(mutex_);

    auto it = entities_.find(entity_id);
    if (it == entities_.end()) {
      return;
    }

    const EntityPosition& pos = it->second;
    int32_t x = pos.x;
    int32_t y = pos.y;
    GridId grid = pos.grid;

    // 获取视野范围内的格子
    GridArray surrounding{};
    const size_t surrounding_count = FillSurroundingGrids(grid, surrounding);

    // 收集离开事件，避免锁内执行用户回调。
    CollectSurroundingEntities(
        surrounding, surrounding_count,
        [this](const GridId& candidate_grid) {
          return GetEntitiesInGrid(candidate_grid);
        },
        [&](uint64_t other_id) {
          if (other_id == entity_id) {
            return;
          }
          pending_events.push_back(
              PendingAOIEvent{AOIEventType::kLeave, other_id, entity_id, x, y});
        });

    // 从格子中移除
    RemoveFromGrid(entity_id, grid);

    // 移除实体记录
    entities_.erase(it);
    callback_snapshot = callback_;
  }

  DispatchAOIEvents(callback_snapshot, pending_events);
}

void AOIManager::Move(uint64_t entity_id, int32_t new_x, int32_t new_y) {
  std::vector<PendingAOIEvent> pending_events;
  AOICallback callback_snapshot;
  {
    std::unique_lock<std::shared_mutex> lock(mutex_);

    auto it = entities_.find(entity_id);
    if (it == entities_.end()) {
      return;
    }

    EntityPosition& pos = it->second;
    GridId old_grid = pos.grid;
    GridId new_grid = ToGridId(new_x, new_y);

    // 更新位置
    pos.x = new_x;
    pos.y = new_y;
    pos.grid = new_grid;

    // 如果格子没有变化，只需通知视野内实体移动事件
    if (old_grid == new_grid) {
      GridArray surrounding{};
      const size_t surrounding_count = FillSurroundingGrids(new_grid, surrounding);
      CollectSurroundingEntities(
          surrounding, surrounding_count,
          [this](const GridId& candidate_grid) {
            return GetEntitiesInGrid(candidate_grid);
          },
          [&](uint64_t other_id) {
            if (other_id == entity_id) {
              return;
            }
            pending_events.push_back(PendingAOIEvent{
                AOIEventType::kMove, other_id, entity_id, new_x, new_y});
          });
      callback_snapshot = callback_;
    } else {
      // 格子发生变化，需要计算进入和离开的视野范围
      GridArray old_surrounding{};
      GridArray new_surrounding{};
      const size_t old_surrounding_count =
          FillSurroundingGrids(old_grid, old_surrounding);
      const size_t new_surrounding_count =
          FillSurroundingGrids(new_grid, new_surrounding);

      // 更新格子归属
      RemoveFromGrid(entity_id, old_grid);
      AddToGrid(entity_id, new_grid);

      // 处理离开视野的实体
      EmitGridDeltaEvents(
          old_surrounding, old_surrounding_count, new_surrounding,
          new_surrounding_count,
          [this](const GridArray& grids, size_t count, const GridId& target) {
            return ContainsGrid(grids, count, target);
          },
          [&](const GridId& delta_grid) {
            CollectSurroundingEntities(
                std::array<GridId, 1>{delta_grid}, 1,
                [this](const GridId& candidate_grid) {
                  return GetEntitiesInGrid(candidate_grid);
                },
                [&](uint64_t other_id) {
                  if (other_id == entity_id) {
                    return;
                  }
                  pending_events.push_back(PendingAOIEvent{
                      AOIEventType::kLeave, other_id, entity_id, new_x, new_y});
                  auto other_it = entities_.find(other_id);
                  if (other_it != entities_.end()) {
                    pending_events.push_back(PendingAOIEvent{
                        AOIEventType::kLeave, entity_id, other_id, other_it->second.x,
                        other_it->second.y});
                  }
                });
          });

      // 处理进入视野的实体
      EmitGridDeltaEvents(
          new_surrounding, new_surrounding_count, old_surrounding,
          old_surrounding_count,
          [this](const GridArray& grids, size_t count, const GridId& target) {
            return ContainsGrid(grids, count, target);
          },
          [&](const GridId& delta_grid) {
            CollectSurroundingEntities(
                std::array<GridId, 1>{delta_grid}, 1,
                [this](const GridId& candidate_grid) {
                  return GetEntitiesInGrid(candidate_grid);
                },
                [&](uint64_t other_id) {
                  if (other_id == entity_id) {
                    return;
                  }
                  pending_events.push_back(PendingAOIEvent{
                      AOIEventType::kEnter, other_id, entity_id, new_x, new_y});
                  auto other_it = entities_.find(other_id);
                  if (other_it != entities_.end()) {
                    pending_events.push_back(PendingAOIEvent{
                        AOIEventType::kEnter, entity_id, other_id, other_it->second.x,
                        other_it->second.y});
                  }
                });
          });

      // 仍在视野内，发送移动事件
      for (size_t i = 0; i < new_surrounding_count; ++i) {
        const GridId& grid = new_surrounding[i];
        if (!ContainsGrid(old_surrounding, old_surrounding_count, grid)) {
          continue;
        }
        CollectSurroundingEntities(
            std::array<GridId, 1>{grid}, 1,
            [this](const GridId& candidate_grid) {
              return GetEntitiesInGrid(candidate_grid);
            },
            [&](uint64_t other_id) {
              if (other_id == entity_id) {
                return;
              }
              pending_events.push_back(PendingAOIEvent{
                  AOIEventType::kMove, other_id, entity_id, new_x, new_y});
            });
      }
      callback_snapshot = callback_;
    }
  }

  DispatchAOIEvents(callback_snapshot, pending_events);
}

std::vector<uint64_t> AOIManager::GetEntitiesInView(int32_t x, int32_t y) const {
  std::shared_lock<std::shared_mutex> lock(mutex_);

  std::vector<uint64_t> result;
  GridId grid = ToGridId(x, y);
  GridArray surrounding{};
  const size_t surrounding_count = FillSurroundingGrids(grid, surrounding);

  size_t reserve_count = 0;
  for (size_t i = 0; i < surrounding_count; ++i) {
    const auto* entities = GetEntitiesInGrid(surrounding[i]);
    if (entities != nullptr) {
      reserve_count += entities->size();
    }
  }
  result.reserve(reserve_count);

  for (size_t i = 0; i < surrounding_count; ++i) {
    const auto* entities = GetEntitiesInGrid(surrounding[i]);
    if (entities == nullptr) {
      continue;
    }
    result.insert(result.end(), entities->begin(), entities->end());
  }

  return result;
}

std::vector<uint64_t> AOIManager::GetEntitiesInViewOf(uint64_t entity_id) const {
  std::shared_lock<std::shared_mutex> lock(mutex_);

  std::vector<uint64_t> result;

  auto it = entities_.find(entity_id);
  if (it == entities_.end()) {
    return result;
  }

  GridArray surrounding{};
  const size_t surrounding_count =
      FillSurroundingGrids(it->second.grid, surrounding);

  size_t reserve_count = 0;
  for (size_t i = 0; i < surrounding_count; ++i) {
    const auto* entities = GetEntitiesInGrid(surrounding[i]);
    if (entities != nullptr) {
      reserve_count += entities->size();
    }
  }
  result.reserve(reserve_count);

  for (size_t i = 0; i < surrounding_count; ++i) {
    const auto* entities = GetEntitiesInGrid(surrounding[i]);
    if (entities == nullptr) {
      continue;
    }
    for (uint64_t id : *entities) {
      if (id != entity_id) {
        result.push_back(id);
      }
    }
  }

  return result;
}

bool AOIManager::InViewOf(uint64_t entity_a, uint64_t entity_b) const {
  std::shared_lock<std::shared_mutex> lock(mutex_);

  auto it_a = entities_.find(entity_a);
  auto it_b = entities_.find(entity_b);

  if (it_a == entities_.end() || it_b == entities_.end()) {
    return false;
  }

  const GridId& grid_a = it_a->second.grid;
  const GridId& grid_b = it_b->second.grid;

  // 检查两个实体的格子是否在九宫格范围内
  int32_t dx = std::abs(grid_a.gx - grid_b.gx);
  int32_t dy = std::abs(grid_a.gy - grid_b.gy);

  return dx <= 1 && dy <= 1;
}

size_t AOIManager::EntityCount() const {
  std::shared_lock<std::shared_mutex> lock(mutex_);
  return entities_.size();
}

bool AOIManager::GetEntityPosition(uint64_t entity_id, int32_t& out_x,
                                   int32_t& out_y) const {
  std::shared_lock<std::shared_mutex> lock(mutex_);

  auto it = entities_.find(entity_id);
  if (it != entities_.end()) {
    out_x = it->second.x;
    out_y = it->second.y;
    return true;
  }
  return false;
}

void AOIManager::Clear() {
  std::unique_lock<std::shared_mutex> lock(mutex_);
  grids_.clear();
  entities_.clear();
}

size_t AOIManager::FillSurroundingGrids(const GridId& center,
                                        GridArray& out) const {
  size_t count = 0;
  for (int32_t dy = -1; dy <= 1; ++dy) {
    for (int32_t dx = -1; dx <= 1; ++dx) {
      int32_t gx = center.gx + dx;
      int32_t gy = center.gy + dy;

      // 边界检查
      if (gx >= 0 && gx < grid_count_x_ && gy >= 0 && gy < grid_count_y_) {
        out[count++] = {gx, gy};
      }
    }
  }

  return count;
}

const std::unordered_set<uint64_t>* AOIManager::GetEntitiesInGrid(
    const GridId& grid) const {
  auto it = grids_.find(grid);
  if (it == grids_.end()) {
    return nullptr;
  }
  return &it->second;
}

bool AOIManager::ContainsGrid(const GridArray& grids, size_t count,
                              const GridId& target) {
  for (size_t i = 0; i < count; ++i) {
    if (grids[i] == target) {
      return true;
    }
  }
  return false;
}

void AOIManager::AddToGrid(uint64_t entity_id, const GridId& grid) {
  grids_[grid].insert(entity_id);
}

void AOIManager::RemoveFromGrid(uint64_t entity_id, const GridId& grid) {
  auto it = grids_.find(grid);
  if (it != grids_.end()) {
    it->second.erase(entity_id);
    if (it->second.empty()) {
      grids_.erase(it);
    }
  }
}

}  // namespace mir2::game::map
