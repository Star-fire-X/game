/**
 * @file map_context_service.cc
 * @brief Map runtime context binding service implementation.
 */

#include "game/map/map_context_service.h"

#include "game/map/map_instance.h"
#include "game/map/scene_manager.h"

namespace mir2::game::map {

MapContextService::MapContextService(SceneManager& scene_manager)
    : scene_manager_(scene_manager) {}

MapInstance* MapContextService::GetMap(uint32_t map_id) const {
  return scene_manager_.GetMap(static_cast<int32_t>(map_id));
}

MapInstance* MapContextService::GetMap(entt::registry& registry) const {
  uint32_t map_id = 0;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = registry_map_ids_.find(&registry);
    if (it == registry_map_ids_.end()) {
      return nullptr;
    }
    map_id = it->second;
  }
  return GetMap(map_id);
}

void MapContextService::BindRegistry(entt::registry& registry, uint32_t map_id) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    registry_map_ids_[&registry] = map_id;
  }

  registry.ctx().insert_or_assign<MapInstance*>(GetMap(map_id));
}

}  // namespace mir2::game::map
