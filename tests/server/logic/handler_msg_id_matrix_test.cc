#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <sstream>
#include <string>
#include <unordered_set>
#include <vector>

#include "common/protocol/universal_forward_msg_ids.h"
#include "logic/handler_msg_id_matrix.h"

namespace {

template <size_t N>
void AppendUnexpected(const std::array<uint16_t, N>& msg_ids,
                      std::vector<uint16_t>* unexpected,
                      std::unordered_set<uint16_t>* seen) {
  for (const auto msg_id : msg_ids) {
    if (mir2::common::protocol::IsUniversalForwardMsgId(msg_id)) {
      continue;
    }
    if (!seen->insert(msg_id).second) {
      continue;
    }
    unexpected->push_back(msg_id);
  }
}

template <size_t N>
void AppendUnexpectedPlaceholders(
    const std::array<mir2::logic::matrix::PlaceholderBinding, N>& bindings,
    std::vector<uint16_t>* unexpected,
    std::unordered_set<uint16_t>* seen) {
  for (const auto& binding : bindings) {
    if (mir2::common::protocol::IsUniversalForwardMsgId(binding.msg_id)) {
      continue;
    }
    if (!seen->insert(binding.msg_id).second) {
      continue;
    }
    unexpected->push_back(binding.msg_id);
  }
}

std::string JoinMsgIds(const std::vector<uint16_t>& msg_ids) {
  std::ostringstream out;
  bool first = true;
  for (const auto msg_id : msg_ids) {
    if (!first) {
      out << ",";
    }
    first = false;
    out << msg_id;
  }
  return out.str();
}

}  // namespace

TEST(LogicHandlerMsgIdMatrixTest, UniversalForwardMatrixCoveredByLogicRegistry) {
  std::vector<uint16_t> missing;
  for (const auto msg_id : mir2::common::protocol::kUniversalForwardMsgIds) {
    if (!mir2::logic::matrix::IsLogicRegistryMsgId(msg_id)) {
      missing.push_back(msg_id);
    }
  }

  EXPECT_TRUE(missing.empty())
      << "Universal-forward msg_ids missing in logic registry matrix: "
      << JoinMsgIds(missing);
  EXPECT_TRUE(mir2::logic::matrix::IsUniversalForwardCoveredByLogicRegistry());
}

TEST(LogicHandlerMsgIdMatrixTest, LogicRegistryMatrixIsSubsetOfUniversalForward) {
  std::vector<uint16_t> unexpected;
  std::unordered_set<uint16_t> seen;

  AppendUnexpected(mir2::logic::matrix::kLoginHandlerMsgIds, &unexpected, &seen);
  AppendUnexpected(mir2::logic::matrix::kMovementHandlerMsgIds, &unexpected, &seen);
  AppendUnexpected(mir2::logic::matrix::kAttackHandlerMsgIds, &unexpected, &seen);
  AppendUnexpected(mir2::logic::matrix::kSkillHandlerMsgIds, &unexpected, &seen);
  AppendUnexpected(mir2::logic::matrix::kCharacterHandlerMsgIds, &unexpected, &seen);
  AppendUnexpected(mir2::logic::matrix::kChatHandlerMsgIds, &unexpected, &seen);
  AppendUnexpected(mir2::logic::matrix::kItemHandlerMsgIds, &unexpected, &seen);
  AppendUnexpected(mir2::logic::matrix::kGuildHandlerMsgIds, &unexpected, &seen);
  AppendUnexpected(mir2::logic::matrix::kTradeHandlerMsgIds, &unexpected, &seen);
  AppendUnexpected(mir2::logic::matrix::kPartyHandlerMsgIds, &unexpected, &seen);
  AppendUnexpected(mir2::logic::matrix::kRankingHandlerMsgIds, &unexpected, &seen);
  AppendUnexpected(mir2::logic::matrix::kMailHandlerMsgIds, &unexpected, &seen);
  AppendUnexpected(mir2::logic::matrix::kAchievementHandlerMsgIds, &unexpected, &seen);
  AppendUnexpected(mir2::logic::matrix::kAuctionHandlerMsgIds, &unexpected, &seen);
  AppendUnexpected(mir2::logic::matrix::kNpcHandlerMsgIds, &unexpected, &seen);
  AppendUnexpectedPlaceholders(
      mir2::logic::matrix::kPlaceholderBindings, &unexpected, &seen);

  EXPECT_TRUE(unexpected.empty())
      << "Logic registry matrix contains msg_ids outside universal-forward matrix: "
      << JoinMsgIds(unexpected);
  EXPECT_TRUE(mir2::logic::matrix::IsLogicRegistrySubsetOfUniversalForward());
}
