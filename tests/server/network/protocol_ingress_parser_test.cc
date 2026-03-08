#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <vector>

#include "common/enums.h"
#include "common/protocol/packet_codec.h"
#include "network/packet_codec.h"
#include "network/protocol_ingress_parser.h"

namespace mir2::network {
namespace {

constexpr uint16_t kMsgIdHeartbeat =
    static_cast<uint16_t>(mir2::common::MsgId::kHeartbeat);
constexpr uint16_t kMsgIdLoginReq =
    static_cast<uint16_t>(mir2::common::MsgId::kLoginReq);
constexpr uint32_t kLegacyV1Magic = 0x4D495232;  // "MIR2"

TEST(ProtocolIngressParserTest, ParsesCompleteV2PacketAcrossChunks) {
  ProtocolIngressParser parser(mir2::common::ChannelType::kTcp);
  auto encoded = PacketCodec::EncodeV2(
      kMsgIdHeartbeat, nullptr, 0, /*sequence=*/7);
  ASSERT_FALSE(encoded.empty());

  ASSERT_TRUE(parser.AppendBytes(encoded.data(), 4, encoded.size()));
  const IngressParseResult partial = parser.NextPacket();
  EXPECT_EQ(partial.action, IngressParseAction::kNeedMoreData);

  ASSERT_TRUE(parser.AppendBytes(
      encoded.data() + 4, encoded.size() - 4, encoded.size()));
  const IngressParseResult result = parser.NextPacket();
  EXPECT_EQ(result.action, IngressParseAction::kPacketReady);
  EXPECT_EQ(result.error, IngressParseError::kNone);
  EXPECT_EQ(result.parsed.packet.msg_id, kMsgIdHeartbeat);
  EXPECT_TRUE(result.parsed.packet.payload.empty());
  EXPECT_EQ(result.parsed.sequence, 7);
  EXPECT_TRUE(parser.IsProtocolVersionDetected());
  EXPECT_EQ(parser.GetProtocolVersion(), ProtocolVersion::kV2);
}

TEST(ProtocolIngressParserTest, RejectsLegacyV1Magic) {
  ProtocolIngressParser parser(mir2::common::ChannelType::kTcp);
  std::array<uint8_t, sizeof(uint32_t)> header{};
  std::memcpy(header.data(), &kLegacyV1Magic, sizeof(kLegacyV1Magic));
  ASSERT_TRUE(parser.AppendBytes(header.data(), header.size(), header.size()));

  const IngressParseResult result = parser.NextPacket();
  EXPECT_EQ(result.action, IngressParseAction::kFatalError);
  EXPECT_EQ(result.error, IngressParseError::kLegacyV1);
}

TEST(ProtocolIngressParserTest, RejectsKcpFlagOnTcpChannel) {
  ProtocolIngressParser parser(mir2::common::ChannelType::kTcp);
  auto encoded = PacketCodec::EncodeV2(
      kMsgIdHeartbeat,
      nullptr,
      0,
      /*sequence=*/1,
      mir2::common::PacketHeaderV2::kFlagChannelKcp);
  ASSERT_FALSE(encoded.empty());
  ASSERT_TRUE(parser.AppendBytes(encoded.data(), encoded.size(), encoded.size()));

  const IngressParseResult result = parser.NextPacket();
  EXPECT_EQ(result.action, IngressParseAction::kFatalError);
  EXPECT_EQ(result.error, IngressParseError::kChannelMismatch);
}

TEST(ProtocolIngressParserTest, RejectsOversizedPayloadFromHeader) {
  ProtocolIngressParser parser(mir2::common::ChannelType::kTcp);
  PacketHeaderV2 header{};
  header.msg_id = kMsgIdHeartbeat;
  header.payload_size = static_cast<uint32_t>(mir2::common::kMaxPayloadSize + 1);
  auto header_bytes = header.ToBytes();
  ASSERT_TRUE(parser.AppendBytes(
      header_bytes.data(), header_bytes.size(), header_bytes.size()));

  const IngressParseResult result = parser.NextPacket();
  EXPECT_EQ(result.action, IngressParseAction::kFatalError);
  EXPECT_EQ(result.error, IngressParseError::kPayloadTooLarge);
}

TEST(ProtocolIngressParserTest, AppendBytesFailsWhenBufferWouldOverflow) {
  ProtocolIngressParser parser(mir2::common::ChannelType::kTcp);
  const std::array<uint8_t, 4> first = {0, 1, 2, 3};
  const std::array<uint8_t, 1> second = {4};

  EXPECT_TRUE(parser.AppendBytes(first.data(), first.size(), first.size()));
  EXPECT_FALSE(parser.AppendBytes(second.data(), second.size(), first.size()));
}

TEST(ProtocolIngressParserTest, DecodeFailureIsReportedAsFatal) {
  ProtocolIngressParser parser(mir2::common::ChannelType::kTcp);
  auto encoded = PacketCodec::EncodeV2(
      kMsgIdLoginReq, nullptr, 0, /*sequence=*/2);
  ASSERT_FALSE(encoded.empty());
  ASSERT_TRUE(parser.AppendBytes(encoded.data(), encoded.size(), encoded.size()));

  const IngressParseResult result = parser.NextPacket();
  EXPECT_EQ(result.action, IngressParseAction::kFatalError);
  EXPECT_EQ(result.error, IngressParseError::kDecodeFailed);
}

}  // namespace
}  // namespace mir2::network
