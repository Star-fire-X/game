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

bool SceneManagerAdapter::UpdateEntityPosition(int32_t map_id,
                                               entt::entity entity,
                                               int32_t new_x,
                                               int32_t new_y) {
  return scene_manager_.UpdateEntityPosition(map_id, entity, new_x, new_y);
}

bool SceneManagerAdapter::RemoveEntityFromMap(int32_t map_id,
                                              entt::entity entity) {
  return scene_manager_.RemoveEntityFromMap(map_id, entity);
}

bool SceneManagerAdapter::MoveEntityToMap(int32_t from_map_id,
                                          int32_t to_map_id,
                                          entt::entity entity,
                                          int32_t x,
                                          int32_t y) {
  return scene_manager_.MoveEntityToMap(from_map_id, to_map_id, entity, x, y);
}

}  // namespace mir2::game::map
