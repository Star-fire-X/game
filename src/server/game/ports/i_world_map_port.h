/**
 * @file i_world_map_port.h
 * @brief ECS <-> world map operations port interface.
 */

#ifndef MIR2_SERVER_GAME_PORTS_I_WORLD_MAP_PORT_H_
#define MIR2_SERVER_GAME_PORTS_I_WORLD_MAP_PORT_H_

#include <cstdint>

#include <entt/entt.hpp>

namespace mir2::game::ports {

class IWorldMapPort {
 public:
  virtual ~IWorldMapPort() = default;

  virtual bool MapExists(int32_t map_id) const = 0;
  virtual bool AddEntityToMap(int32_t map_id,
                              entt::entity entity,
                              int32_t x,
                              int32_t y) = 0;
  virtual bool UpdateEntityPosition(int32_t map_id,
                                    entt::entity entity,
                                    int32_t new_x,
                                    int32_t new_y) = 0;
  virtual bool RemoveEntityFromMap(int32_t map_id,
                                   entt::entity entity) = 0;
  virtual bool MoveEntityToMap(int32_t from_map_id,
                               int32_t to_map_id,
                               entt::entity entity,
                               int32_t x,
                               int32_t y) = 0;
};

}  // namespace mir2::game::ports

#endif  // MIR2_SERVER_GAME_PORTS_I_WORLD_MAP_PORT_H_
