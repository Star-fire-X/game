/**
 * @file handler_msg_id_matrix.h
 * @brief Canonical logic handler registration matrix and consistency guards.
 */

#ifndef MIR2_LOGIC_HANDLER_MSG_ID_MATRIX_H_
#define MIR2_LOGIC_HANDLER_MSG_ID_MATRIX_H_

#include <array>
#include <cstdint>

#include "common/enums.h"
#include "common/protocol/universal_forward_msg_ids.h"
#include "guild_generated.h"

namespace mir2::logic::matrix {

inline constexpr std::array<uint16_t, 1> kLoginHandlerMsgIds = {
    static_cast<uint16_t>(mir2::common::MsgId::kLoginReq)};
inline constexpr std::array<uint16_t, 1> kMovementHandlerMsgIds = {
    static_cast<uint16_t>(mir2::common::MsgId::kMoveReq)};
inline constexpr std::array<uint16_t, 1> kAttackHandlerMsgIds = {
    static_cast<uint16_t>(mir2::common::MsgId::kAttackReq)};
inline constexpr std::array<uint16_t, 1> kSkillHandlerMsgIds = {
    static_cast<uint16_t>(mir2::common::MsgId::kSkillReq)};
inline constexpr std::array<uint16_t, 4> kCharacterHandlerMsgIds = {
    static_cast<uint16_t>(mir2::common::MsgId::kRoleListReq),
    static_cast<uint16_t>(mir2::common::MsgId::kCreateRoleReq),
    static_cast<uint16_t>(mir2::common::MsgId::kSelectRoleReq),
    static_cast<uint16_t>(mir2::common::MsgId::kLogout)};
inline constexpr std::array<uint16_t, 1> kChatHandlerMsgIds = {
    static_cast<uint16_t>(mir2::common::MsgId::kChatReq)};
inline constexpr std::array<uint16_t, 5> kItemHandlerMsgIds = {
    static_cast<uint16_t>(mir2::common::MsgId::kPickupItemReq),
    static_cast<uint16_t>(mir2::common::MsgId::kUseItemReq),
    static_cast<uint16_t>(mir2::common::MsgId::kDropItemReq),
    static_cast<uint16_t>(mir2::common::MsgId::kEquipReq),
    static_cast<uint16_t>(mir2::common::MsgId::kUnequipReq)};
inline constexpr std::array<uint16_t, 20> kGuildHandlerMsgIds = {
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
    static_cast<uint16_t>(mir2::proto::GuildMessageType::UPDATE_RANK)};
inline constexpr std::array<uint16_t, 5> kTradeHandlerMsgIds = {
    static_cast<uint16_t>(mir2::common::MsgId::kTradeReq),
    static_cast<uint16_t>(mir2::common::MsgId::kTradeAddItemReq),
    static_cast<uint16_t>(mir2::common::MsgId::kTradeSetGoldReq),
    static_cast<uint16_t>(mir2::common::MsgId::kTradeConfirmReq),
    static_cast<uint16_t>(mir2::common::MsgId::kTradeCancelReq)};
inline constexpr std::array<uint16_t, 4> kPartyHandlerMsgIds = {
    static_cast<uint16_t>(mir2::common::MsgId::kPartyInviteReq),
    static_cast<uint16_t>(mir2::common::MsgId::kPartyJoinReq),
    static_cast<uint16_t>(mir2::common::MsgId::kPartyLeaveReq),
    static_cast<uint16_t>(mir2::common::MsgId::kPartyKickReq)};
inline constexpr std::array<uint16_t, 2> kRankingHandlerMsgIds = {
    static_cast<uint16_t>(mir2::common::MsgId::kRankingReq),
    static_cast<uint16_t>(mir2::common::MsgId::kRankingMyRankReq)};
inline constexpr std::array<uint16_t, 5> kMailHandlerMsgIds = {
    static_cast<uint16_t>(mir2::common::MsgId::kMailSendReq),
    static_cast<uint16_t>(mir2::common::MsgId::kMailListReq),
    static_cast<uint16_t>(mir2::common::MsgId::kMailReadReq),
    static_cast<uint16_t>(mir2::common::MsgId::kMailDeleteReq),
    static_cast<uint16_t>(mir2::common::MsgId::kMailClaimReq)};
inline constexpr std::array<uint16_t, 2> kAchievementHandlerMsgIds = {
    static_cast<uint16_t>(mir2::common::MsgId::kAchievementListReq),
    static_cast<uint16_t>(mir2::common::MsgId::kAchievementClaimReq)};
inline constexpr std::array<uint16_t, 4> kAuctionHandlerMsgIds = {
    static_cast<uint16_t>(mir2::common::MsgId::kAuctionListReq),
    static_cast<uint16_t>(mir2::common::MsgId::kAuctionSellReq),
    static_cast<uint16_t>(mir2::common::MsgId::kAuctionBuyReq),
    static_cast<uint16_t>(mir2::common::MsgId::kAuctionCancelReq)};
inline constexpr std::array<uint16_t, 2> kNpcHandlerMsgIds = {
    static_cast<uint16_t>(mir2::common::MsgId::kNpcInteractReq),
    static_cast<uint16_t>(mir2::common::MsgId::kNpcMenuSelect)};

struct PlaceholderBinding {
  uint16_t msg_id;
  const char* name;
};

inline constexpr std::array<PlaceholderBinding, 1> kPlaceholderBindings = {{
    {static_cast<uint16_t>(mir2::common::MsgId::kGuildChat), "guild_chat"},
}};

template <size_t N>
constexpr bool ContainsMsgId(const std::array<uint16_t, N>& msg_ids,
                             uint16_t msg_id) {
  for (const auto candidate : msg_ids) {
    if (candidate == msg_id) {
      return true;
    }
  }
  return false;
}

template <size_t N>
constexpr bool ContainsPlaceholderMsgId(
    const std::array<PlaceholderBinding, N>& bindings,
    uint16_t msg_id) {
  for (const auto& binding : bindings) {
    if (binding.msg_id == msg_id) {
      return true;
    }
  }
  return false;
}

constexpr bool IsLogicRegistryMsgId(uint16_t msg_id) {
  return ContainsMsgId(kLoginHandlerMsgIds, msg_id) ||
         ContainsMsgId(kMovementHandlerMsgIds, msg_id) ||
         ContainsMsgId(kAttackHandlerMsgIds, msg_id) ||
         ContainsMsgId(kSkillHandlerMsgIds, msg_id) ||
         ContainsMsgId(kCharacterHandlerMsgIds, msg_id) ||
         ContainsMsgId(kChatHandlerMsgIds, msg_id) ||
         ContainsMsgId(kItemHandlerMsgIds, msg_id) ||
         ContainsMsgId(kGuildHandlerMsgIds, msg_id) ||
         ContainsMsgId(kTradeHandlerMsgIds, msg_id) ||
         ContainsMsgId(kPartyHandlerMsgIds, msg_id) ||
         ContainsMsgId(kRankingHandlerMsgIds, msg_id) ||
         ContainsMsgId(kMailHandlerMsgIds, msg_id) ||
         ContainsMsgId(kAchievementHandlerMsgIds, msg_id) ||
         ContainsMsgId(kAuctionHandlerMsgIds, msg_id) ||
         ContainsMsgId(kNpcHandlerMsgIds, msg_id) ||
         ContainsPlaceholderMsgId(kPlaceholderBindings, msg_id);
}

template <size_t N>
constexpr bool IsSubsetOfUniversal(const std::array<uint16_t, N>& msg_ids) {
  for (const auto msg_id : msg_ids) {
    if (!mir2::common::protocol::IsUniversalForwardMsgId(msg_id)) {
      return false;
    }
  }
  return true;
}

template <size_t N>
constexpr bool IsPlaceholderSubsetOfUniversal(
    const std::array<PlaceholderBinding, N>& bindings) {
  for (const auto& binding : bindings) {
    if (!mir2::common::protocol::IsUniversalForwardMsgId(binding.msg_id)) {
      return false;
    }
  }
  return true;
}

constexpr bool IsUniversalForwardCoveredByLogicRegistry() {
  for (const auto msg_id : mir2::common::protocol::kUniversalForwardMsgIds) {
    if (!IsLogicRegistryMsgId(msg_id)) {
      return false;
    }
  }
  return true;
}

constexpr bool IsLogicRegistrySubsetOfUniversalForward() {
  return IsSubsetOfUniversal(kLoginHandlerMsgIds) &&
         IsSubsetOfUniversal(kMovementHandlerMsgIds) &&
         IsSubsetOfUniversal(kAttackHandlerMsgIds) &&
         IsSubsetOfUniversal(kSkillHandlerMsgIds) &&
         IsSubsetOfUniversal(kCharacterHandlerMsgIds) &&
         IsSubsetOfUniversal(kChatHandlerMsgIds) &&
         IsSubsetOfUniversal(kItemHandlerMsgIds) &&
         IsSubsetOfUniversal(kGuildHandlerMsgIds) &&
         IsSubsetOfUniversal(kTradeHandlerMsgIds) &&
         IsSubsetOfUniversal(kPartyHandlerMsgIds) &&
         IsSubsetOfUniversal(kRankingHandlerMsgIds) &&
         IsSubsetOfUniversal(kMailHandlerMsgIds) &&
         IsSubsetOfUniversal(kAchievementHandlerMsgIds) &&
         IsSubsetOfUniversal(kAuctionHandlerMsgIds) &&
         IsSubsetOfUniversal(kNpcHandlerMsgIds) &&
         IsPlaceholderSubsetOfUniversal(kPlaceholderBindings);
}

static_assert(IsUniversalForwardCoveredByLogicRegistry(),
              "Universal forward matrix has msg_id without logic registry coverage.");
static_assert(IsLogicRegistrySubsetOfUniversalForward(),
              "Logic registry matrix has msg_id not present in universal forward matrix.");

}  // namespace mir2::logic::matrix

#endif  // MIR2_LOGIC_HANDLER_MSG_ID_MATRIX_H_
