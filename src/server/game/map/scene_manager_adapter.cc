#include "game/map/scene_manager_adapter.h"

#include "game/map/scene_manager.h"

namespace mir2::game::map {

SceneManagerAdapter::SceneManagerAdapter(SceneManager& scene_manager)
    : scene_manager_(scene_manager) {}

bool SceneManagerAdapter::MapExists(int32_t map_id) const {
  return scene_manager_.GetMap(map_id) != nullptr;
}

bool SceneManagerAdapter::AddEntityToMap(int32_t map_id,
                                         entt::entity entity,
                                         int32_t x,
                                         int32_t y) {
  return scene_manager_.AddEntityToMap(map_id, entity, x, y);
}

bool SceneManagerAdapter::UpdateEntityPosition(entt::entity entity,
                                               int32_t new_x,
                                               int32_t new_y) {
  return scene_manager_.UpdateEntityPosition(entity, new_x, new_y);
}

}  // namespace mir2::game::map
