/**
 * @file aoi_manager.cc
 * @brief AOI 管理器实现
 */

#include "game/map/aoi_manager.h"

#include <algorithm>
#include <chrono>

#include "monitor/metrics.h"

namespace mir2::game::map {
namespace {

using SteadyClock = std::chrono::steady_clock;

constexpr const char* kMetricAoiLockWaitMs = "logic.aoi.lock_wait_ms";
constexpr const char* kMetricAoiLockWaitSamplesTotal =
    "logic.aoi.lock_wait_samples_total";
constexpr const char* kMetricAoiLockWaitNsTotal = "logic.aoi.lock_wait_ns_total";
constexpr const char* kMetricAoiEventsPerBatch = "logic.aoi.events_per_batch";
constexpr const char* kMetricAoiEventsTotal = "logic.aoi.events_total";
constexpr const char* kMetricAoiHotGridDensity = "logic.aoi.hot_grid_density";

void RecordAoiLockWait(SteadyClock::time_point wait_started_at) {
  const auto wait_ns_raw =
      std::chrono::duration_cast<std::chrono::nanoseconds>(
          SteadyClock::now() - wait_started_at)
          .count();
  if (wait_ns_raw < 0) {
    return;
  }
  const uint64_t wait_ns = static_cast<uint64_t>(wait_ns_raw);
  monitor::Metrics::Instance().SetGauge(
      kMetricAoiLockWaitMs, static_cast<double>(wait_ns) / 1000000.0);
  monitor::Metrics::Instance().IncrementCounter(kMetricAoiLockWaitSamplesTotal);
  monitor::Metrics::Instance().IncrementCounter(kMetricAoiLockWaitNsTotal, wait_ns);
}

void RecordAoiEventBatch(size_t event_count) {
  monitor::Metrics::Instance().SetGauge(
      kMetricAoiEventsPerBatch, static_cast<double>(event_count));
  monitor::Metrics::Instance().IncrementCounter(
      kMetricAoiEventsTotal, static_cast<uint64_t>(event_count));
}

void UpdateAoiHotGridDensityGauge(size_t density) {
  monitor::Metrics::Instance().SetGauge(
      kMetricAoiHotGridDensity, static_cast<double>(density));
}

struct PendingAOIEvent {
  AOIEventType type = AOIEventType::kMove;
  uint64_t watcher_id = 0;
  uint64_t target_id = 0;
  int32_t x = 0;
  int32_t y = 0;
};

void DispatchAOIEvents(const AOIManager::AOICallback& callback,
                       const std::vector<PendingAOIEvent>& events) {
  RecordAoiEventBatch(events.size());

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
  SetHotGridDensity(0);
}

void AOIManager::SetCallback(AOICallback callback) {
  std::lock_guard<std::mutex> lock(callback_mutex_);
  callback_ = std::move(callback);
}

void AOIManager::Enter(uint64_t entity_id, int32_t x, int32_t y) {
  std::vector<PendingAOIEvent> pending_events;
  std::unordered_set<uint64_t> nearby_entity_ids;

  const auto wait_started_at = SteadyClock::now();
  const size_t entity_shard_idx = EntityShardIndex(entity_id);
  EntityShard& entity_shard = entity_shards_[entity_shard_idx];
  std::unique_lock<std::shared_mutex> entity_lock(entity_shard.mutex);

  if (entity_shard.entities.find(entity_id) != entity_shard.entities.end()) {
    RecordAoiLockWait(wait_started_at);
    return;
  }

  const GridId grid = ToGridId(x, y);
  GridArray surrounding{};
  const size_t surrounding_count = FillSurroundingGrids(grid, surrounding);
  auto grid_shard_indices = CollectGridShardIndices(surrounding, surrounding_count);
  auto grid_locks = LockGridShardsUnique(grid_shard_indices);
  RecordAoiLockWait(wait_started_at);

  entity_shard.entities.emplace(entity_id, EntityPosition{x, y, grid});
  AddToGrid(entity_id, grid);
  CollectEntitiesFromGridsUnsafe(surrounding, surrounding_count, nearby_entity_ids);

  grid_locks.clear();
  entity_lock.unlock();

  nearby_entity_ids.erase(entity_id);
  for (uint64_t other_id : nearby_entity_ids) {
    pending_events.push_back(
        PendingAOIEvent{AOIEventType::kEnter, other_id, entity_id, x, y});
    EntityPosition other_pos;
    if (TryGetEntityPositionSnapshot(other_id, other_pos)) {
      pending_events.push_back(
          PendingAOIEvent{AOIEventType::kEnter, entity_id, other_id,
                          other_pos.x, other_pos.y});
    }
  }

  DispatchAOIEvents(SnapshotCallback(), pending_events);
}

void AOIManager::Leave(uint64_t entity_id) {
  std::vector<PendingAOIEvent> pending_events;
  std::unordered_set<uint64_t> nearby_entity_ids;
  bool should_recompute_hot_density = false;

  const auto wait_started_at = SteadyClock::now();
  const size_t entity_shard_idx = EntityShardIndex(entity_id);
  EntityShard& entity_shard = entity_shards_[entity_shard_idx];
  std::unique_lock<std::shared_mutex> entity_lock(entity_shard.mutex);

  auto it = entity_shard.entities.find(entity_id);
  if (it == entity_shard.entities.end()) {
    RecordAoiLockWait(wait_started_at);
    return;
  }

  const EntityPosition pos = it->second;
  GridArray surrounding{};
  const size_t surrounding_count = FillSurroundingGrids(pos.grid, surrounding);
  auto grid_shard_indices = CollectGridShardIndices(surrounding, surrounding_count);
  auto grid_locks = LockGridShardsUnique(grid_shard_indices);
  RecordAoiLockWait(wait_started_at);

  CollectEntitiesFromGridsUnsafe(surrounding, surrounding_count, nearby_entity_ids);
  should_recompute_hot_density = RemoveFromGrid(entity_id, pos.grid);
  entity_shard.entities.erase(it);

  grid_locks.clear();
  entity_lock.unlock();

  if (should_recompute_hot_density) {
    RecomputeHotGridDensity();
  }

  nearby_entity_ids.erase(entity_id);
  for (uint64_t other_id : nearby_entity_ids) {
    pending_events.push_back(PendingAOIEvent{
        AOIEventType::kLeave, other_id, entity_id, pos.x, pos.y});
  }

  DispatchAOIEvents(SnapshotCallback(), pending_events);
}

void AOIManager::Move(uint64_t entity_id, int32_t new_x, int32_t new_y) {
  std::vector<PendingAOIEvent> pending_events;
  std::unordered_set<uint64_t> leave_ids;
  std::unordered_set<uint64_t> enter_ids;
  std::unordered_set<uint64_t> move_ids;
  bool should_recompute_hot_density = false;
  bool crossed_grid = false;

  const auto wait_started_at = SteadyClock::now();
  const size_t entity_shard_idx = EntityShardIndex(entity_id);
  EntityShard& entity_shard = entity_shards_[entity_shard_idx];
  std::unique_lock<std::shared_mutex> entity_lock(entity_shard.mutex);

  auto it = entity_shard.entities.find(entity_id);
  if (it == entity_shard.entities.end()) {
    RecordAoiLockWait(wait_started_at);
    return;
  }

  const EntityPosition old_pos = it->second;
  const GridId new_grid = ToGridId(new_x, new_y);
  it->second = EntityPosition{new_x, new_y, new_grid};

  if (old_pos.grid == new_grid) {
    GridArray surrounding{};
    const size_t surrounding_count = FillSurroundingGrids(new_grid, surrounding);
    auto grid_shard_indices = CollectGridShardIndices(surrounding, surrounding_count);
    auto grid_locks = LockGridShardsShared(grid_shard_indices);
    RecordAoiLockWait(wait_started_at);
    CollectEntitiesFromGridsUnsafe(surrounding, surrounding_count, move_ids);
    grid_locks.clear();
  } else {
    crossed_grid = true;
    GridArray old_surrounding{};
    GridArray new_surrounding{};
    const size_t old_surrounding_count =
        FillSurroundingGrids(old_pos.grid, old_surrounding);
    const size_t new_surrounding_count =
        FillSurroundingGrids(new_grid, new_surrounding);

    auto grid_shard_indices = CollectGridShardIndices(
        old_surrounding, old_surrounding_count,
        new_surrounding, new_surrounding_count);
    auto grid_locks = LockGridShardsUnique(grid_shard_indices);
    RecordAoiLockWait(wait_started_at);

    should_recompute_hot_density = RemoveFromGrid(entity_id, old_pos.grid);
    AddToGrid(entity_id, new_grid);

    for (size_t i = 0; i < old_surrounding_count; ++i) {
      const GridId& grid = old_surrounding[i];
      if (ContainsGrid(new_surrounding, new_surrounding_count, grid)) {
        continue;
      }
      CollectEntitiesInGridUnsafe(grid, leave_ids);
    }

    for (size_t i = 0; i < new_surrounding_count; ++i) {
      const GridId& grid = new_surrounding[i];
      if (ContainsGrid(old_surrounding, old_surrounding_count, grid)) {
        continue;
      }
      CollectEntitiesInGridUnsafe(grid, enter_ids);
    }

    for (size_t i = 0; i < new_surrounding_count; ++i) {
      const GridId& grid = new_surrounding[i];
      if (!ContainsGrid(old_surrounding, old_surrounding_count, grid)) {
        continue;
      }
      CollectEntitiesInGridUnsafe(grid, move_ids);
    }

    grid_locks.clear();
  }

  entity_lock.unlock();

  leave_ids.erase(entity_id);
  enter_ids.erase(entity_id);
  move_ids.erase(entity_id);

  if (should_recompute_hot_density) {
    RecomputeHotGridDensity();
  }

  if (!crossed_grid) {
    for (uint64_t other_id : move_ids) {
      pending_events.push_back(PendingAOIEvent{
          AOIEventType::kMove, other_id, entity_id, new_x, new_y});
    }
    DispatchAOIEvents(SnapshotCallback(), pending_events);
    return;
  }

  for (uint64_t other_id : leave_ids) {
    pending_events.push_back(PendingAOIEvent{
        AOIEventType::kLeave, other_id, entity_id, new_x, new_y});
    EntityPosition other_pos;
    if (TryGetEntityPositionSnapshot(other_id, other_pos)) {
      pending_events.push_back(PendingAOIEvent{
          AOIEventType::kLeave, entity_id, other_id, other_pos.x, other_pos.y});
    }
  }

  for (uint64_t other_id : enter_ids) {
    pending_events.push_back(PendingAOIEvent{
        AOIEventType::kEnter, other_id, entity_id, new_x, new_y});
    EntityPosition other_pos;
    if (TryGetEntityPositionSnapshot(other_id, other_pos)) {
      pending_events.push_back(PendingAOIEvent{
          AOIEventType::kEnter, entity_id, other_id, other_pos.x, other_pos.y});
    }
  }

  for (uint64_t other_id : move_ids) {
    pending_events.push_back(PendingAOIEvent{
        AOIEventType::kMove, other_id, entity_id, new_x, new_y});
  }

  DispatchAOIEvents(SnapshotCallback(), pending_events);
}

std::vector<uint64_t> AOIManager::GetEntitiesInView(int32_t x, int32_t y) const {
  const auto wait_started_at = SteadyClock::now();
  GridArray surrounding{};
  const GridId grid = ToGridId(x, y);
  const size_t surrounding_count = FillSurroundingGrids(grid, surrounding);
  auto grid_shard_indices = CollectGridShardIndices(surrounding, surrounding_count);
  auto grid_locks = LockGridShardsShared(grid_shard_indices);
  RecordAoiLockWait(wait_started_at);

  std::vector<uint64_t> result;
  size_t reserve_count = 0;
  for (size_t i = 0; i < surrounding_count; ++i) {
    const GridId& current = surrounding[i];
    const GridShard& grid_shard = grid_shards_[GridShardIndex(current)];
    auto it = grid_shard.grids.find(current);
    if (it == grid_shard.grids.end()) {
      continue;
    }
    reserve_count += it->second.size();
  }
  result.reserve(reserve_count);

  for (size_t i = 0; i < surrounding_count; ++i) {
    const GridId& current = surrounding[i];
    const GridShard& grid_shard = grid_shards_[GridShardIndex(current)];
    auto it = grid_shard.grids.find(current);
    if (it == grid_shard.grids.end()) {
      continue;
    }
    result.insert(result.end(), it->second.begin(), it->second.end());
  }

  return result;
}

std::vector<uint64_t> AOIManager::GetEntitiesInViewOf(uint64_t entity_id) const {
  const auto wait_started_at = SteadyClock::now();

  EntityPosition center{};
  const size_t entity_shard_idx = EntityShardIndex(entity_id);
  {
    const EntityShard& entity_shard = entity_shards_[entity_shard_idx];
    std::shared_lock<std::shared_mutex> entity_lock(entity_shard.mutex);
    auto it = entity_shard.entities.find(entity_id);
    if (it == entity_shard.entities.end()) {
      RecordAoiLockWait(wait_started_at);
      return {};
    }
    center = it->second;
  }

  GridArray surrounding{};
  const size_t surrounding_count =
      FillSurroundingGrids(center.grid, surrounding);
  auto grid_shard_indices = CollectGridShardIndices(surrounding, surrounding_count);
  auto grid_locks = LockGridShardsShared(grid_shard_indices);
  RecordAoiLockWait(wait_started_at);

  std::vector<uint64_t> result;
  size_t reserve_count = 0;
  for (size_t i = 0; i < surrounding_count; ++i) {
    const GridId& current = surrounding[i];
    const GridShard& grid_shard = grid_shards_[GridShardIndex(current)];
    auto it = grid_shard.grids.find(current);
    if (it == grid_shard.grids.end()) {
      continue;
    }
    reserve_count += it->second.size();
  }
  result.reserve(reserve_count);

  for (size_t i = 0; i < surrounding_count; ++i) {
    const GridId& current = surrounding[i];
    const GridShard& grid_shard = grid_shards_[GridShardIndex(current)];
    auto it = grid_shard.grids.find(current);
    if (it == grid_shard.grids.end()) {
      continue;
    }
    for (uint64_t id : it->second) {
      if (id != entity_id) {
        result.push_back(id);
      }
    }
  }

  return result;
}

bool AOIManager::InViewOf(uint64_t entity_a, uint64_t entity_b) const {
  const auto wait_started_at = SteadyClock::now();
  const size_t shard_a = EntityShardIndex(entity_a);
  const size_t shard_b = EntityShardIndex(entity_b);

  EntityPosition pos_a{};
  EntityPosition pos_b{};
  if (shard_a == shard_b) {
    const EntityShard& shared_shard = entity_shards_[shard_a];
    std::shared_lock<std::shared_mutex> lock(shared_shard.mutex);
    RecordAoiLockWait(wait_started_at);

    auto it_a = shared_shard.entities.find(entity_a);
    auto it_b = shared_shard.entities.find(entity_b);
    if (it_a == shared_shard.entities.end() || it_b == shared_shard.entities.end()) {
      return false;
    }
    pos_a = it_a->second;
    pos_b = it_b->second;
  } else {
    const size_t first = std::min(shard_a, shard_b);
    const size_t second = std::max(shard_a, shard_b);
    std::shared_lock<std::shared_mutex> first_lock(entity_shards_[first].mutex);
    std::shared_lock<std::shared_mutex> second_lock(entity_shards_[second].mutex);
    RecordAoiLockWait(wait_started_at);

    const auto& shard_a_map = entity_shards_[shard_a].entities;
    const auto& shard_b_map = entity_shards_[shard_b].entities;
    auto it_a = shard_a_map.find(entity_a);
    auto it_b = shard_b_map.find(entity_b);
    if (it_a == shard_a_map.end() || it_b == shard_b_map.end()) {
      return false;
    }
    pos_a = it_a->second;
    pos_b = it_b->second;
  }

  const int32_t dx = std::abs(pos_a.grid.gx - pos_b.grid.gx);
  const int32_t dy = std::abs(pos_a.grid.gy - pos_b.grid.gy);
  return dx <= 1 && dy <= 1;
}

size_t AOIManager::EntityCount() const {
  const auto wait_started_at = SteadyClock::now();
  auto locks = LockAllEntityShardsShared();
  RecordAoiLockWait(wait_started_at);

  size_t total = 0;
  for (const auto& shard : entity_shards_) {
    total += shard.entities.size();
  }
  return total;
}

bool AOIManager::GetEntityPosition(uint64_t entity_id, int32_t& out_x,
                                   int32_t& out_y) const {
  const auto wait_started_at = SteadyClock::now();
  const size_t shard_index = EntityShardIndex(entity_id);
  const EntityShard& shard = entity_shards_[shard_index];
  std::shared_lock<std::shared_mutex> lock(shard.mutex);
  RecordAoiLockWait(wait_started_at);

  auto it = shard.entities.find(entity_id);
  if (it == shard.entities.end()) {
    return false;
  }
  out_x = it->second.x;
  out_y = it->second.y;
  return true;
}

void AOIManager::Clear() {
  const auto wait_started_at = SteadyClock::now();
  auto entity_locks = LockAllEntityShardsUnique();
  auto grid_locks = LockAllGridShardsUnique();
  RecordAoiLockWait(wait_started_at);

  for (auto& shard : entity_shards_) {
    shard.entities.clear();
  }
  for (auto& shard : grid_shards_) {
    shard.grids.clear();
  }
  SetHotGridDensity(0);
}

size_t AOIManager::FillSurroundingGrids(const GridId& center,
                                        GridArray& out) const {
  size_t count = 0;
  for (int32_t dy = -1; dy <= 1; ++dy) {
    for (int32_t dx = -1; dx <= 1; ++dx) {
      const int32_t gx = center.gx + dx;
      const int32_t gy = center.gy + dy;

      if (gx >= 0 && gx < grid_count_x_ && gy >= 0 && gy < grid_count_y_) {
        out[count++] = {gx, gy};
      }
    }
  }
  return count;
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

size_t AOIManager::EntityShardIndex(uint64_t entity_id) const {
  return static_cast<size_t>(entity_id % kEntityShardCount);
}

size_t AOIManager::GridShardIndex(const GridId& grid) const {
  return GridIdHash{}(grid) % kGridShardCount;
}

std::vector<size_t> AOIManager::CollectGridShardIndices(const GridArray& grids,
                                                        size_t count) const {
  std::vector<size_t> indices;
  indices.reserve(count);
  for (size_t i = 0; i < count; ++i) {
    indices.push_back(GridShardIndex(grids[i]));
  }
  std::sort(indices.begin(), indices.end());
  indices.erase(std::unique(indices.begin(), indices.end()), indices.end());
  return indices;
}

std::vector<size_t> AOIManager::CollectGridShardIndices(const GridArray& lhs,
                                                        size_t lhs_count,
                                                        const GridArray& rhs,
                                                        size_t rhs_count) const {
  std::vector<size_t> indices;
  indices.reserve(lhs_count + rhs_count);
  for (size_t i = 0; i < lhs_count; ++i) {
    indices.push_back(GridShardIndex(lhs[i]));
  }
  for (size_t i = 0; i < rhs_count; ++i) {
    indices.push_back(GridShardIndex(rhs[i]));
  }
  std::sort(indices.begin(), indices.end());
  indices.erase(std::unique(indices.begin(), indices.end()), indices.end());
  return indices;
}

std::vector<std::shared_lock<std::shared_mutex>> AOIManager::LockGridShardsShared(
    const std::vector<size_t>& shard_indices) const {
  std::vector<std::shared_lock<std::shared_mutex>> locks;
  locks.reserve(shard_indices.size());
  for (size_t index : shard_indices) {
    locks.emplace_back(grid_shards_[index].mutex);
  }
  return locks;
}

std::vector<std::unique_lock<std::shared_mutex>> AOIManager::LockGridShardsUnique(
    const std::vector<size_t>& shard_indices) {
  std::vector<std::unique_lock<std::shared_mutex>> locks;
  locks.reserve(shard_indices.size());
  for (size_t index : shard_indices) {
    locks.emplace_back(grid_shards_[index].mutex);
  }
  return locks;
}

std::vector<std::shared_lock<std::shared_mutex>>
AOIManager::LockAllEntityShardsShared() const {
  std::vector<std::shared_lock<std::shared_mutex>> locks;
  locks.reserve(kEntityShardCount);
  for (const auto& shard : entity_shards_) {
    locks.emplace_back(shard.mutex);
  }
  return locks;
}

std::vector<std::unique_lock<std::shared_mutex>>
AOIManager::LockAllEntityShardsUnique() {
  std::vector<std::unique_lock<std::shared_mutex>> locks;
  locks.reserve(kEntityShardCount);
  for (auto& shard : entity_shards_) {
    locks.emplace_back(shard.mutex);
  }
  return locks;
}

std::vector<std::shared_lock<std::shared_mutex>>
AOIManager::LockAllGridShardsShared() const {
  std::vector<std::shared_lock<std::shared_mutex>> locks;
  locks.reserve(kGridShardCount);
  for (const auto& shard : grid_shards_) {
    locks.emplace_back(shard.mutex);
  }
  return locks;
}

std::vector<std::unique_lock<std::shared_mutex>> AOIManager::LockAllGridShardsUnique() {
  std::vector<std::unique_lock<std::shared_mutex>> locks;
  locks.reserve(kGridShardCount);
  for (auto& shard : grid_shards_) {
    locks.emplace_back(shard.mutex);
  }
  return locks;
}

bool AOIManager::TryGetEntityPositionSnapshot(uint64_t entity_id,
                                              EntityPosition& out) const {
  const size_t shard_index = EntityShardIndex(entity_id);
  const EntityShard& shard = entity_shards_[shard_index];
  std::shared_lock<std::shared_mutex> lock(shard.mutex);
  auto it = shard.entities.find(entity_id);
  if (it == shard.entities.end()) {
    return false;
  }
  out = it->second;
  return true;
}

AOIManager::AOICallback AOIManager::SnapshotCallback() const {
  std::lock_guard<std::mutex> lock(callback_mutex_);
  return callback_;
}

void AOIManager::CollectEntitiesInGridUnsafe(
    const GridId& grid, std::unordered_set<uint64_t>& out) const {
  const GridShard& shard = grid_shards_[GridShardIndex(grid)];
  auto it = shard.grids.find(grid);
  if (it == shard.grids.end()) {
    return;
  }
  out.insert(it->second.begin(), it->second.end());
}

void AOIManager::CollectEntitiesFromGridsUnsafe(
    const GridArray& grids, size_t count, std::unordered_set<uint64_t>& out) const {
  for (size_t i = 0; i < count; ++i) {
    CollectEntitiesInGridUnsafe(grids[i], out);
  }
}

void AOIManager::AddToGrid(uint64_t entity_id, const GridId& grid) {
  GridShard& shard = grid_shards_[GridShardIndex(grid)];
  auto& entities = shard.grids[grid];
  entities.insert(entity_id);
  ObserveHotGridDensity(entities.size());
}

bool AOIManager::RemoveFromGrid(uint64_t entity_id, const GridId& grid) {
  GridShard& shard = grid_shards_[GridShardIndex(grid)];
  auto it = shard.grids.find(grid);
  if (it == shard.grids.end()) {
    return false;
  }

  const size_t old_size = it->second.size();
  it->second.erase(entity_id);
  const size_t new_size = it->second.size();
  if (it->second.empty()) {
    shard.grids.erase(it);
  }

  const size_t current_hot = hot_grid_density_.load(std::memory_order_relaxed);
  return old_size >= current_hot && new_size < old_size;
}

void AOIManager::ObserveHotGridDensity(size_t density) {
  size_t current = hot_grid_density_.load(std::memory_order_relaxed);
  while (current < density &&
         !hot_grid_density_.compare_exchange_weak(
             current, density, std::memory_order_relaxed)) {
  }
  if (current < density) {
    UpdateAoiHotGridDensityGauge(density);
  }
}

void AOIManager::SetHotGridDensity(size_t density) {
  hot_grid_density_.store(density, std::memory_order_relaxed);
  UpdateAoiHotGridDensityGauge(density);
}

void AOIManager::RecomputeHotGridDensity() {
  auto locks = LockAllGridShardsShared();
  size_t max_density = 0;
  for (const auto& shard : grid_shards_) {
    for (const auto& entry : shard.grids) {
      max_density = std::max(max_density, entry.second.size());
    }
  }
  SetHotGridDensity(max_density);
}

}  // namespace mir2::game::map
