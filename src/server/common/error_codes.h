/**
 * @file error_codes.h
 * @brief 错误码定义
 */

#ifndef MIR2_COMMON_ERROR_CODES_H_
#define MIR2_COMMON_ERROR_CODES_H_

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
    case ErrorCode::kNoParty:
      return "No party";
    case ErrorCode::kTargetRefused:
      return "Target refused";
    case ErrorCode::kInvalidPath:
      return "Invalid path";
    case ErrorCode::kSpeedViolation:
      return "Speed violation";
    case ErrorCode::kPathBlocked:
      return "Path blocked";
    case ErrorCode::kInsufficientMp:
      return "Insufficient MP";
    case ErrorCode::kTradeInvalidState:
      return "Trade invalid state";
    case ErrorCode::kTradeTargetBusy:
      return "Trade target busy";
    case ErrorCode::kTradeTimeout:
      return "Trade timeout";
    case ErrorCode::kPartyInviteInvalid:
      return "Party invite invalid";
    case ErrorCode::kPartyFull:
      return "Party full";
    case ErrorCode::kPartyNotFound:
      return "Party not found";
    case ErrorCode::kRankingTypeInvalid:
      return "Ranking type invalid";
    case ErrorCode::kMailNotFound:
      return "Mail not found";
    case ErrorCode::kMailAlreadyClaimed:
      return "Mail already claimed";
    case ErrorCode::kMailAttachmentInvalid:
      return "Mail attachment invalid";
    case ErrorCode::kAchievementNotFound:
      return "Achievement not found";
    case ErrorCode::kAchievementAlreadyClaimed:
      return "Achievement already claimed";
    case ErrorCode::kAchievementNotCompleted:
      return "Achievement not completed";
    case ErrorCode::kAuctionNotFound:
      return "Auction not found";
    case ErrorCode::kAuctionBidTooLow:
      return "Auction bid too low";
    case ErrorCode::kAuctionAlreadySold:
      return "Auction already sold";
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

#endif  // MIR2_COMMON_ERROR_CODES_H_
