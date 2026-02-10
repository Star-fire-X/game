/**
 * @file map_instance.cc
 * @brief 地图实例实现
 */

#include "game/map/map_instance.h"

#include <algorithm>
#include <atomic>

namespace mir2::game::map {

MapInstance::MapInstance(int32_t map_id, int32_t map_width, int32_t map_height,
                         int32_t grid_size, std::vector<uint8_t> walkability,
                         std::optional<MapTileData> tile_data)
    : map_id_(map_id),
      map_width_(map_width),
      map_height_(map_height),
      aoi_manager_(std::make_unique<AOIManager>(map_width, map_height, grid_size)),
      door_manager_(std::make_unique<DoorManager>()) {
  const size_t expected_size =
      static_cast<size_t>(map_width_) * static_cast<size_t>(map_height_);
  if (tile_data) {
    tile_data_ = std::move(tile_data);
    door_manager_->BuildDoorIndex(tile_data_->width, tile_data_->height,
                                  tile_data_->tiles);
    if (tile_data_->walkable.size() != expected_size) {
      if (walkability.size() == expected_size) {
        tile_data_->walkable = std::move(walkability);
      } else {
        tile_data_->walkable.assign(expected_size, 1);
      }
    }
  } else {
    if (walkability.size() == expected_size) {
      walkability_ = std::move(walkability);
    } else {
      walkability_.assign(expected_size, 1);
    }
  }

  // 设置 AOI 回调适配器
  aoi_manager_->SetCallback([this](AOIEventType event_type, uint64_t watcher_id,
                                    uint64_t target_id, int32_t x, int32_t y) {
    OnAOIEvent(event_type, watcher_id, target_id, x, y);
  });

  area_event_processor_.SetAOIManager(aoi_manager_.get());
  std::atomic_store_explicit(
      &attributes_snapshot_,
      std::shared_ptr<const MapAttributes>(
          std::make_shared<MapAttributes>(attributes_)),
      std::memory_order_release);
}

bool MapInstance::IsValidPosition(int32_t x, int32_t y) const {
  return x >= 0 && x < map_width_ && y >= 0 && y < map_height_;
}

bool MapInstance::IsWalkable(int32_t x, int32_t y) const {
  std::shared_lock<std::shared_mutex> lock(mutex_);
  if (!IsValidPosition(x, y)) {
    return false;
  }
  const auto& walkability = WalkabilityDataUnsafe();
  // Use size_t intermediates to avoid int32 overflow when computing index.
  const size_t index = static_cast<size_t>(y) * static_cast<size_t>(map_width_) +
                       static_cast<size_t>(x);
  if (index >= walkability.size()) {
    return false;
  }
  return walkability[index] != 0;
}

bool MapInstance::IsFlyable(int32_t x, int32_t y) const {
  std::shared_lock<std::shared_mutex> lock(mutex_);
  if (tile_data_) {
    return tile_data_->IsFlyable(x, y);
  }
  if (!IsValidPosition(x, y)) {
    return false;
  }
  const auto& walkability = WalkabilityDataUnsafe();
  const size_t index = static_cast<size_t>(y) * static_cast<size_t>(map_width_) +
                       static_cast<size_t>(x);
  if (index >= walkability.size()) {
    return false;
  }
  return walkability[index] != 0;
}

void MapInstance::SetWalkable(int32_t x, int32_t y, bool walkable) {
  std::unique_lock<std::shared_mutex> lock(mutex_);
  if (!IsValidPosition(x, y)) {
    return;
  }
  auto& walkability_data = MutableWalkabilityDataUnsafe();
  const size_t index = static_cast<size_t>(y) * static_cast<size_t>(map_width_) +
                       static_cast<size_t>(x);
  if (index >= walkability_data.size()) {
    return;
  }
  const uint8_t value = static_cast<uint8_t>(walkable ? 1 : 0);
  walkability_data[index] = value;
}

bool MapInstance::HasDoor(int32_t x, int32_t y) const {
  std::shared_lock<std::shared_mutex> lock(mutex_);
  if (!tile_data_) {
    return false;
  }
  const TileInfo* tile = tile_data_->GetTile(x, y);
  if (!tile) {
    return false;
  }
  return tile->door_index != 0;
}

uint8_t MapInstance::GetDoorIndex(int32_t x, int32_t y) const {
  std::shared_lock<std::shared_mutex> lock(mutex_);
  if (!tile_data_) {
    return 0;
  }
  const TileInfo* tile = tile_data_->GetTile(x, y);
  if (!tile) {
    return 0;
  }
  return tile->door_index;
}

bool MapInstance::IsDoorOpen(int32_t x, int32_t y) const {
  std::shared_lock<std::shared_mutex> lock(mutex_);
  if (!door_manager_) {
    return false;
  }
  return door_manager_->IsDoorOpen(x, y);
}

const TileInfo* MapInstance::GetTileInfo(int32_t x, int32_t y) const {
  std::shared_lock<std::shared_mutex> lock(mutex_);
  if (!tile_data_) {
    return nullptr;
  }
  return tile_data_->GetTile(x, y);
}

bool MapInstance::InSafeZone(int32_t x, int32_t y) const {
  std::shared_lock<std::shared_mutex> lock(mutex_);

  if (!IsValidPosition(x, y)) {
    return false;
  }

  if (attributes_.is_safe_zone) {
    return true;
  }

  if (attributes_.safe_zones.empty()) {
    return false;
  }

  if (safe_zone_index_) {
    auto candidates = safe_zone_index_->GetEntitiesInView(x, y);
    for (uint64_t id : candidates) {
      if (id < attributes_.safe_zones.size() &&
          attributes_.safe_zones[id].Contains(x, y)) {
        return true;
      }
    }
    return false;
  }

  for (const auto& zone : attributes_.safe_zones) {
    if (zone.Contains(x, y)) {
      return true;
    }
  }

  return false;
}

bool MapInstance::CanPK() const {
  const auto attrs =
      std::atomic_load_explicit(&attributes_snapshot_, std::memory_order_acquire);
  if (!attrs) {
    return false;
  }
  if (attrs->is_safe_zone) {
    return false;
  }
  return attrs->is_pk_zone;
}

bool MapInstance::CheckLevelRequirement(int32_t level) const {
  const auto attrs =
      std::atomic_load_explicit(&attributes_snapshot_, std::memory_order_acquire);
  if (!attrs) {
    return false;
  }
  if (level < attrs->min_level) {
    return false;
  }
  if (level > attrs->max_level) {
    return false;
  }
  return true;
}

MapAttributes MapInstance::GetAttributes() const {
  const auto attrs =
      std::atomic_load_explicit(&attributes_snapshot_, std::memory_order_acquire);
  if (attrs) {
    return *attrs;
  }

  std::shared_lock<std::shared_mutex> lock(mutex_);
  return attributes_;
}

void MapInstance::SetAttributes(const MapAttributes& attrs) {
  std::unique_lock<std::shared_mutex> lock(mutex_);
  attributes_ = attrs;
  RebuildSafeZoneIndex();
  std::atomic_store_explicit(
      &attributes_snapshot_,
      std::shared_ptr<const MapAttributes>(
          std::make_shared<MapAttributes>(attributes_)),
      std::memory_order_release);
}

void MapInstance::SetAOICallback(AOIEventCallback callback) {
  std::lock_guard<std::mutex> lock(aoi_callback_mutex_);
  aoi_callback_ = std::move(callback);
}

void MapInstance::SetEventBus(ecs::EventBus* event_bus) {
  std::unique_lock<std::shared_mutex> lock(mutex_);
  area_event_processor_.SetEventBus(event_bus);
}

void MapInstance::AddAreaTrigger(const AreaTrigger& trigger) {
  std::unique_lock<std::shared_mutex> lock(mutex_);
  area_event_processor_.AddTrigger(trigger);
}

void MapInstance::RemoveAreaTrigger(uint32_t trigger_id) {
  std::unique_lock<std::shared_mutex> lock(mutex_);
  area_event_processor_.RemoveTrigger(trigger_id);
}

void MapInstance::AddContinuousAreaEffect(const ContinuousAreaEffect& effect) {
  std::unique_lock<std::shared_mutex> lock(mutex_);
  area_event_processor_.AddContinuousEffect(effect);
}

void MapInstance::UpdateAreaEvents(float delta_time, entt::registry& registry) {
  std::unique_lock<std::shared_mutex> lock(mutex_);
  area_event_processor_.Update(delta_time, registry);
}

bool MapInstance::AddEntity(entt::entity entity, int32_t x, int32_t y) {
  std::lock_guard<std::mutex> entity_ops_lock(entity_ops_mutex_);

  {
    std::unique_lock<std::shared_mutex> lock(mutex_);

    // 检查坐标是否合法
    if (!IsValidPosition(x, y)) {
      return false;
    }

    // 检查实体是否已存在
    if (entities_.find(entity) != entities_.end()) {
      return false;
    }

    // 添加到本地集合
    entities_.insert(entity);
  }

  // AOI 回调可能重入 MapInstance，避免在持有 mutex_ 时调用。
  aoi_manager_->Enter(EntityToId(entity), x, y);

  std::unique_lock<std::shared_mutex> lock(mutex_);
  if (entities_.find(entity) == entities_.end()) {
    lock.unlock();
    aoi_manager_->Leave(EntityToId(entity));
    return false;
  }
  area_event_processor_.CheckPlayerEnterExit(entity, -1, -1, x, y);

  return true;
}

bool MapInstance::RemoveEntity(entt::entity entity) {
  std::lock_guard<std::mutex> entity_ops_lock(entity_ops_mutex_);

  {
    std::unique_lock<std::shared_mutex> lock(mutex_);

    // 检查实体是否存在
    auto it = entities_.find(entity);
    if (it == entities_.end()) {
      return false;
    }

    // 从本地集合移除
    entities_.erase(it);
  }

  int32_t old_x = -1;
  int32_t old_y = -1;
  aoi_manager_->GetEntityPosition(EntityToId(entity), old_x, old_y);

  // AOI 回调可能重入 MapInstance，避免在持有 mutex_ 时调用。
  aoi_manager_->Leave(EntityToId(entity));

  std::unique_lock<std::shared_mutex> lock(mutex_);
  area_event_processor_.CheckPlayerEnterExit(entity, old_x, old_y, -1, -1);

  return true;
}

bool MapInstance::UpdateEntityPosition(entt::entity entity, int32_t new_x,
                                        int32_t new_y) {
  std::lock_guard<std::mutex> entity_ops_lock(entity_ops_mutex_);

  {
    std::unique_lock<std::shared_mutex> lock(mutex_);

    // 检查坐标是否合法
    if (!IsValidPosition(new_x, new_y)) {
      return false;
    }

    // 检查实体是否存在
    if (entities_.find(entity) == entities_.end()) {
      return false;
    }
  }

  int32_t old_x = new_x;
  int32_t old_y = new_y;
  aoi_manager_->GetEntityPosition(EntityToId(entity), old_x, old_y);

  // AOI 回调可能重入 MapInstance，避免在持有 mutex_ 时调用。
  aoi_manager_->Move(EntityToId(entity), new_x, new_y);

  std::unique_lock<std::shared_mutex> lock(mutex_);
  if (entities_.find(entity) == entities_.end()) {
    return false;
  }
  area_event_processor_.CheckPlayerEnterExit(entity, old_x, old_y, new_x, new_y);

  return true;
}

std::vector<entt::entity> MapInstance::GetEntitiesInView(int32_t x,
                                                          int32_t y) const {
  std::shared_lock<std::shared_mutex> lock(mutex_);

  auto entity_ids = aoi_manager_->GetEntitiesInView(x, y);

  std::vector<entt::entity> entities;
  entities.reserve(entity_ids.size());

  for (uint64_t id : entity_ids) {
    entities.push_back(IdToEntity(id));
  }

  return entities;
}

std::vector<entt::entity> MapInstance::GetEntitiesInViewOf(
    entt::entity entity) const {
  std::shared_lock<std::shared_mutex> lock(mutex_);

  // 检查实体是否存在
  if (entities_.find(entity) == entities_.end()) {
    return {};
  }

  auto entity_ids = aoi_manager_->GetEntitiesInViewOf(EntityToId(entity));

  std::vector<entt::entity> entities;
  entities.reserve(entity_ids.size());

  for (uint64_t id : entity_ids) {
    entities.push_back(IdToEntity(id));
  }

  return entities;
}

bool MapInstance::HasEntity(entt::entity entity) const {
  std::shared_lock<std::shared_mutex> lock(mutex_);
  return entities_.find(entity) != entities_.end();
}

size_t MapInstance::EntityCount() const {
  std::shared_lock<std::shared_mutex> lock(mutex_);
  return entities_.size();
}

bool MapInstance::GetEntityPosition(entt::entity entity, int32_t& out_x,
                                     int32_t& out_y) const {
  std::shared_lock<std::shared_mutex> lock(mutex_);

  // 检查实体是否存在
  if (entities_.find(entity) == entities_.end()) {
    return false;
  }

  return aoi_manager_->GetEntityPosition(EntityToId(entity), out_x, out_y);
}

void MapInstance::Clear() {
  std::lock_guard<std::mutex> entity_ops_lock(entity_ops_mutex_);
  std::unique_lock<std::shared_mutex> lock(mutex_);
  entities_.clear();
  aoi_manager_->Clear();
}

void MapInstance::RebuildSafeZoneIndex() {
  safe_zone_index_.reset();

  if (attributes_.safe_zones.empty()) {
    return;
  }

  int32_t max_radius = 0;
  for (const auto& zone : attributes_.safe_zones) {
    if (zone.radius > max_radius) {
      max_radius = zone.radius;
    }
  }

  const int32_t grid_size = std::max(AOIManager::kDefaultGridSize, max_radius);
  safe_zone_index_ =
      std::make_unique<AOIManager>(map_width_, map_height_, grid_size);

  for (size_t i = 0; i < attributes_.safe_zones.size(); ++i) {
    const auto& zone = attributes_.safe_zones[i];
    if (!IsValidPosition(zone.x, zone.y)) {
      continue;
    }
    safe_zone_index_->Enter(static_cast<uint64_t>(i), zone.x, zone.y);
  }
}

const std::vector<uint8_t>& MapInstance::WalkabilityDataUnsafe() const {
  if (tile_data_) {
    return tile_data_->walkable;
  }
  return walkability_;
}

std::vector<uint8_t>& MapInstance::MutableWalkabilityDataUnsafe() {
  if (tile_data_) {
    return tile_data_->walkable;
  }
  return walkability_;
}

void MapInstance::OnAOIEvent(AOIEventType event_type, uint64_t watcher_id,
                             uint64_t target_id, int32_t x, int32_t y) {
  // 转换 ID 为实体
  entt::entity watcher = IdToEntity(watcher_id);
  entt::entity target = IdToEntity(target_id);

  AOIEventCallback callback;
  {
    std::lock_guard<std::mutex> lock(aoi_callback_mutex_);
    callback = aoi_callback_;
  }

  if (callback) {
    callback(event_type, watcher, target, x, y);
  }
}

bool MapInstance::CanRecall() const {
  const auto attrs =
      std::atomic_load_explicit(&attributes_snapshot_, std::memory_order_acquire);
  return attrs ? !attrs->no_recall : false;
}

bool MapInstance::CanRandomMove() const {
  const auto attrs =
      std::atomic_load_explicit(&attributes_snapshot_, std::memory_order_acquire);
  return attrs ? !attrs->no_random_move : false;
}

bool MapInstance::IsFightZone() const {
  const auto attrs =
      std::atomic_load_explicit(&attributes_snapshot_, std::memory_order_acquire);
  return attrs ? (attrs->fight_zone || attrs->fight3_zone) : false;
}

bool MapInstance::IsMineMap() const {
  const auto attrs =
      std::atomic_load_explicit(&attributes_snapshot_, std::memory_order_acquire);
  return attrs ? attrs->mine_map > 0 : false;
}

int32_t MapInstance::GetDarkLevel() const {
  const auto attrs =
      std::atomic_load_explicit(&attributes_snapshot_, std::memory_order_acquire);
  return attrs ? attrs->dark_level : 0;
}

bool MapInstance::CheckQuestRequirement(int32_t quest_id,
                                        int32_t quest_value) const {
  const auto attrs =
      std::atomic_load_explicit(&attributes_snapshot_, std::memory_order_acquire);
  if (!attrs) {
    return true;
  }
  for (const auto& req : attrs->quest_requirements) {
    if (req.quest_id == quest_id) {
      return quest_value >= req.quest_value;
    }
  }
  return true;
}

}  // namespace mir2::game::map
