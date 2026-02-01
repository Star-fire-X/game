/**
 * @file handler_utils.h
 * @brief Handler工具函数
 */

#ifndef LEGEND2_SERVER_HANDLERS_HANDLER_UTILS_H
#define LEGEND2_SERVER_HANDLERS_HANDLER_UTILS_H

#include "server/common/error_codes.h"
#include "common/protocol/message_codec.h"
#include "common/types/error_codes.h"
#include "common_generated.h"

namespace legend2::handlers {

inline mir2::proto::ErrorCode ToProtoError(mir2::common::ErrorCode code) {
    return static_cast<mir2::proto::ErrorCode>(static_cast<uint16_t>(code));
}

inline mir2::common::ErrorCode ToCommonError(mir2::common::ErrorCode code) {
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
        case mir2::common::ErrorCode::ACCOUNT_NOT_FOUND:
            return mir2::common::ErrorCode::kAccountNotFound;
        case mir2::common::ErrorCode::INVALID_CREDENTIALS:
            return mir2::common::ErrorCode::kPasswordWrong;
        case mir2::common::ErrorCode::DUPLICATE_CHARACTER_NAME:
            return mir2::common::ErrorCode::kNameExists;
        default:
            return mir2::common::ErrorCode::kUnknown;
    }
}

inline mir2::common::ErrorCode ToCommonError(mir2::common::MessageCodecStatus status) {
    return status == mir2::common::MessageCodecStatus::kOk
               ? mir2::common::ErrorCode::kOk
               : mir2::common::ErrorCode::kInvalidAction;
}

}  // namespace legend2::handlers

#endif  // LEGEND2_SERVER_HANDLERS_HANDLER_UTILS_H
