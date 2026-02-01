// =============================================================================
// Legend2 错误码定义 (Error Codes)
//
// 功能说明:
//   - 游戏操作错误码
//   - 错误码到字符串转换
// =============================================================================

#ifndef LEGEND2_COMMON_TYPES_ERROR_CODES_H
#define LEGEND2_COMMON_TYPES_ERROR_CODES_H

#include <cstdint>

namespace mir2::common {

/// 游戏操作错误码
enum class ErrorCode : uint16_t {
    SUCCESS = 0,

    // 兼容旧协议错误码 (k*)
    kOk = SUCCESS,
    kUnknown = 1,
    kAccountNotFound = 100,
    kPasswordWrong = 101,
    kNameExists = 111,
    kTargetDead = 300,
    kSkillCooldown = 302,
    kInvalidAction = 400,
    kTargetNotFound = 401,
    kTargetOutOfRange = 402,
    kInvalidPath = 403,
    kSpeedViolation = 404,
    kPathBlocked = 405,
    kInsufficientMp = 500,
    kKickHeartbeatTimeout = 9001,
    kKickDuplicateLogin = 9002,
    kKickAdminManual = 9003,

    // 连接错误 (1xxx)
    CONNECTION_FAILED = 1001,
    CONNECTION_TIMEOUT = 1002,
    CONNECTION_LOST = 1003,
    INVALID_PACKET = 1004,

    // 认证错误 (2xxx)
    INVALID_CREDENTIALS = 2001,
    ACCOUNT_NOT_FOUND = 2002,
    ACCOUNT_ALREADY_EXISTS = 2003,
    CHARACTER_NOT_FOUND = 2004,
    DUPLICATE_CHARACTER_NAME = 2005,
    INVALID_CHARACTER_NAME = 2006,
    INVALID_CHARACTER_CLASS = 2007,
    MAX_CHARACTERS_REACHED = 2008,

    // 游戏逻辑错误 (3xxx)
    INVALID_POSITION = 3001,
    POSITION_NOT_WALKABLE = 3002,
    TARGET_OUT_OF_RANGE = 3003,
    TARGET_NOT_FOUND = 3004,
    INSUFFICIENT_MP = 3005,
    SKILL_ON_COOLDOWN = 3006,
    SKILL_NOT_LEARNED = 3007,
    INVENTORY_FULL = 3008,
    ITEM_NOT_FOUND = 3009,
    INVALID_EQUIPMENT_SLOT = 3010,
    LEVEL_REQUIREMENT_NOT_MET = 3011,
    CLASS_REQUIREMENT_NOT_MET = 3012,
    CHARACTER_DEAD = 3013,
    INVALID_ACTION = 3014,

    // 服务器错误 (5xxx)
    INTERNAL_ERROR = 5001,
    DATABASE_ERROR = 5002,
    SERVER_OVERLOADED = 5003,
    SERVER_MAINTENANCE = 5004,
    NOT_IMPLEMENTED = 5005
};

/// 将错误码转换为字符串描述
inline const char* error_code_to_string(ErrorCode code) {
    switch (code) {
        case ErrorCode::SUCCESS:  // kOk is alias of SUCCESS
            return "Success";
        case ErrorCode::kUnknown: return "Unknown";
        case ErrorCode::kAccountNotFound: return "Account not found";
        case ErrorCode::kPasswordWrong: return "Password wrong";
        case ErrorCode::kNameExists: return "Name exists";
        case ErrorCode::kTargetDead: return "Target dead";
        case ErrorCode::kSkillCooldown: return "Skill cooldown";
        case ErrorCode::kInvalidAction: return "Invalid action";
        case ErrorCode::kTargetNotFound: return "Target not found";
        case ErrorCode::kTargetOutOfRange: return "Target out of range";
        case ErrorCode::kInvalidPath: return "Invalid path";
        case ErrorCode::kSpeedViolation: return "Speed violation";
        case ErrorCode::kPathBlocked: return "Path blocked";
        case ErrorCode::kInsufficientMp: return "Insufficient MP";
        case ErrorCode::kKickHeartbeatTimeout: return "Heartbeat timeout";
        case ErrorCode::kKickDuplicateLogin: return "Duplicate login";
        case ErrorCode::kKickAdminManual: return "Kicked by admin";
        case ErrorCode::CONNECTION_FAILED: return "Connection failed";
        case ErrorCode::CONNECTION_TIMEOUT: return "Connection timeout";
        case ErrorCode::CONNECTION_LOST: return "Connection lost";
        case ErrorCode::INVALID_PACKET: return "Invalid packet";
        case ErrorCode::INVALID_CREDENTIALS: return "Invalid credentials";
        case ErrorCode::ACCOUNT_NOT_FOUND: return "Account not found";
        case ErrorCode::ACCOUNT_ALREADY_EXISTS: return "Account already exists";
        case ErrorCode::CHARACTER_NOT_FOUND: return "Character not found";
        case ErrorCode::DUPLICATE_CHARACTER_NAME: return "Character name already taken";
        case ErrorCode::INVALID_CHARACTER_NAME: return "Invalid character name";
        case ErrorCode::INVALID_CHARACTER_CLASS: return "Invalid character class";
        case ErrorCode::MAX_CHARACTERS_REACHED: return "Maximum characters reached";
        case ErrorCode::INVALID_POSITION: return "Invalid position";
        case ErrorCode::POSITION_NOT_WALKABLE: return "Position not walkable";
        case ErrorCode::TARGET_OUT_OF_RANGE: return "Target out of range";
        case ErrorCode::TARGET_NOT_FOUND: return "Target not found";
        case ErrorCode::INSUFFICIENT_MP: return "Insufficient MP";
        case ErrorCode::SKILL_ON_COOLDOWN: return "Skill on cooldown";
        case ErrorCode::SKILL_NOT_LEARNED: return "Skill not learned";
        case ErrorCode::INVENTORY_FULL: return "Inventory full";
        case ErrorCode::ITEM_NOT_FOUND: return "Item not found";
        case ErrorCode::INVALID_EQUIPMENT_SLOT: return "Invalid equipment slot";
        case ErrorCode::LEVEL_REQUIREMENT_NOT_MET: return "Level requirement not met";
        case ErrorCode::CLASS_REQUIREMENT_NOT_MET: return "Class requirement not met";
        case ErrorCode::CHARACTER_DEAD: return "Character is dead";
        case ErrorCode::INVALID_ACTION: return "Invalid action";
        case ErrorCode::INTERNAL_ERROR: return "Internal server error";
        case ErrorCode::DATABASE_ERROR: return "Database error";
        case ErrorCode::SERVER_OVERLOADED: return "Server overloaded";
        case ErrorCode::SERVER_MAINTENANCE: return "Server under maintenance";
        case ErrorCode::NOT_IMPLEMENTED: return "Not implemented";
        default: return "Unknown error";
    }
}

} // namespace mir2::common

#endif // LEGEND2_COMMON_TYPES_ERROR_CODES_H
