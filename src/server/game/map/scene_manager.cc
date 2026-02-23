/**
 * @file scene_manager.cc
 * @brief 场景管理器实现
 */

#include "game/map/scene_manager.h"

#include <filesystem>
#include <string>
#include <utility>
#include <vector>

#include "log/logger.h"

namespace mir2::game::map {
namespace {

std::vector<std::string> BuildMapPathCandidates(
    const SceneManager::MapConfig& config) {
  std::filesystem::path base_path;
  if (!config.map_path.empty()) {
    base_path = config.map_path;
  } else {
    base_path =
        std::filesystem::path("Map") /
        (std::to_string(config.map_id) + ".map");
  }

  std::vector<std::string> candidates;
  if (base_path.is_absolute()) {
    candidates.push_back(base_path.string());
    return candidates;
  }

  const std::filesystem::path cwd = std::filesystem::current_path();
  candidates.push_back((cwd / base_path).string());
  candidates.push_back((cwd / ".." / base_path).lexically_normal().string());
  candidates.push_back((cwd / "../.." / base_path).lexically_normal().string());
  return candidates;
}

}  // namespace

std::optional<MapTileData> SceneManager::LoadMapDataUnlocked(
    const MapConfig& config,
    int32_t& out_width,
    int32_t& out_height,
    std::vector<uint8_t>& out_walkability) const {
  out_width = config.width;
  out_height = config.height;
  out_walkability.clear();

  if (!config.load_walkability && (out_width <= 0 || out_height <= 0)) {
    SYSLOG_ERROR("SceneManager: invalid map dimensions map_id={} width={} height={} "
                 "with load_walkability=false",
                 config.map_id, out_width, out_height);
    return std::nullopt;
  }

  if (config.load_walkability) {
    for (const auto& path : BuildMapPathCandidates(config)) {
      auto tile_data = map_loader_.LoadFull(path);
      if (tile_data) {
        out_width = tile_data->width;
        out_height = tile_data->height;
        return tile_data;
      }

      auto loaded = map_loader_.Load(path);
      if (loaded) {
        out_width = loaded->width;
        out_height = loaded->height;
        out_walkability = std::move(loaded->walkable);
        return std::nullopt;
      }
    }
  }

  if (out_width <= 0 || out_height <= 0) {
    SYSLOG_ERROR("SceneManager: invalid map dimensions map_id={} width={} height={}",
                 config.map_id, out_width, out_height);
    return std::nullopt;
  }

  return std::nullopt;
}

void SceneManager::ApplyMapFixes(MapInstance* map, const MapConfig& config) {
  if (!map) {
    return;
  }

  for (const auto& [x, y] : config.fixes) {
    map->SetWalkable(x, y, false);
  }
}

MapInstance* SceneManager::CreateMap(const MapConfig& config) {
  int32_t map_width = config.width;
  int32_t map_height = config.height;
  std::vector<uint8_t> walkability;
  std::optional<MapTileData> tile_data;
  try {
    tile_data = LoadMapDataUnlocked(config, map_width, map_height, walkability);
  } catch (...) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    loading_map_ids_.erase(config.map_id);
    map_load_cv_.notify_all();
    throw;
  }

  if (map_width <= 0 || map_height <= 0) {
    return nullptr;
  }

  auto map = std::make_shared<MapInstance>(
      config.map_id,
      map_width,
      map_height,
      config.grid_size,
      std::move(walkability),
      std::move(tile_data));
  ApplyMapFixes(map.get(), config);

  std::unique_lock<std::shared_mutex> lock(mutex_);
  if (maps_.find(config.map_id) != maps_.end()) {
    return nullptr;
  }

  MapInstance* map_ptr = map.get();
  maps_[config.map_id] = std::move(map);

  MapConfig stored_config = config;
  stored_config.width = map_width;
  stored_config.height = map_height;
  map_configs_[config.map_id] = std::move(stored_config);
  return map_ptr;
}

MapInstance* SceneManager::GetOrCreateMap(const MapConfig& config) {
  {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    auto it = maps_.find(config.map_id);
    if (it != maps_.end()) {
      return it->second.get();
    }
  }

  {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    while (true) {
      auto it = maps_.find(config.map_id);
      if (it != maps_.end()) {
        return it->second.get();
      }

      const auto [loading_it, inserted] = loading_map_ids_.insert(config.map_id);
      (void)loading_it;
      if (inserted) {
        break;
      }

      map_load_cv_.wait(lock, [this, &config]() {
        return maps_.find(config.map_id) != maps_.end() ||
               loading_map_ids_.find(config.map_id) == loading_map_ids_.end();
      });
    }
  }

  int32_t map_width = config.width;
  int32_t map_height = config.height;
  std::vector<uint8_t> walkability;
  std::optional<MapTileData> tile_data;
  try {
    tile_data = LoadMapDataUnlocked(config, map_width, map_height, walkability);
  } catch (...) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    loading_map_ids_.erase(config.map_id);
    map_load_cv_.notify_all();
    throw;
  }

  if (map_width <= 0 || map_height <= 0) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    loading_map_ids_.erase(config.map_id);
    map_load_cv_.notify_all();
    return nullptr;
  }

  auto map = std::make_shared<MapInstance>(
      config.map_id,
      map_width,
      map_height,
      config.grid_size,
      std::move(walkability),
      std::move(tile_data));
  ApplyMapFixes(map.get(), config);

  std::unique_lock<std::shared_mutex> lock(mutex_);
  loading_map_ids_.erase(config.map_id);

  auto it = maps_.find(config.map_id);
  if (it != maps_.end()) {
    map_load_cv_.notify_all();
    return it->second.get();
  }

  MapInstance* map_ptr = map.get();
  maps_[config.map_id] = std::move(map);

  MapConfig stored_config = config;
  stored_config.width = map_width;
  stored_config.height = map_height;
  map_configs_[config.map_id] = std::move(stored_config);

  map_load_cv_.notify_all();
  return map_ptr;
}

MapInstance* SceneManager::GetMap(int32_t map_id) {
  std::shared_lock<std::shared_mutex> lock(mutex_);
  auto it = maps_.find(map_id);
  if (it == maps_.end()) {
    return nullptr;
  }
  return it->second.get();
}

const MapInstance* SceneManager::GetMap(int32_t map_id) const {
  std::shared_lock<std::shared_mutex> lock(mutex_);
  auto it = maps_.find(map_id);
  if (it == maps_.end()) {
    return nullptr;
  }
  return it->second.get();
}

MapInstance* SceneManager::GetMapByEntity(entt::entity entity) {
  std::shared_lock<std::shared_mutex> lock(mutex_);
  auto entity_it = entity_to_map_.find(entity);
  if (entity_it == entity_to_map_.end()) {
    return nullptr;
  }
  auto map_it = maps_.find(entity_it->second);
  if (map_it == maps_.end()) {
    return nullptr;
  }
  return map_it->second.get();
}

const MapInstance* SceneManager::GetMapByEntity(entt::entity entity) const {
  std::shared_lock<std::shared_mutex> lock(mutex_);
  auto entity_it = entity_to_map_.find(entity);
  if (entity_it == entity_to_map_.end()) {
    return nullptr;
  }
  auto map_it = maps_.find(entity_it->second);
  if (map_it == maps_.end()) {
    return nullptr;
  }
  return map_it->second.get();
}

std::optional<int32_t> SceneManager::TryGetEntityMapId(entt::entity entity) const {
  std::shared_lock<std::shared_mutex> lock(mutex_);
  auto entity_it = entity_to_map_.find(entity);
  if (entity_it == entity_to_map_.end()) {
    return std::nullopt;
  }
  if (maps_.find(entity_it->second) == maps_.end()) {
    return std::nullopt;
  }
  return entity_it->second;
}

bool SceneManager::AddEntityToMap(int32_t map_id,
                                  entt::entity entity,
                                  int32_t x,
                                  int32_t y) {
  std::lock_guard<std::mutex> entity_ops_lock(entity_ops_mutex_);

  std::shared_ptr<MapInstance> target_map;
  std::shared_ptr<MapInstance> old_map;
  {
    std::shared_lock<std::shared_mutex> lock(mutex_);

    auto map_it = maps_.find(map_id);
    if (map_it == maps_.end()) {
      return false;
    }
    target_map = map_it->second;

    auto entity_it = entity_to_map_.find(entity);
    if (entity_it != entity_to_map_.end()) {
      auto old_map_it = maps_.find(entity_it->second);
      if (old_map_it != maps_.end()) {
        old_map = old_map_it->second;
      }
    }
  }

  if (!target_map->AddEntity(entity, x, y)) {
    return false;
  }

  if (old_map && old_map.get() != target_map.get()) {
    (void)old_map->RemoveEntity(entity);
  }

  {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    auto map_it = maps_.find(map_id);
    if (map_it == maps_.end() || map_it->second.get() != target_map.get()) {
      lock.unlock();
      (void)target_map->RemoveEntity(entity);
      return false;
    }
    IndexEntity(entity, map_id);
  }

  return true;
}

bool SceneManager::RemoveEntityFromMap(entt::entity entity) {
  std::lock_guard<std::mutex> entity_ops_lock(entity_ops_mutex_);

  std::shared_ptr<MapInstance> map;
  int32_t map_id = 0;
  {
    std::shared_lock<std::shared_mutex> lock(mutex_);

    auto entity_it = entity_to_map_.find(entity);
    if (entity_it == entity_to_map_.end()) {
      return false;
    }
    map_id = entity_it->second;

    auto map_it = maps_.find(map_id);
    if (map_it == maps_.end()) {
      lock.unlock();
      std::unique_lock<std::shared_mutex> write_lock(mutex_);
      UnindexEntity(entity);
      return false;
    }

    map = map_it->second;
  }

  if (!map->RemoveEntity(entity)) {
    return false;
  }

  std::unique_lock<std::shared_mutex> lock(mutex_);
  auto entity_it = entity_to_map_.find(entity);
  if (entity_it != entity_to_map_.end() && entity_it->second == map_id) {
    UnindexEntity(entity);
  }
  return true;
}

bool SceneManager::UpdateEntityPosition(entt::entity entity,
                                        int32_t new_x,
                                        int32_t new_y) {
  std::lock_guard<std::mutex> entity_ops_lock(entity_ops_mutex_);

  std::shared_ptr<MapInstance> map;
  {
    std::shared_lock<std::shared_mutex> lock(mutex_);

    auto entity_it = entity_to_map_.find(entity);
    if (entity_it == entity_to_map_.end()) {
      return false;
    }

    auto map_it = maps_.find(entity_it->second);
    if (map_it == maps_.end()) {
      lock.unlock();
      std::unique_lock<std::shared_mutex> write_lock(mutex_);
      UnindexEntity(entity);
      return false;
    }

    map = map_it->second;
  }

  return map->UpdateEntityPosition(entity, new_x, new_y);
}

bool SceneManager::TeleportEntity(entt::entity entity,
                                  int32_t target_map_id,
                                  int32_t target_x,
                                  int32_t target_y) {
  std::lock_guard<std::mutex> entity_ops_lock(entity_ops_mutex_);

  std::shared_ptr<MapInstance> target_map;
  std::shared_ptr<MapInstance> current_map;
  {
    std::shared_lock<std::shared_mutex> lock(mutex_);

    auto target_it = maps_.find(target_map_id);
    if (target_it == maps_.end()) {
      return false;
    }
    target_map = target_it->second;

    auto entity_it = entity_to_map_.find(entity);
    if (entity_it != entity_to_map_.end()) {
      auto current_it = maps_.find(entity_it->second);
      if (current_it != maps_.end()) {
        current_map = current_it->second;
      }
    }
  }

  if (current_map && current_map.get() == target_map.get()) {
    return target_map->UpdateEntityPosition(entity, target_x, target_y);
  }

  if (!target_map->IsValidPosition(target_x, target_y)) {
    return false;
  }

  int32_t source_x = 0;
  int32_t source_y = 0;
  const bool has_source = current_map && current_map.get() != target_map.get();

  if (has_source) {
    if (!current_map->GetEntityPosition(entity, source_x, source_y)) {
      return false;
    }
    if (!current_map->RemoveEntity(entity)) {
      return false;
    }
  }

  auto rollback_to_source = [&]() -> bool {
    if (!has_source) {
      return true;
    }
    if (current_map->AddEntity(entity, source_x, source_y)) {
      return true;
    }
    SYSLOG_ERROR("SceneManager: teleport rollback failed entity={} source_map={} "
                 "target_map={} source=({}, {}) target=({}, {})",
                 static_cast<uint64_t>(entt::to_integral(entity)),
                 current_map->GetMapId(),
                 target_map_id,
                 source_x,
                 source_y,
                 target_x,
                 target_y);
    std::unique_lock<std::shared_mutex> lock(mutex_);
    UnindexEntity(entity);
    return false;
  };

  if (!target_map->AddEntity(entity, target_x, target_y)) {
    return rollback_to_source();
  }

  {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    auto target_it = maps_.find(target_map_id);
    if (target_it == maps_.end() || target_it->second.get() != target_map.get()) {
      lock.unlock();
      (void)target_map->RemoveEntity(entity);
      return rollback_to_source();
    }
    IndexEntity(entity, target_map_id);
  }

  return true;
}

bool SceneManager::DestroyMap(int32_t map_id) {
  std::lock_guard<std::mutex> entity_ops_lock(entity_ops_mutex_);

  std::unique_lock<std::shared_mutex> lock(mutex_);
  auto map_it = maps_.find(map_id);
  if (map_it == maps_.end()) {
    return false;
  }

  for (auto entity_it = entity_to_map_.begin(); entity_it != entity_to_map_.end();) {
    if (entity_it->second == map_id) {
      entity_it = entity_to_map_.erase(entity_it);
    } else {
      ++entity_it;
    }
  }

  maps_.erase(map_it);
  map_configs_.erase(map_id);
  loading_map_ids_.erase(map_id);
  map_load_cv_.notify_all();
  return true;
}

bool SceneManager::ReloadMap(int32_t map_id) {
  struct SavedEntity {
    entt::entity entity;
    int32_t x;
    int32_t y;
  };

  std::lock_guard<std::mutex> entity_ops_lock(entity_ops_mutex_);

  std::shared_ptr<MapInstance> old_map;
  MapConfig config;
  std::vector<entt::entity> entities_to_restore;

  {
    std::shared_lock<std::shared_mutex> lock(mutex_);

    auto map_it = maps_.find(map_id);
    if (map_it == maps_.end()) {
      return false;
    }
    old_map = map_it->second;

    auto config_it = map_configs_.find(map_id);
    if (config_it != map_configs_.end()) {
      config = config_it->second;
    } else {
      config.map_id = map_id;
      config.width = old_map->GetMapWidth();
      config.height = old_map->GetMapHeight();
      config.grid_size = AOIManager::kDefaultGridSize;
      config.map_path.clear();
      config.load_walkability = true;
    }

    entities_to_restore.reserve(entity_to_map_.size());
    for (const auto& [entity, entity_map_id] : entity_to_map_) {
      if (entity_map_id == map_id) {
        entities_to_restore.push_back(entity);
      }
    }
  }

  std::vector<SavedEntity> saved_entities;
  saved_entities.reserve(entities_to_restore.size());
  for (entt::entity entity : entities_to_restore) {
    int32_t x = 0;
    int32_t y = 0;
    if (old_map->GetEntityPosition(entity, x, y)) {
      saved_entities.push_back({entity, x, y});
    }
  }

  int32_t map_width = config.width;
  int32_t map_height = config.height;
  std::vector<uint8_t> walkability;
  auto tile_data =
      LoadMapDataUnlocked(config, map_width, map_height, walkability);
  if (map_width <= 0 || map_height <= 0) {
    return false;
  }

  auto new_map = std::make_shared<MapInstance>(
      config.map_id,
      map_width,
      map_height,
      config.grid_size,
      std::move(walkability),
      std::move(tile_data));
  ApplyMapFixes(new_map.get(), config);

  bool restored = true;
  std::vector<entt::entity> restored_entities;
  restored_entities.reserve(saved_entities.size());
  for (const auto& saved : saved_entities) {
    if (!new_map->AddEntity(saved.entity, saved.x, saved.y)) {
      restored = false;
      continue;
    }
    restored_entities.push_back(saved.entity);
  }

  {
    std::unique_lock<std::shared_mutex> lock(mutex_);

    auto map_it = maps_.find(map_id);
    if (map_it == maps_.end() || map_it->second.get() != old_map.get()) {
      return false;
    }

    map_it->second = new_map;

    for (auto entity_it = entity_to_map_.begin(); entity_it != entity_to_map_.end();) {
      if (entity_it->second == map_id) {
        entity_it = entity_to_map_.erase(entity_it);
      } else {
        ++entity_it;
      }
    }

    for (entt::entity entity : restored_entities) {
      IndexEntity(entity, map_id);
    }

    config.width = map_width;
    config.height = map_height;
    map_configs_[map_id] = std::move(config);
  }

  return restored;
}

size_t SceneManager::MapCount() const {
  std::shared_lock<std::shared_mutex> lock(mutex_);
  return maps_.size();
}

void SceneManager::Clear() {
  std::lock_guard<std::mutex> entity_ops_lock(entity_ops_mutex_);

  std::unique_lock<std::shared_mutex> lock(mutex_);
  loading_map_ids_.clear();
  entity_to_map_.clear();
  maps_.clear();
  map_configs_.clear();
  map_load_cv_.notify_all();
}

void SceneManager::IndexEntity(entt::entity entity, int32_t map_id) {
  entity_to_map_[entity] = map_id;
}

void SceneManager::UnindexEntity(entt::entity entity) {
  entity_to_map_.erase(entity);
}

}  // namespace mir2::game::map
