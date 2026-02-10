/**
 * @file handler_error_utils.h
 * @brief Shared error conversion helpers for logic handlers.
 */

#ifndef MIR2_LOGIC_HANDLERS_HANDLER_ERROR_UTILS_H_
#define MIR2_LOGIC_HANDLERS_HANDLER_ERROR_UTILS_H_

#include "common/enums.h"
#include "common/protocol/message_codec.h"

namespace mir2::logic {

inline mir2::common::ErrorCode ToCommonError(mir2::common::MessageCodecStatus status) {
  return status == mir2::common::MessageCodecStatus::kOk
             ? mir2::common::ErrorCode::kOk
             : mir2::common::ErrorCode::kInvalidAction;
}

inline mir2::proto::ErrorCode ToProtoError(mir2::common::ErrorCode code) {
  return static_cast<mir2::proto::ErrorCode>(static_cast<uint16_t>(code));
}

}  // namespace mir2::logic

#endif  // MIR2_LOGIC_HANDLERS_HANDLER_ERROR_UTILS_H_
