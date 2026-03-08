// =============================================================================
// Legend2 游戏常量定义 (Constants)
//
// 功能说明:
//   - 游戏相关常量
//   - 网络相关常量
//   - 地图相关常量
// =============================================================================

#ifndef LEGEND2_COMMON_TYPES_CONSTANTS_H
#define LEGEND2_COMMON_TYPES_CONSTANTS_H

#include <cstddef>
#include <cstdint>
#include "common/enums.h"

namespace mir2::common {
namespace constants {

// 背包相关
constexpr int MAX_INVENTORY_SIZE = 40;
constexpr int MAX_EQUIPMENT_SLOTS = static_cast<int>(EquipSlot::MAX_SLOTS);

// 地图相关
constexpr int TILE_WIDTH = 48;
constexpr int TILE_HEIGHT = 32;

// 战斗相关
constexpr int BASE_ATTACK_RANGE = 1;
constexpr int MAX_ATTACK_RANGE = 10;

// 技能ID常量
constexpr uint32_t SKILL_ID_ERGUM = 12;      // 野蛮冲撞
constexpr uint32_t SKILL_ID_HOLYWORD = 16;   // 圣言术
constexpr uint32_t SKILL_ID_TELEPORT = 21;   // 瞬息移动
constexpr uint32_t SKILL_ID_TRAPSPELL = 46;  // 困魔咒
constexpr uint32_t SKILL_ID_FIREWALL = 9;    // 火墙
constexpr uint32_t SKILL_ID_CHARM = 50;      // 怪物诱惑

// 网络相关
constexpr int HEARTBEAT_INTERVAL_MS = 5000;
constexpr int CONNECTION_TIMEOUT_MS = 15000;
constexpr uint16_t DEFAULT_SERVER_PORT = 7000;

// 游戏相关
constexpr int TARGET_FPS = 60;
constexpr int SERVER_TICK_RATE = 20;

// 登录校验相关
constexpr size_t LOGIN_USERNAME_MIN_LENGTH = 3;
constexpr size_t LOGIN_USERNAME_MAX_LENGTH = 32;
constexpr size_t LOGIN_PASSWORD_MIN_LENGTH = 6;
constexpr size_t LOGIN_PASSWORD_MAX_LENGTH = 64;

} // namespace constants
} // namespace mir2::common

#endif // LEGEND2_COMMON_TYPES_CONSTANTS_H
