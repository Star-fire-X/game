#include <gtest/gtest.h>

#include <vector>

#include "common/enums.h"
#include "common/protocol/packet_codec.h"
#include "network/packet_codec.h"

namespace {

std::vector<uint8_t> BuildPayload(size_t size) {
    std::vector<uint8_t> payload(size);
    for (size_t i = 0; i < payload.size(); ++i) {
        payload[i] = static_cast<uint8_t>(i & 0xFF);
    }
    return payload;
}

}  // namespace

TEST(packet_codec, EncodeDecodeRoundTrip) {
    const std::vector<mir2::common::MsgId> msg_ids = {
        mir2::common::MsgId::kLoginReq,
        mir2::common::MsgId::kMoveReq,
        mir2::common::MsgId::kChatReq,
        mir2::common::MsgId::kHeartbeat,
        mir2::common::MsgId::kKick
    };

    for (const auto msg_id : msg_ids) {
        const auto payload = BuildPayload(32);
        const auto encoded = mir2::common::EncodePacket(static_cast<uint16_t>(msg_id),
                                                        payload.data(),
                                                        payload.size());
        ASSERT_FALSE(encoded.empty());

        mir2::common::NetworkPacket decoded;
        const auto status = mir2::common::DecodePacket(encoded.data(), encoded.size(), &decoded);
        EXPECT_EQ(status, mir2::common::DecodeStatus::kOk);
        EXPECT_EQ(decoded.msg_id, static_cast<uint16_t>(msg_id));
        EXPECT_EQ(decoded.payload, payload);
    }
}

TEST(packet_codec, EmptyPayloadRoundTrip) {
    const auto encoded = mir2::common::EncodePacket(
        static_cast<uint16_t>(mir2::common::MsgId::kHeartbeat), nullptr, 0);
    ASSERT_EQ(encoded.size(), mir2::common::PacketHeader::kSize);

    mir2::common::NetworkPacket decoded;
    const auto status = mir2::common::DecodePacket(encoded.data(), encoded.size(), &decoded);
    EXPECT_EQ(status, mir2::common::DecodeStatus::kOk);
    EXPECT_EQ(decoded.msg_id, static_cast<uint16_t>(mir2::common::MsgId::kHeartbeat));
    EXPECT_TRUE(decoded.payload.empty());
}

TEST(packet_codec, MaxPayloadRoundTrip) {
    const auto payload = BuildPayload(mir2::common::kMaxPayloadSize);
    const auto encoded = mir2::common::EncodePacket(
        static_cast<uint16_t>(mir2::common::MsgId::kMoveReq),
        payload.data(),
        payload.size());
    ASSERT_EQ(encoded.size(), mir2::common::PacketHeader::kSize + payload.size());

    mir2::common::NetworkPacket decoded;
    const auto status = mir2::common::DecodePacket(encoded.data(), encoded.size(), &decoded);
    EXPECT_EQ(status, mir2::common::DecodeStatus::kOk);
    EXPECT_EQ(decoded.msg_id, static_cast<uint16_t>(mir2::common::MsgId::kMoveReq));
    EXPECT_EQ(decoded.payload, payload);
}

TEST(packet_codec, InvalidMagicRejected) {
    mir2::common::PacketHeader header;
    header.magic = 0x12345678;
    header.msg_id = static_cast<uint16_t>(mir2::common::MsgId::kLoginReq);
    header.payload_size = 0;

    const auto bytes = header.ToBytes();
    mir2::common::NetworkPacket decoded;
    const auto status = mir2::common::DecodePacket(bytes.data(), bytes.size(), &decoded);
    EXPECT_EQ(status, mir2::common::DecodeStatus::kInvalidMagic);
}

TEST(packet_codec, PayloadTooLargeRejected) {
    mir2::common::PacketHeader header;
    header.msg_id = static_cast<uint16_t>(mir2::common::MsgId::kLoginReq);
    header.payload_size = static_cast<uint32_t>(mir2::common::kMaxPayloadSize + 1);

    const auto bytes = header.ToBytes();
    mir2::common::NetworkPacket decoded;
    const auto status = mir2::common::DecodePacket(bytes.data(), bytes.size(), &decoded);
    EXPECT_EQ(status, mir2::common::DecodeStatus::kPayloadTooLarge);
}

TEST(packet_codec, TruncatedPacketRejected) {
    mir2::common::PacketHeader header;
    header.msg_id = static_cast<uint16_t>(mir2::common::MsgId::kLoginReq);
    header.payload_size = 4;

    const auto bytes = header.ToBytes();
    std::vector<uint8_t> truncated(bytes.begin(), bytes.end());
    truncated.push_back(0xAA);

    mir2::common::NetworkPacket decoded;
    const auto status = mir2::common::DecodePacket(truncated.data(), truncated.size(), &decoded);
    EXPECT_EQ(status, mir2::common::DecodeStatus::kTruncated);
}

TEST(packet_codec, ServerCodecMatchesCommon) {
    const auto payload = BuildPayload(128);
    const auto common_encoded = mir2::common::EncodePacket(
        static_cast<uint16_t>(mir2::common::MsgId::kChatReq),
        payload.data(),
        payload.size());
    const auto server_encoded = mir2::network::PacketCodec::Encode(
        static_cast<uint16_t>(mir2::common::MsgId::kChatReq),
        payload.data(),
        payload.size());

    EXPECT_EQ(common_encoded, server_encoded);
}

TEST(packet_codec, V2EncodeDecodeRoundTrip) {
    const std::vector<mir2::common::MsgId> msg_ids = {
        mir2::common::MsgId::kLoginReq,
        mir2::common::MsgId::kMoveReq,
        mir2::common::MsgId::kChatReq,
        mir2::common::MsgId::kHeartbeat,
        mir2::common::MsgId::kKick
    };

    for (size_t i = 0; i < msg_ids.size(); ++i) {
        const auto msg_id = msg_ids[i];
        const auto payload = BuildPayload(64);
        const uint16_t sequence = static_cast<uint16_t>(100 + i);
        const uint8_t flags = (i % 2 == 0)
                                  ? static_cast<uint8_t>(mir2::common::PacketFlags::kEncrypted)
                                  : static_cast<uint8_t>(mir2::common::PacketFlags::kCompressed);

        const auto encoded = mir2::common::EncodePacketV2(static_cast<uint16_t>(msg_id),
                                                          payload.data(),
                                                          payload.size(),
                                                          sequence,
                                                          flags);
        ASSERT_FALSE(encoded.empty());

        mir2::common::NetworkPacket decoded;
        uint16_t decoded_sequence = 0;
        uint8_t decoded_flags = 0;
        const auto status =
            mir2::common::DecodePacketV2(encoded.data(),
                                         encoded.size(),
                                         &decoded,
                                         &decoded_sequence,
                                         &decoded_flags);
        EXPECT_EQ(status, mir2::common::DecodeStatus::kOk);
        EXPECT_EQ(decoded.msg_id, static_cast<uint16_t>(msg_id));
        EXPECT_EQ(decoded.payload, payload);
        EXPECT_EQ(decoded_sequence, sequence);
        EXPECT_EQ(decoded_flags, flags);
    }
}

TEST(packet_codec, V2EmptyPayloadRoundTrip) {
    const auto encoded = mir2::common::EncodePacketV2(
        static_cast<uint16_t>(mir2::common::MsgId::kHeartbeat), nullptr, 0, 7, 0);
    ASSERT_EQ(encoded.size(), mir2::common::PacketHeaderV2::kSize);

    mir2::common::NetworkPacket decoded;
    uint16_t sequence = 0;
    uint8_t flags = 0;
    const auto status =
        mir2::common::DecodePacketV2(encoded.data(), encoded.size(), &decoded, &sequence, &flags);
    EXPECT_EQ(status, mir2::common::DecodeStatus::kOk);
    EXPECT_EQ(decoded.msg_id, static_cast<uint16_t>(mir2::common::MsgId::kHeartbeat));
    EXPECT_TRUE(decoded.payload.empty());
    EXPECT_EQ(sequence, 7);
    EXPECT_EQ(flags, 0);
}

TEST(packet_codec, V2MaxPayloadRoundTrip) {
    const auto payload = BuildPayload(mir2::common::kMaxPayloadSize);
    const auto encoded = mir2::common::EncodePacketV2(
        static_cast<uint16_t>(mir2::common::MsgId::kMoveReq),
        payload.data(),
        payload.size(),
        42,
        0);
    ASSERT_EQ(encoded.size(), mir2::common::PacketHeaderV2::kSize + payload.size());

    mir2::common::NetworkPacket decoded;
    const auto status = mir2::common::DecodePacketV2(encoded.data(), encoded.size(), &decoded);
    EXPECT_EQ(status, mir2::common::DecodeStatus::kOk);
    EXPECT_EQ(decoded.msg_id, static_cast<uint16_t>(mir2::common::MsgId::kMoveReq));
    EXPECT_EQ(decoded.payload, payload);
}

TEST(packet_codec, V2InvalidMagicRejected) {
    mir2::common::PacketHeaderV2 header;
    header.magic = 0x12345678;
    header.msg_id = static_cast<uint16_t>(mir2::common::MsgId::kLoginReq);
    header.payload_size = 0;

    const auto bytes = header.ToBytes();
    mir2::common::NetworkPacket decoded;
    const auto status = mir2::common::DecodePacketV2(bytes.data(), bytes.size(), &decoded);
    EXPECT_EQ(status, mir2::common::DecodeStatus::kInvalidMagic);
}

TEST(packet_codec, V2InvalidVersionRejected) {
    mir2::common::PacketHeaderV2 header;
    header.version = 0xFF;
    header.msg_id = static_cast<uint16_t>(mir2::common::MsgId::kLoginReq);
    header.payload_size = 0;

    const auto bytes = header.ToBytes();
    mir2::common::NetworkPacket decoded;
    const auto status = mir2::common::DecodePacketV2(bytes.data(), bytes.size(), &decoded);
    EXPECT_EQ(status, mir2::common::DecodeStatus::kInvalidVersion);
}

TEST(packet_codec, V2InvalidChecksumRejected) {
    const auto payload = BuildPayload(16);
    auto encoded = mir2::common::EncodePacketV2(
        static_cast<uint16_t>(mir2::common::MsgId::kChatReq),
        payload.data(),
        payload.size(),
        11,
        0);
    ASSERT_GT(encoded.size(), mir2::common::PacketHeaderV2::kSize);

    encoded[mir2::common::PacketHeaderV2::kSize] ^= 0xFF;

    mir2::common::NetworkPacket decoded;
    const auto status = mir2::common::DecodePacketV2(encoded.data(), encoded.size(), &decoded);
    EXPECT_EQ(status, mir2::common::DecodeStatus::kInvalidChecksum);
}

TEST(packet_codec, V2PayloadTooLargeRejected) {
    mir2::common::PacketHeaderV2 header;
    header.msg_id = static_cast<uint16_t>(mir2::common::MsgId::kLoginReq);
    header.payload_size = static_cast<uint32_t>(mir2::common::kMaxPayloadSize + 1);

    const auto bytes = header.ToBytes();
    mir2::common::NetworkPacket decoded;
    const auto status = mir2::common::DecodePacketV2(bytes.data(), bytes.size(), &decoded);
    EXPECT_EQ(status, mir2::common::DecodeStatus::kPayloadTooLarge);
}

TEST(packet_codec, V2TruncatedPacketRejected) {
    mir2::common::PacketHeaderV2 header;
    header.msg_id = static_cast<uint16_t>(mir2::common::MsgId::kLoginReq);
    header.payload_size = 4;

    const auto bytes = header.ToBytes();
    std::vector<uint8_t> truncated(bytes.begin(), bytes.end());
    truncated.push_back(0xAA);

    mir2::common::NetworkPacket decoded;
    const auto status =
        mir2::common::DecodePacketV2(truncated.data(), truncated.size(), &decoded);
    EXPECT_EQ(status, mir2::common::DecodeStatus::kTruncated);
}

TEST(packet_codec, CRC16KnownVector) {
    const char input[] = "123456789";
    const auto crc =
        mir2::common::CalcCRC16(reinterpret_cast<const uint8_t*>(input), sizeof(input) - 1);
    EXPECT_EQ(crc, 0x29B1);
}

TEST(packet_codec, DetectProtocolVersionTest) {
    uint32_t v1_magic = mir2::common::PacketHeader::kMagic;
    uint32_t v2_magic = mir2::common::PacketHeaderV2::kMagic;
    uint32_t unknown_magic = 0x12345678;

    EXPECT_EQ(mir2::common::DetectProtocolVersion(reinterpret_cast<uint8_t*>(&v1_magic)),
              mir2::common::ProtocolVersion::kV1);
    EXPECT_EQ(mir2::common::DetectProtocolVersion(reinterpret_cast<uint8_t*>(&v2_magic)),
              mir2::common::ProtocolVersion::kV2);
    EXPECT_EQ(mir2::common::DetectProtocolVersion(reinterpret_cast<uint8_t*>(&unknown_magic)),
              mir2::common::ProtocolVersion::kV1);
}

TEST(packet_codec, V2SequencePreserved) {
    const auto payload = BuildPayload(8);
    const uint16_t sequence = 0xBEEF;
    const auto encoded = mir2::common::EncodePacketV2(
        static_cast<uint16_t>(mir2::common::MsgId::kMoveReq),
        payload.data(),
        payload.size(),
        sequence,
        0);
    ASSERT_FALSE(encoded.empty());

    mir2::common::NetworkPacket decoded;
    uint16_t decoded_sequence = 0;
    const auto status = mir2::common::DecodePacketV2(
        encoded.data(), encoded.size(), &decoded, &decoded_sequence, nullptr);
    EXPECT_EQ(status, mir2::common::DecodeStatus::kOk);
    EXPECT_EQ(decoded_sequence, sequence);
}

TEST(packet_codec, V2FlagsPreserved) {
    const auto payload = BuildPayload(8);
    const uint8_t flags =
        static_cast<uint8_t>(mir2::common::PacketFlags::kEncrypted) |
        static_cast<uint8_t>(mir2::common::PacketFlags::kCompressed);
    const auto encoded = mir2::common::EncodePacketV2(
        static_cast<uint16_t>(mir2::common::MsgId::kChatReq),
        payload.data(),
        payload.size(),
        3,
        flags);
    ASSERT_FALSE(encoded.empty());

    mir2::common::NetworkPacket decoded;
    uint8_t decoded_flags = 0;
    const auto status = mir2::common::DecodePacketV2(
        encoded.data(), encoded.size(), &decoded, nullptr, &decoded_flags);
    EXPECT_EQ(status, mir2::common::DecodeStatus::kOk);
    EXPECT_EQ(decoded_flags, flags);
}
