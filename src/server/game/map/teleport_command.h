/**
 * @file teleport_command.h
 * @brief 传送命令定义
 */

#ifndef MIR2_GAME_MAP_TELEPORT_COMMAND_H_
#define MIR2_GAME_MAP_TELEPORT_COMMAND_H_

#include <entt/entt.hpp>
#include <cstdint>

namespace mir2::game::map {

/**
 * @brief 传送结果
 */
enum class TeleportResult : uint8_t {
  kSuccess = 0,           // 传送成功
  kInvalidEntity,         // 无效实体
  kInvalidMapId,          // 目标地图不存在
  kInvalidPosition,       // 目标坐标非法
  kSameMap,               // 同地图传送（应使用移动）
  kEntityNotInMap,        // 实体不在任何地图中
  kFailed,                // 其他失败原因
};

/**
 * @brief 传送命令
 */
struct TeleportCommand {
  entt::entity entity = entt::null;  // 要传送的实体
  int32_t target_map_id = 0;         // 目标地图ID
  int32_t target_x = 0;              // 目标X坐标
  int32_t target_y = 0;              // 目标Y坐标

  TeleportCommand() = default;

  TeleportCommand(entt::entity e, int32_t map_id, int32_t x, int32_t y)
      : entity(e), target_map_id(map_id), target_x(x), target_y(y) {}
};

}  // namespace mir2::game::map

#endif  // MIR2_GAME_MAP_TELEPORT_COMMAND_H_
