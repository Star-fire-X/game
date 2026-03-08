/**
 * @file universal_forward_msg_ids.h
 * @brief Canonical universal-forward request msg-id matrix.
 */

#ifndef MIR2_COMMON_PROTOCOL_UNIVERSAL_FORWARD_MSG_IDS_H_
#define MIR2_COMMON_PROTOCOL_UNIVERSAL_FORWARD_MSG_IDS_H_

#include <array>
#include <cstdint>

#include "common/enums.h"
#include "guild_generated.h"

namespace mir2::common::protocol {

inline constexpr std::array<uint16_t, 59> kUniversalForwardMsgIds = {
    static_cast<uint16_t>(mir2::common::MsgId::kLoginReq),
    static_cast<uint16_t>(mir2::common::MsgId::kLogout),
    static_cast<uint16_t>(mir2::common::MsgId::kCreateRoleReq),
    static_cast<uint16_t>(mir2::common::MsgId::kSelectRoleReq),
    static_cast<uint16_t>(mir2::common::MsgId::kRoleListReq),
    static_cast<uint16_t>(mir2::common::MsgId::kMoveReq),
    static_cast<uint16_t>(mir2::common::MsgId::kAttackReq),
    static_cast<uint16_t>(mir2::common::MsgId::kSkillReq),
    static_cast<uint16_t>(mir2::common::MsgId::kChatReq),
    static_cast<uint16_t>(mir2::common::MsgId::kUseItemReq),
    static_cast<uint16_t>(mir2::common::MsgId::kDropItemReq),
    static_cast<uint16_t>(mir2::common::MsgId::kPickupItemReq),
    static_cast<uint16_t>(mir2::common::MsgId::kEquipReq),
    static_cast<uint16_t>(mir2::common::MsgId::kUnequipReq),
    static_cast<uint16_t>(mir2::common::MsgId::kNpcInteractReq),
    static_cast<uint16_t>(mir2::common::MsgId::kNpcMenuSelect),
    static_cast<uint16_t>(mir2::common::MsgId::kGuildChat),
    static_cast<uint16_t>(mir2::common::MsgId::kGuildCreateReq),
    static_cast<uint16_t>(mir2::common::MsgId::kGuildJoinReq),
    static_cast<uint16_t>(mir2::common::MsgId::kGuildLeaveReq),
    static_cast<uint16_t>(mir2::common::MsgId::kGuildKickReq),
    static_cast<uint16_t>(mir2::common::MsgId::kGuildDeclareWarReq),
    static_cast<uint16_t>(mir2::common::MsgId::kGuildCancelWarReq),
    static_cast<uint16_t>(mir2::common::MsgId::kGuildMakeAllyReq),
    static_cast<uint16_t>(mir2::common::MsgId::kGuildBreakAllyReq),
    static_cast<uint16_t>(mir2::common::MsgId::kGuildUpdateNoticeReq),
    static_cast<uint16_t>(mir2::common::MsgId::kGuildUpdateRankReq),
    static_cast<uint16_t>(mir2::proto::GuildMessageType::CREATE),
    static_cast<uint16_t>(mir2::proto::GuildMessageType::JOIN),
    static_cast<uint16_t>(mir2::proto::GuildMessageType::LEAVE),
    static_cast<uint16_t>(mir2::proto::GuildMessageType::KICK),
    static_cast<uint16_t>(mir2::proto::GuildMessageType::DECLARE_WAR),
    static_cast<uint16_t>(mir2::proto::GuildMessageType::CANCEL_WAR),
    static_cast<uint16_t>(mir2::proto::GuildMessageType::MAKE_ALLY),
    static_cast<uint16_t>(mir2::proto::GuildMessageType::BREAK_ALLY),
    static_cast<uint16_t>(mir2::proto::GuildMessageType::UPDATE_NOTICE),
    static_cast<uint16_t>(mir2::proto::GuildMessageType::UPDATE_RANK),
    static_cast<uint16_t>(mir2::common::MsgId::kTradeReq),
    static_cast<uint16_t>(mir2::common::MsgId::kTradeAddItemReq),
    static_cast<uint16_t>(mir2::common::MsgId::kTradeSetGoldReq),
    static_cast<uint16_t>(mir2::common::MsgId::kTradeConfirmReq),
    static_cast<uint16_t>(mir2::common::MsgId::kTradeCancelReq),
    static_cast<uint16_t>(mir2::common::MsgId::kPartyInviteReq),
    static_cast<uint16_t>(mir2::common::MsgId::kPartyJoinReq),
    static_cast<uint16_t>(mir2::common::MsgId::kPartyLeaveReq),
    static_cast<uint16_t>(mir2::common::MsgId::kPartyKickReq),
    static_cast<uint16_t>(mir2::common::MsgId::kRankingReq),
    static_cast<uint16_t>(mir2::common::MsgId::kRankingMyRankReq),
    static_cast<uint16_t>(mir2::common::MsgId::kMailSendReq),
    static_cast<uint16_t>(mir2::common::MsgId::kMailListReq),
    static_cast<uint16_t>(mir2::common::MsgId::kMailReadReq),
    static_cast<uint16_t>(mir2::common::MsgId::kMailDeleteReq),
    static_cast<uint16_t>(mir2::common::MsgId::kMailClaimReq),
    static_cast<uint16_t>(mir2::common::MsgId::kAchievementListReq),
    static_cast<uint16_t>(mir2::common::MsgId::kAchievementClaimReq),
    static_cast<uint16_t>(mir2::common::MsgId::kAuctionListReq),
    static_cast<uint16_t>(mir2::common::MsgId::kAuctionSellReq),
    static_cast<uint16_t>(mir2::common::MsgId::kAuctionBuyReq),
    static_cast<uint16_t>(mir2::common::MsgId::kAuctionCancelReq),
};

inline constexpr bool IsUniversalForwardMsgId(uint16_t msg_id) {
  for (const auto candidate : kUniversalForwardMsgIds) {
    if (candidate == msg_id) {
      return true;
    }
  }
  return false;
}

}  // namespace mir2::common::protocol

#endif  // MIR2_COMMON_PROTOCOL_UNIVERSAL_FORWARD_MSG_IDS_H_
