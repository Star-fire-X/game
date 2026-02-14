/**
 * @file error_code_adapter.h
 * @brief Shared error code adaptation utilities for legacy protocol mapping.
 */

#ifndef MIR2_LOGIC_SERVICES_ERROR_CODE_ADAPTER_H_
#define MIR2_LOGIC_SERVICES_ERROR_CODE_ADAPTER_H_

#include "server/common/error_codes.h"

namespace mir2::logic {

inline mir2::common::ErrorCode ToLegacyError(mir2::common::ErrorCode code) {
  switch (code) {
    case mir2::common::ErrorCode::SUCCESS:
      return mir2::common::ErrorCode::kOk;
    case mir2::common::ErrorCode::INVALID_ACTION:
      return mir2::common::ErrorCode::kInvalidAction;
    case mir2::common::ErrorCode::TARGET_NOT_FOUND:
      return mir2::common::ErrorCode::kTargetNotFound;
    case mir2::common::ErrorCode::TARGET_OUT_OF_RANGE:
      return mir2::common::ErrorCode::kTargetOutOfRange;
    case mir2::common::ErrorCode::INSUFFICIENT_MP:
      return mir2::common::ErrorCode::kInsufficientMp;
    case mir2::common::ErrorCode::SKILL_ON_COOLDOWN:
      return mir2::common::ErrorCode::kSkillCooldown;
    case mir2::common::ErrorCode::CHARACTER_DEAD:
      return mir2::common::ErrorCode::kTargetDead;
    case mir2::common::ErrorCode::ITEM_NOT_FOUND:
      return mir2::common::ErrorCode::kTargetNotFound;
    case mir2::common::ErrorCode::INVENTORY_FULL:
      return mir2::common::ErrorCode::kInvalidAction;
    case mir2::common::ErrorCode::INVALID_POSITION:
      return mir2::common::ErrorCode::kInvalidAction;
    case mir2::common::ErrorCode::POSITION_NOT_WALKABLE:
      return mir2::common::ErrorCode::kInvalidAction;
    default:
      return mir2::common::ErrorCode::kUnknown;
  }
}

}  // namespace mir2::logic

#endif  // MIR2_LOGIC_SERVICES_ERROR_CODE_ADAPTER_H_
