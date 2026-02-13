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
  switch (status) {
    case mir2::common::MessageCodecStatus::kOk:
      return mir2::common::ErrorCode::kOk;
    case mir2::common::MessageCodecStatus::kInvalidMsgId:
      return mir2::common::ErrorCode::kDecodeInvalidMsgId;
    case mir2::common::MessageCodecStatus::kInvalidPayload:
      return mir2::common::ErrorCode::kDecodeInvalidPayload;
    case mir2::common::MessageCodecStatus::kMissingField:
      return mir2::common::ErrorCode::kDecodeMissingField;
    case mir2::common::MessageCodecStatus::kStringTooLong:
      return mir2::common::ErrorCode::kDecodeStringTooLong;
    case mir2::common::MessageCodecStatus::kValueOutOfRange:
      return mir2::common::ErrorCode::kDecodeValueOutOfRange;
  }
  return mir2::common::ErrorCode::kUnknown;
}

inline mir2::proto::ErrorCode ToProtoError(mir2::common::ErrorCode code) {
  return static_cast<mir2::proto::ErrorCode>(static_cast<uint16_t>(code));
}

}  // namespace mir2::logic

#endif  // MIR2_LOGIC_HANDLERS_HANDLER_ERROR_UTILS_H_
