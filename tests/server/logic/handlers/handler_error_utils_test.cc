#include <gtest/gtest.h>

#include "common/protocol/message_codec.h"
#include "common/types/error_codes.h"
#include "logic/handlers/handler_error_utils.h"

namespace mir2::logic::test {

TEST(HandlerErrorUtilsTest, ToCommonErrorMapsCodecStatuses) {
  EXPECT_EQ(ToCommonError(mir2::common::MessageCodecStatus::kOk),
            mir2::common::ErrorCode::kOk);
  EXPECT_EQ(ToCommonError(mir2::common::MessageCodecStatus::kInvalidMsgId),
            mir2::common::ErrorCode::kDecodeInvalidMsgId);
  EXPECT_EQ(ToCommonError(mir2::common::MessageCodecStatus::kInvalidPayload),
            mir2::common::ErrorCode::kDecodeInvalidPayload);
  EXPECT_EQ(ToCommonError(mir2::common::MessageCodecStatus::kMissingField),
            mir2::common::ErrorCode::kDecodeMissingField);
  EXPECT_EQ(ToCommonError(mir2::common::MessageCodecStatus::kStringTooLong),
            mir2::common::ErrorCode::kDecodeStringTooLong);
  EXPECT_EQ(ToCommonError(mir2::common::MessageCodecStatus::kValueOutOfRange),
            mir2::common::ErrorCode::kDecodeValueOutOfRange);
}

TEST(HandlerErrorUtilsTest, ToProtoErrorSupportsExtendedRanges) {
  EXPECT_EQ(ToProtoError(mir2::common::ErrorCode::kTradeInvalidState),
            mir2::proto::ErrorCode::ERR_TRADE_INVALID_STATE);
  EXPECT_EQ(ToProtoError(mir2::common::ErrorCode::kPartyFull),
            mir2::proto::ErrorCode::ERR_PARTY_FULL);
  EXPECT_EQ(ToProtoError(mir2::common::ErrorCode::kMailAlreadyClaimed),
            mir2::proto::ErrorCode::ERR_MAIL_ALREADY_CLAIMED);
  EXPECT_EQ(ToProtoError(mir2::common::ErrorCode::kAuctionBidTooLow),
            mir2::proto::ErrorCode::ERR_AUCTION_BID_TOO_LOW);
}

TEST(HandlerErrorUtilsTest, ToProtoErrorMapsModernCharacterAliases) {
  EXPECT_EQ(ToProtoError(mir2::common::ErrorCode::ACCOUNT_NOT_FOUND),
            mir2::proto::ErrorCode::ERR_ACCOUNT_NOT_FOUND);
  EXPECT_EQ(ToProtoError(mir2::common::ErrorCode::INVALID_CHARACTER_NAME),
            mir2::proto::ErrorCode::ERR_INVALID_ACTION);
  EXPECT_EQ(ToProtoError(mir2::common::ErrorCode::INVALID_CHARACTER_CLASS),
            mir2::proto::ErrorCode::ERR_INVALID_ACTION);
  EXPECT_EQ(ToProtoError(mir2::common::ErrorCode::INVALID_CREDENTIALS),
            mir2::proto::ErrorCode::ERR_INVALID_ACTION);
}

}  // namespace mir2::logic::test
