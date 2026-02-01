/**
 * @file error_codes.h
 * @brief 错误码定义
 */

#ifndef MIR2_COMMON_ERROR_CODES_H
#define MIR2_COMMON_ERROR_CODES_H

#include "common/types/error_codes.h"

namespace mir2::common {

/**
 * @brief 错误码转字符串
 */
inline const char* ToString(ErrorCode code) {
  switch (code) {
    case ErrorCode::kOk:
      return "OK";
    case ErrorCode::kUnknown:
      return "Unknown";
    case ErrorCode::kAccountNotFound:
      return "Account not found";
    case ErrorCode::kPasswordWrong:
      return "Password wrong";
    case ErrorCode::kNameExists:
      return "Name exists";
    case ErrorCode::kTargetDead:
      return "Target dead";
    case ErrorCode::kSkillCooldown:
      return "Skill cooldown";
    case ErrorCode::kInvalidAction:
      return "Invalid action";
    case ErrorCode::kTargetNotFound:
      return "Target not found";
    case ErrorCode::kTargetOutOfRange:
      return "Target out of range";
    case ErrorCode::kInvalidPath:
      return "Invalid path";
    case ErrorCode::kSpeedViolation:
      return "Speed violation";
    case ErrorCode::kPathBlocked:
      return "Path blocked";
    case ErrorCode::kInsufficientMp:
      return "Insufficient MP";
    case ErrorCode::kKickHeartbeatTimeout:
      return "Heartbeat timeout";
    case ErrorCode::kKickDuplicateLogin:
      return "Duplicate login";
    case ErrorCode::kKickAdminManual:
      return "Kicked by admin";
    default:
      return "Unknown";
  }
}

}  // namespace mir2::common

#endif  // MIR2_COMMON_ERROR_CODES_H
