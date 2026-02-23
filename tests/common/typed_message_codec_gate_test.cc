#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <sstream>
#include <string>
#include <unordered_set>
#include <vector>

#include "common/protocol/message_codec.h"
#include "common/protocol/typed_message_bindings.h"

namespace {

template <size_t N>
bool ContainsMsgId(const std::array<mir2::common::MsgId, N>& msg_ids,
                   mir2::common::MsgId msg_id) {
  for (const auto candidate : msg_ids) {
    if (candidate == msg_id) {
      return true;
    }
  }
  return false;
}

std::string JoinMsgIds(const std::vector<mir2::common::MsgId>& msg_ids) {
  std::ostringstream out;
  bool first = true;
  for (const auto msg_id : msg_ids) {
    if (!first) {
      out << ",";
    }
    first = false;
    out << static_cast<uint16_t>(msg_id);
  }
  return out.str();
}

bool HasTypedBindingTrait(mir2::common::MsgId msg_id) {
  using mir2::common::MsgId;
  using namespace mir2::common::protocol::bindings;
  switch (msg_id) {
    case MsgId::kLoginReq:
      return kHasTypedBinding<MsgId::kLoginReq>;
    case MsgId::kLoginRsp:
      return kHasTypedBinding<MsgId::kLoginRsp>;
    case MsgId::kCreateRoleReq:
      return kHasTypedBinding<MsgId::kCreateRoleReq>;
    case MsgId::kCreateRoleRsp:
      return kHasTypedBinding<MsgId::kCreateRoleRsp>;
    case MsgId::kMoveReq:
      return kHasTypedBinding<MsgId::kMoveReq>;
    case MsgId::kMoveRsp:
      return kHasTypedBinding<MsgId::kMoveRsp>;
    case MsgId::kAttackReq:
      return kHasTypedBinding<MsgId::kAttackReq>;
    case MsgId::kAttackRsp:
      return kHasTypedBinding<MsgId::kAttackRsp>;
    case MsgId::kSkillReq:
      return kHasTypedBinding<MsgId::kSkillReq>;
    case MsgId::kSkillRsp:
      return kHasTypedBinding<MsgId::kSkillRsp>;
    case MsgId::kUseItemReq:
      return kHasTypedBinding<MsgId::kUseItemReq>;
    case MsgId::kUseItemRsp:
      return kHasTypedBinding<MsgId::kUseItemRsp>;
    case MsgId::kPickupItemReq:
      return kHasTypedBinding<MsgId::kPickupItemReq>;
    case MsgId::kPickupItemRsp:
      return kHasTypedBinding<MsgId::kPickupItemRsp>;
    case MsgId::kDropItemReq:
      return kHasTypedBinding<MsgId::kDropItemReq>;
    case MsgId::kDropItemRsp:
      return kHasTypedBinding<MsgId::kDropItemRsp>;
    case MsgId::kEquipReq:
      return kHasTypedBinding<MsgId::kEquipReq>;
    case MsgId::kEquipRsp:
      return kHasTypedBinding<MsgId::kEquipRsp>;
    case MsgId::kUnequipReq:
      return kHasTypedBinding<MsgId::kUnequipReq>;
    case MsgId::kUnequipRsp:
      return kHasTypedBinding<MsgId::kUnequipRsp>;
    case MsgId::kChatReq:
      return kHasTypedBinding<MsgId::kChatReq>;
    case MsgId::kChatRsp:
      return kHasTypedBinding<MsgId::kChatRsp>;
    case MsgId::kGuildCreateReq:
      return kHasTypedBinding<MsgId::kGuildCreateReq>;
    case MsgId::kGuildCreateRsp:
      return kHasTypedBinding<MsgId::kGuildCreateRsp>;
    case MsgId::kGuildJoinReq:
      return kHasTypedBinding<MsgId::kGuildJoinReq>;
    case MsgId::kGuildJoinRsp:
      return kHasTypedBinding<MsgId::kGuildJoinRsp>;
    case MsgId::kGuildLeaveReq:
      return kHasTypedBinding<MsgId::kGuildLeaveReq>;
    case MsgId::kGuildLeaveRsp:
      return kHasTypedBinding<MsgId::kGuildLeaveRsp>;
    case MsgId::kGuildKickReq:
      return kHasTypedBinding<MsgId::kGuildKickReq>;
    case MsgId::kGuildKickRsp:
      return kHasTypedBinding<MsgId::kGuildKickRsp>;
    case MsgId::kGuildDeclareWarReq:
      return kHasTypedBinding<MsgId::kGuildDeclareWarReq>;
    case MsgId::kGuildDeclareWarRsp:
      return kHasTypedBinding<MsgId::kGuildDeclareWarRsp>;
    case MsgId::kGuildCancelWarReq:
      return kHasTypedBinding<MsgId::kGuildCancelWarReq>;
    case MsgId::kGuildCancelWarRsp:
      return kHasTypedBinding<MsgId::kGuildCancelWarRsp>;
    case MsgId::kGuildMakeAllyReq:
      return kHasTypedBinding<MsgId::kGuildMakeAllyReq>;
    case MsgId::kGuildMakeAllyRsp:
      return kHasTypedBinding<MsgId::kGuildMakeAllyRsp>;
    case MsgId::kGuildBreakAllyReq:
      return kHasTypedBinding<MsgId::kGuildBreakAllyReq>;
    case MsgId::kGuildBreakAllyRsp:
      return kHasTypedBinding<MsgId::kGuildBreakAllyRsp>;
    case MsgId::kTradeReq:
      return kHasTypedBinding<MsgId::kTradeReq>;
    case MsgId::kTradeRsp:
      return kHasTypedBinding<MsgId::kTradeRsp>;
    case MsgId::kTradeAddItemReq:
      return kHasTypedBinding<MsgId::kTradeAddItemReq>;
    case MsgId::kTradeAddItemRsp:
      return kHasTypedBinding<MsgId::kTradeAddItemRsp>;
    case MsgId::kTradeSetGoldReq:
      return kHasTypedBinding<MsgId::kTradeSetGoldReq>;
    case MsgId::kTradeSetGoldRsp:
      return kHasTypedBinding<MsgId::kTradeSetGoldRsp>;
    case MsgId::kTradeConfirmReq:
      return kHasTypedBinding<MsgId::kTradeConfirmReq>;
    case MsgId::kTradeConfirmRsp:
      return kHasTypedBinding<MsgId::kTradeConfirmRsp>;
    case MsgId::kTradeCancelReq:
      return kHasTypedBinding<MsgId::kTradeCancelReq>;
    case MsgId::kTradeCancelRsp:
      return kHasTypedBinding<MsgId::kTradeCancelRsp>;
    case MsgId::kTradeUpdate:
      return kHasTypedBinding<MsgId::kTradeUpdate>;
    case MsgId::kTradeComplete:
      return kHasTypedBinding<MsgId::kTradeComplete>;
    case MsgId::kPartyInviteReq:
      return kHasTypedBinding<MsgId::kPartyInviteReq>;
    case MsgId::kPartyInviteRsp:
      return kHasTypedBinding<MsgId::kPartyInviteRsp>;
    case MsgId::kPartyJoinReq:
      return kHasTypedBinding<MsgId::kPartyJoinReq>;
    case MsgId::kPartyJoinRsp:
      return kHasTypedBinding<MsgId::kPartyJoinRsp>;
    case MsgId::kPartyLeaveReq:
      return kHasTypedBinding<MsgId::kPartyLeaveReq>;
    case MsgId::kPartyLeaveRsp:
      return kHasTypedBinding<MsgId::kPartyLeaveRsp>;
    case MsgId::kPartyKickReq:
      return kHasTypedBinding<MsgId::kPartyKickReq>;
    case MsgId::kPartyKickRsp:
      return kHasTypedBinding<MsgId::kPartyKickRsp>;
    case MsgId::kPartyUpdate:
      return kHasTypedBinding<MsgId::kPartyUpdate>;
    case MsgId::kGuildUpdateNoticeReq:
      return kHasTypedBinding<MsgId::kGuildUpdateNoticeReq>;
    case MsgId::kGuildUpdateNoticeRsp:
      return kHasTypedBinding<MsgId::kGuildUpdateNoticeRsp>;
    case MsgId::kGuildUpdateRankReq:
      return kHasTypedBinding<MsgId::kGuildUpdateRankReq>;
    case MsgId::kGuildUpdateRankRsp:
      return kHasTypedBinding<MsgId::kGuildUpdateRankRsp>;
    case MsgId::kGuildInfoSync:
      return kHasTypedBinding<MsgId::kGuildInfoSync>;
    case MsgId::kRankingReq:
      return kHasTypedBinding<MsgId::kRankingReq>;
    case MsgId::kRankingRsp:
      return kHasTypedBinding<MsgId::kRankingRsp>;
    case MsgId::kRankingMyRankReq:
      return kHasTypedBinding<MsgId::kRankingMyRankReq>;
    case MsgId::kRankingMyRankRsp:
      return kHasTypedBinding<MsgId::kRankingMyRankRsp>;
    case MsgId::kMailSendReq:
      return kHasTypedBinding<MsgId::kMailSendReq>;
    case MsgId::kMailSendRsp:
      return kHasTypedBinding<MsgId::kMailSendRsp>;
    case MsgId::kMailListReq:
      return kHasTypedBinding<MsgId::kMailListReq>;
    case MsgId::kMailListRsp:
      return kHasTypedBinding<MsgId::kMailListRsp>;
    case MsgId::kMailReadReq:
      return kHasTypedBinding<MsgId::kMailReadReq>;
    case MsgId::kMailReadRsp:
      return kHasTypedBinding<MsgId::kMailReadRsp>;
    case MsgId::kMailDeleteReq:
      return kHasTypedBinding<MsgId::kMailDeleteReq>;
    case MsgId::kMailDeleteRsp:
      return kHasTypedBinding<MsgId::kMailDeleteRsp>;
    case MsgId::kMailClaimReq:
      return kHasTypedBinding<MsgId::kMailClaimReq>;
    case MsgId::kMailClaimRsp:
      return kHasTypedBinding<MsgId::kMailClaimRsp>;
    case MsgId::kMailNotify:
      return kHasTypedBinding<MsgId::kMailNotify>;
    case MsgId::kAchievementListReq:
      return kHasTypedBinding<MsgId::kAchievementListReq>;
    case MsgId::kAchievementListRsp:
      return kHasTypedBinding<MsgId::kAchievementListRsp>;
    case MsgId::kAchievementClaimReq:
      return kHasTypedBinding<MsgId::kAchievementClaimReq>;
    case MsgId::kAchievementClaimRsp:
      return kHasTypedBinding<MsgId::kAchievementClaimRsp>;
    case MsgId::kAchievementUpdate:
      return kHasTypedBinding<MsgId::kAchievementUpdate>;
    case MsgId::kAuctionListReq:
      return kHasTypedBinding<MsgId::kAuctionListReq>;
    case MsgId::kAuctionListRsp:
      return kHasTypedBinding<MsgId::kAuctionListRsp>;
    case MsgId::kAuctionSellReq:
      return kHasTypedBinding<MsgId::kAuctionSellReq>;
    case MsgId::kAuctionSellRsp:
      return kHasTypedBinding<MsgId::kAuctionSellRsp>;
    case MsgId::kAuctionBuyReq:
      return kHasTypedBinding<MsgId::kAuctionBuyReq>;
    case MsgId::kAuctionBuyRsp:
      return kHasTypedBinding<MsgId::kAuctionBuyRsp>;
    case MsgId::kAuctionCancelReq:
      return kHasTypedBinding<MsgId::kAuctionCancelReq>;
    case MsgId::kAuctionCancelRsp:
      return kHasTypedBinding<MsgId::kAuctionCancelRsp>;
    case MsgId::kAuctionNotify:
      return kHasTypedBinding<MsgId::kAuctionNotify>;
    default:
      return false;
  }
}

}  // namespace

TEST(TypedMessageCodecGateTest, TypedBindingMatrixMatchesMessageCodecMatrix) {
  std::vector<mir2::common::MsgId> missing_in_typed;
  for (const auto msg_id : mir2::common::kMessageCodecManagedMsgIds) {
    if (!ContainsMsgId(mir2::common::protocol::bindings::kTypedBindingMsgIds, msg_id)) {
      missing_in_typed.push_back(msg_id);
    }
  }

  std::vector<mir2::common::MsgId> unexpected_in_typed;
  for (const auto msg_id : mir2::common::protocol::bindings::kTypedBindingMsgIds) {
    if (!ContainsMsgId(mir2::common::kMessageCodecManagedMsgIds, msg_id)) {
      unexpected_in_typed.push_back(msg_id);
    }
  }

  EXPECT_TRUE(missing_in_typed.empty())
      << "MessageCodec-managed msg_ids missing typed binding matrix: "
      << JoinMsgIds(missing_in_typed);
  EXPECT_TRUE(unexpected_in_typed.empty())
      << "Typed binding matrix has unexpected msg_ids: "
      << JoinMsgIds(unexpected_in_typed);
}

TEST(TypedMessageCodecGateTest, CoverageGateSatisfiedForManagedMessages) {
  int typed_covered_count = 0;
  for (const auto msg_id : mir2::common::kMessageCodecManagedMsgIds) {
    if (ContainsMsgId(mir2::common::protocol::bindings::kTypedBindingMsgIds, msg_id)) {
      ++typed_covered_count;
    }
  }

  const auto total_count =
      static_cast<int>(mir2::common::kMessageCodecManagedMsgIds.size());
  ASSERT_GT(total_count, 0);

  const int coverage_percent = (typed_covered_count * 100) / total_count;
  EXPECT_GE(coverage_percent,
            mir2::common::protocol::bindings::kTypedCodecCoverageGatePercent)
      << "Typed codec coverage below gate: covered=" << typed_covered_count
      << ", total=" << total_count
      << ", coverage_percent=" << coverage_percent;
}

TEST(TypedMessageCodecGateTest, ManagedMessageMatrixHasNoDuplicateMsgIds) {
  std::unordered_set<uint16_t> seen;
  for (const auto msg_id : mir2::common::kMessageCodecManagedMsgIds) {
    const auto inserted = seen.insert(static_cast<uint16_t>(msg_id)).second;
    EXPECT_TRUE(inserted) << "Duplicate message codec matrix msg_id="
                          << static_cast<uint16_t>(msg_id);
  }
}

TEST(TypedMessageCodecGateTest, ManagedMessagesHaveTypedBindingTrait) {
  std::vector<mir2::common::MsgId> missing_trait;
  for (const auto msg_id : mir2::common::kMessageCodecManagedMsgIds) {
    if (!HasTypedBindingTrait(msg_id)) {
      missing_trait.push_back(msg_id);
    }
  }

  EXPECT_TRUE(missing_trait.empty())
      << "Managed msg_ids missing kHasTypedBinding trait coverage: "
      << JoinMsgIds(missing_trait);
}
