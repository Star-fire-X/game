/**
 * @file scroll_teleport.h
 * @brief 回城卷与地牢卷传送逻辑
 */

#ifndef MIR2_GAME_MAP_SCROLL_TELEPORT_H_
#define MIR2_GAME_MAP_SCROLL_TELEPORT_H_

#include <entt/entt.hpp>

#include <cstdint>
#include <functional>
#include <optional>
#include <string>

#include "game/map/map_attributes.h"
#include "game/map/teleport_command.h"

namespace mir2::game::map {

class ScrollTeleport {
 public:
  using WalkableChecker = std::function<bool(int32_t, int32_t)>;

  // 回城卷
  static std::optional<TeleportCommand> UseTownScroll(
      entt::entity entity,
      const std::string& current_map,
      int32_t pk_level,
      const MapAttributes& attrs);

  // 地牢卷
  static std::optional<TeleportCommand> UseDungeonScroll(
      entt::entity entity,
      const std::string& current_map,
      uint64_t last_use_time,
      bool in_siege,
      const MapAttributes& attrs,
      int32_t map_width = 0,
      int32_t map_height = 0,
      WalkableChecker walkable_checker = {});
};

}  // namespace mir2::game::map

#endif  // MIR2_GAME_MAP_SCROLL_TELEPORT_H_
