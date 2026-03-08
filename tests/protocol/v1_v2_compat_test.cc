#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

#include <flatbuffers/flatbuffers.h>

#include "chat_generated.h"
#include "common/enums.h"
#include "common/protocol/packet_codec.h"
#include "game_generated.h"
#include "login_generated.h"

namespace {

std::vector<uint8_t> BuildLoginReqPayload() {
  flatbuffers::FlatBufferBuilder builder;
  const auto user = builder.CreateString("compat_user");
  const auto pass = builder.CreateString("compat_pass");
  const auto version = builder.CreateString("1");
  const auto req = mir2::proto::CreateLoginReq(builder, user, pass, version);
  builder.Finish(req);
  const uint8_t* data = builder.GetBufferPointer();
  return std::vector<uint8_t>(data, data + builder.GetSize());
}

std::vector<uint8_t> BuildMoveReqPayload() {
  flatbuffers::FlatBufferBuilder builder;
  const auto req = mir2::proto::CreateMoveReq(builder, 12, 34);
  builder.Finish(req);
  const uint8_t* data = builder.GetBufferPointer();
  return std::vector<uint8_t>(data, data + builder.GetSize());
}

std::vector<uint8_t> BuildChatReqPayload() {
  flatbuffers::FlatBufferBuilder builder;
  const auto text = builder.CreateString("compat_test");
  const auto req = mir2::proto::CreateChatReq(
      builder,
      mir2::proto::ChatChannel::WORLD,
      text,
      0);
  builder.Finish(req);
  const uint8_t* data = builder.GetBufferPointer();
  return std::vector<uint8_t>(data, data + builder.GetSize());
}

std::vector<uint8_t> BuildLegacyV1Packet(uint16_t msg_id, const std::vector<uint8_t>& payload) {
  constexpr uint32_t kLegacyV1Magic = 0x4D495232;  // "MIR2"
  const uint32_t payload_size = static_cast<uint32_t>(payload.size());
  std::vector<uint8_t> bytes(10 + payload.size(), 0);
  std::memcpy(bytes.data(), &kLegacyV1Magic, sizeof(kLegacyV1Magic));
  std::memcpy(bytes.data() + sizeof(kLegacyV1Magic), &msg_id, sizeof(msg_id));
  std::memcpy(bytes.data() + sizeof(kLegacyV1Magic) + sizeof(msg_id),
              &payload_size,
              sizeof(payload_size));
  if (!payload.empty()) {
    std::memcpy(bytes.data() + 10, payload.data(), payload.size());
  }
  return bytes;
}

}  // namespace

TEST(V1V2CompatTest, V2OnlyStreamDecodesByDetectedProtocol) {
  const auto login_payload = BuildLoginReqPayload();
  const auto move_payload = BuildMoveReqPayload();
  const auto chat_payload = BuildChatReqPayload();

  const auto v2_login = mir2::common::EncodePacketV2(
      static_cast<uint16_t>(mir2::common::MsgId::kLoginReq),
      login_payload.data(),
      login_payload.size(),
      /*sequence=*/1,
      0);
  const auto v2_move = mir2::common::EncodePacketV2(
      static_cast<uint16_t>(mir2::common::MsgId::kMoveReq),
      move_payload.data(),
      move_payload.size(),
      /*sequence=*/2,
      mir2::common::PacketHeaderV2::kFlagChannelKcp);
  const auto v2_chat = mir2::common::EncodePacketV2(
      static_cast<uint16_t>(mir2::common::MsgId::kChatReq),
      chat_payload.data(),
      chat_payload.size(),
      /*sequence=*/3,
      0);

  std::vector<uint8_t> stream;
  stream.insert(stream.end(), v2_login.begin(), v2_login.end());
  stream.insert(stream.end(), v2_move.begin(), v2_move.end());
  stream.insert(stream.end(), v2_chat.begin(), v2_chat.end());

  std::size_t offset = 0;
  int decoded_count = 0;
  while (offset < stream.size()) {
    const uint8_t* packet_ptr = stream.data() + offset;
    EXPECT_EQ(mir2::common::DetectProtocolVersion(packet_ptr),
              mir2::common::ProtocolVersion::kV2);

    mir2::common::PacketHeaderV2 header_v2{};
    ASSERT_TRUE(mir2::common::PacketHeaderV2::FromBytes(
        packet_ptr, stream.size() - offset, &header_v2));
    const std::size_t packet_size =
        mir2::common::PacketHeaderV2::kSize + static_cast<std::size_t>(header_v2.payload_size);

    mir2::common::NetworkPacket decoded;
    uint16_t sequence = 0;
    uint8_t flags = 0;
    ASSERT_EQ(mir2::common::DecodePacketV2(packet_ptr, packet_size, &decoded, &sequence, &flags),
              mir2::common::DecodeStatus::kOk);

    if (decoded_count == 0) {
      EXPECT_EQ(decoded.msg_id, static_cast<uint16_t>(mir2::common::MsgId::kLoginReq));
      EXPECT_EQ(decoded.payload, login_payload);
      EXPECT_EQ(sequence, 1);
    } else if (decoded_count == 1) {
      EXPECT_EQ(decoded.msg_id, static_cast<uint16_t>(mir2::common::MsgId::kMoveReq));
      EXPECT_EQ(decoded.payload, move_payload);
      EXPECT_EQ(sequence, 2);
      EXPECT_EQ(flags, mir2::common::PacketHeaderV2::kFlagChannelKcp);
    } else {
      EXPECT_EQ(decoded.msg_id, static_cast<uint16_t>(mir2::common::MsgId::kChatReq));
      EXPECT_EQ(decoded.payload, chat_payload);
      EXPECT_EQ(sequence, 3);
    }

    offset += packet_size;
    ++decoded_count;
  }

  EXPECT_EQ(decoded_count, 3);
  EXPECT_EQ(offset, stream.size());
}

TEST(V1V2CompatTest, V1PacketDetectedAndRejectedByV2Decoder) {
  const auto login_payload = BuildLoginReqPayload();
  const auto legacy_v1 = BuildLegacyV1Packet(
      static_cast<uint16_t>(mir2::common::MsgId::kLoginReq),
      login_payload);
  ASSERT_FALSE(legacy_v1.empty());

  EXPECT_EQ(mir2::common::DetectProtocolVersion(legacy_v1.data()),
            mir2::common::ProtocolVersion::kV1);

  mir2::common::NetworkPacket decoded;
  EXPECT_EQ(mir2::common::DecodePacketV2(legacy_v1.data(), legacy_v1.size(), &decoded),
            mir2::common::DecodeStatus::kProtocolNotSupported);
}
