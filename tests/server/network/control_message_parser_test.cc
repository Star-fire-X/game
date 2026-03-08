#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

#include <flatbuffers/flatbuffers.h>

#include "network/control_message_parser.h"
#include "system_generated.h"

namespace mir2::network {
namespace {

TEST(ControlMessageParserTest, ParseKcpUpgradeRequestAcceptsEmptyPayload) {
  EXPECT_EQ(ControlMessageParser::ParseKcpUpgradeRequest({}),
            KcpUpgradeRequestStatus::kOk);
}

TEST(ControlMessageParserTest, ParseKcpUpgradeRequestRejectsMalformedPayload) {
  const std::vector<uint8_t> malformed = {0x01, 0x02, 0x03};
  EXPECT_EQ(ControlMessageParser::ParseKcpUpgradeRequest(malformed),
            KcpUpgradeRequestStatus::kInvalidPayload);
}

TEST(ControlMessageParserTest, ParseKcpUpgradeRequestAcceptsValidPayload) {
  flatbuffers::FlatBufferBuilder builder;
  const auto request = mir2::proto::CreateKcpUpgradeRequest(builder, 7001);
  builder.Finish(request);
  const uint8_t* data = builder.GetBufferPointer();
  const std::vector<uint8_t> payload(data, data + builder.GetSize());

  EXPECT_EQ(ControlMessageParser::ParseKcpUpgradeRequest(payload),
            KcpUpgradeRequestStatus::kOk);
}

TEST(ControlMessageParserTest, ParseKcpHeartbeatTimestampUsesFallbackOnEmptyPayload) {
  uint32_t timestamp = 0;
  EXPECT_TRUE(ControlMessageParser::ParseKcpHeartbeatTimestamp(
      {}, 12345, &timestamp));
  EXPECT_EQ(timestamp, 12345u);
}

TEST(ControlMessageParserTest, ParseKcpHeartbeatTimestampRejectsMalformedPayload) {
  const std::vector<uint8_t> malformed = {0xFA, 0xFB};
  uint32_t timestamp = 0;
  EXPECT_FALSE(ControlMessageParser::ParseKcpHeartbeatTimestamp(
      malformed, 54321, &timestamp));
  EXPECT_EQ(timestamp, 54321u);
}

TEST(ControlMessageParserTest, ParseKcpHeartbeatTimestampAcceptsValidPayload) {
  flatbuffers::FlatBufferBuilder builder;
  const auto heartbeat = mir2::proto::CreateKcpHeartbeat(builder, 77777);
  builder.Finish(heartbeat);
  const uint8_t* data = builder.GetBufferPointer();
  const std::vector<uint8_t> payload(data, data + builder.GetSize());

  uint32_t timestamp = 0;
  EXPECT_TRUE(ControlMessageParser::ParseKcpHeartbeatTimestamp(
      payload, 1, &timestamp));
  EXPECT_EQ(timestamp, 77777u);
}

TEST(ControlMessageParserTest, ParseKcpHeartbeatTimestampRequiresOutputPointer) {
  EXPECT_FALSE(ControlMessageParser::ParseKcpHeartbeatTimestamp({}, 7, nullptr));
}

}  // namespace
}  // namespace mir2::network
