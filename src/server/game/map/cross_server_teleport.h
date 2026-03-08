/**
 * @file cross_server_teleport.h
 * @brief 跨服传送处理
 */

#ifndef MIR2_GAME_MAP_CROSS_SERVER_TELEPORT_H_
#define MIR2_GAME_MAP_CROSS_SERVER_TELEPORT_H_

#include <entt/entt.hpp>

#include <cstdint>
#include <string>

namespace mir2::game::map {

class CrossServerTeleport {
 public:
  // 发起跨服传送请求 (实际网络通信由Gateway处理)
  static bool RequestCrossServerTeleport(
      entt::entity entity,
      const std::string& target_server,
      const std::string& target_map,
      int32_t target_x,
      int32_t target_y);

  // 处理跨服传送回应
  static void HandleCrossServerResponse(
      entt::entity entity,
      bool success,
      const std::string& error_msg);
};

}  // namespace mir2::game::map

#endif  // MIR2_GAME_MAP_CROSS_SERVER_TELEPORT_H_
