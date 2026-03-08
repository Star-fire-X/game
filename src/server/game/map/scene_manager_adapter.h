/**
 * @file scene_manager_adapter.h
 * @brief SceneManager adapter for IWorldMapPort.
 */

#ifndef MIR2_SERVER_GAME_MAP_SCENE_MANAGER_ADAPTER_H_
#define MIR2_SERVER_GAME_MAP_SCENE_MANAGER_ADAPTER_H_

#include "game/ports/i_world_map_port.h"

namespace mir2::game::map {

class SceneManager;

class SceneManagerAdapter final : public ports::IWorldMapPort {
 public:
  explicit SceneManagerAdapter(SceneManager& scene_manager);

  bool MapExists(int32_t map_id) const override;
  bool AddEntityToMap(int32_t map_id,
                      entt::entity entity,
                      int32_t x,
                      int32_t y) override;
  bool UpdateEntityPosition(entt::entity entity,
                            int32_t new_x,
                            int32_t new_y) override;

 private:
  SceneManager& scene_manager_;
};

}  // namespace mir2::game::map

#endif  // MIR2_SERVER_GAME_MAP_SCENE_MANAGER_ADAPTER_H_
