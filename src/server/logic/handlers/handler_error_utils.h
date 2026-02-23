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
  using CommonError = mir2::common::ErrorCode;
  using ProtoError = mir2::proto::ErrorCode;

  switch (code) {
    case CommonError::kOk:
      return ProtoError::ERR_OK;
    case CommonError::kUnknown:
      return ProtoError::ERR_UNKNOWN;
    case CommonError::kAccountNotFound:
    case CommonError::ACCOUNT_NOT_FOUND:
      return ProtoError::ERR_ACCOUNT_NOT_FOUND;
    case CommonError::kPasswordWrong:
      return ProtoError::ERR_PASSWORD_WRONG;
    case CommonError::kNameExists:
    case CommonError::DUPLICATE_CHARACTER_NAME:
      return ProtoError::ERR_NAME_EXISTS;
    case CommonError::kTargetDead:
      return ProtoError::ERR_TARGET_DEAD;
    case CommonError::kSkillCooldown:
    case CommonError::SKILL_ON_COOLDOWN:
      return ProtoError::ERR_SKILL_COOLDOWN;
    case CommonError::kInvalidAction:
    case CommonError::INVALID_ACTION:
    case CommonError::INVALID_CHARACTER_NAME:
    case CommonError::INVALID_CHARACTER_CLASS:
    case CommonError::INVALID_CREDENTIALS:
      return ProtoError::ERR_INVALID_ACTION;
    case CommonError::kTargetNotFound:
    case CommonError::TARGET_NOT_FOUND:
      return ProtoError::ERR_TARGET_NOT_FOUND;
    case CommonError::kTargetOutOfRange:
    case CommonError::TARGET_OUT_OF_RANGE:
      return ProtoError::ERR_TARGET_OUT_OF_RANGE;
    case CommonError::kDecodeInvalidMsgId:
      return ProtoError::ERR_DECODE_INVALID_MSG_ID;
    case CommonError::kDecodeInvalidPayload:
      return ProtoError::ERR_DECODE_INVALID_PAYLOAD;
    case CommonError::kDecodeMissingField:
      return ProtoError::ERR_DECODE_MISSING_FIELD;
    case CommonError::kDecodeStringTooLong:
      return ProtoError::ERR_DECODE_STRING_TOO_LONG;
    case CommonError::kDecodeValueOutOfRange:
      return ProtoError::ERR_DECODE_VALUE_OUT_OF_RANGE;
    case CommonError::kNoParty:
      return ProtoError::ERR_NO_PARTY;
    case CommonError::kTargetRefused:
      return ProtoError::ERR_TARGET_REFUSED;
    case CommonError::kInsufficientMp:
    case CommonError::INSUFFICIENT_MP:
      return ProtoError::ERR_INSUFFICIENT_MP;
    case CommonError::kTradeInvalidState:
      return ProtoError::ERR_TRADE_INVALID_STATE;
    case CommonError::kTradeTargetBusy:
      return ProtoError::ERR_TRADE_TARGET_BUSY;
    case CommonError::kTradeTimeout:
      return ProtoError::ERR_TRADE_TIMEOUT;
    case CommonError::kPartyInviteInvalid:
      return ProtoError::ERR_PARTY_INVITE_INVALID;
    case CommonError::kPartyFull:
      return ProtoError::ERR_PARTY_FULL;
    case CommonError::kPartyNotFound:
      return ProtoError::ERR_PARTY_NOT_FOUND;
    case CommonError::kRankingTypeInvalid:
      return ProtoError::ERR_RANKING_TYPE_INVALID;
    case CommonError::kMailNotFound:
      return ProtoError::ERR_MAIL_NOT_FOUND;
    case CommonError::kMailAlreadyClaimed:
      return ProtoError::ERR_MAIL_ALREADY_CLAIMED;
    case CommonError::kMailAttachmentInvalid:
      return ProtoError::ERR_MAIL_ATTACHMENT_INVALID;
    case CommonError::kAchievementNotFound:
      return ProtoError::ERR_ACHIEVEMENT_NOT_FOUND;
    case CommonError::kAchievementAlreadyClaimed:
      return ProtoError::ERR_ACHIEVEMENT_ALREADY_CLAIMED;
    case CommonError::kAchievementNotCompleted:
      return ProtoError::ERR_ACHIEVEMENT_NOT_COMPLETED;
    case CommonError::kAuctionNotFound:
      return ProtoError::ERR_AUCTION_NOT_FOUND;
    case CommonError::kAuctionBidTooLow:
      return ProtoError::ERR_AUCTION_BID_TOO_LOW;
    case CommonError::kAuctionAlreadySold:
      return ProtoError::ERR_AUCTION_ALREADY_SOLD;
    case CommonError::kKickHeartbeatTimeout:
      return ProtoError::ERR_KICK_HEARTBEAT_TIMEOUT;
    case CommonError::kKickDuplicateLogin:
      return ProtoError::ERR_KICK_DUPLICATE_LOGIN;
    case CommonError::kKickAdminManual:
      return ProtoError::ERR_KICK_ADMIN_MANUAL;
    default:
      return static_cast<mir2::proto::ErrorCode>(static_cast<uint16_t>(code));
  }
}

}  // namespace mir2::logic

#endif  // MIR2_LOGIC_HANDLERS_HANDLER_ERROR_UTILS_H_
