#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>
#include <string>
#include <unordered_set>
#include <vector>

#include <flatbuffers/flatbuffers.h>

#include "chat_generated.h"
#include "common/enums.h"
#include "common/network/i_channel.h"
#include "common/protocol/packet_codec.h"
#include "game_generated.h"
#include "login_generated.h"
#include "network/packet_codec.h"
#include "system_generated.h"

namespace {

std::vector<uint8_t> BuildValidPayload(mir2::common::MsgId msg_id,
                                       size_t content_size = 0,
                                       bool compressible = true) {
    flatbuffers::FlatBufferBuilder builder;
    switch (msg_id) {
        case mir2::common::MsgId::kLoginReq: {
            const auto user = builder.CreateString("user");
            const auto pass = builder.CreateString("pass");
            const auto version = builder.CreateString("1");
            const auto req = mir2::proto::CreateLoginReq(builder, user, pass, version);
            builder.Finish(req);
            break;
        }
        case mir2::common::MsgId::kMoveReq: {
            const auto req = mir2::proto::CreateMoveReq(builder, 10, 20);
            builder.Finish(req);
            break;
        }
        case mir2::common::MsgId::kChatReq: {
            const size_t size = content_size > 0 ? content_size : 8;
            std::string content(size, 'a');
            if (!compressible) {
                for (size_t i = 0; i < content.size(); ++i) {
                    content[i] = static_cast<char>(0x20 + (i * 31) % 95);
                }
            }
            const auto content_offset = builder.CreateString(content);
            const auto req = mir2::proto::CreateChatReq(
                builder,
                mir2::proto::ChatChannel::WORLD,
                content_offset,
                0);
            builder.Finish(req);
            break;
        }
        case mir2::common::MsgId::kSystemMsg: {
            const size_t size = content_size > 0 ? content_size : 8;
            std::string content(size, 'a');
            if (!compressible) {
                for (size_t i = 0; i < content.size(); ++i) {
                    content[i] = static_cast<char>(0x20 + (i * 17) % 95);
                }
            }
            const auto from_name = builder.CreateString("System");
            const auto content_offset = builder.CreateString(content);
            const auto msg = mir2::proto::CreateChatMessage(
                builder,
                mir2::proto::ChatChannel::SYSTEM,
                0,
                from_name,
                0,
                content_offset,
                0,
                0);
            builder.Finish(msg);
            break;
        }
        case mir2::common::MsgId::kHeartbeat: {
            const auto hb = mir2::proto::CreateHeartbeat(builder, 1, 2);
            builder.Finish(hb);
            break;
        }
        case mir2::common::MsgId::kKick: {
            const auto message = builder.CreateString("kick");
            const auto reason_text = builder.CreateString("kick");
            const auto kick = mir2::proto::CreateKick(
                builder,
                mir2::proto::ErrorCode::ERR_KICK_ADMIN_MANUAL,
                message,
                reason_text);
            builder.Finish(kick);
            break;
        }
        default:
            break;
    }

    const uint8_t* data = builder.GetBufferPointer();
    return std::vector<uint8_t>(data, data + builder.GetSize());
}

std::filesystem::path FindRepoRoot() {
    const auto has_targets = [](const std::filesystem::path& path) {
        std::error_code ec;
        return std::filesystem::exists(path / "src/common/enums.h", ec) &&
               std::filesystem::exists(path / "src/common/protocol/packet_codec.cpp", ec);
    };

    const auto climb = [&](std::filesystem::path start) -> std::filesystem::path {
        std::error_code ec;
        if (start.empty()) {
            return {};
        }
        if (!start.is_absolute()) {
            start = std::filesystem::current_path(ec) / start;
        }
        if (std::filesystem::is_regular_file(start, ec)) {
            start = start.parent_path();
        }
        for (size_t i = 0; i < 24 && !start.empty(); ++i) {
            if (has_targets(start)) {
                return start;
            }
            if (!start.has_parent_path() || start == start.parent_path()) {
                break;
            }
            start = start.parent_path();
        }
        return {};
    };

    if (auto from_cwd = climb(std::filesystem::current_path()); !from_cwd.empty()) {
        return from_cwd;
    }
    if (auto from_file = climb(std::filesystem::path(__FILE__)); !from_file.empty()) {
        return from_file;
    }
    return {};
}

std::string ReadTextFile(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    std::ostringstream output;
    output << input.rdbuf();
    return output.str();
}

std::string ExtractBlock(const std::string& source, const std::string& marker) {
    const size_t start = source.find(marker);
    if (start == std::string::npos) {
        return {};
    }

    const size_t open_brace = source.find('{', start);
    if (open_brace == std::string::npos) {
        return {};
    }

    int depth = 0;
    for (size_t i = open_brace; i < source.size(); ++i) {
        if (source[i] == '{') {
            ++depth;
        } else if (source[i] == '}') {
            --depth;
            if (depth == 0) {
                return source.substr(open_brace + 1, i - open_brace - 1);
            }
        }
    }

    return {};
}

std::unordered_set<std::string> ExtractEnumMsgIdNames(const std::string& enums_text) {
    std::unordered_set<std::string> names;
    const auto enum_block = ExtractBlock(enums_text, "enum class MsgId");
    if (enum_block.empty()) {
        return names;
    }

    static const std::regex enum_re(R"(\b(k[A-Za-z0-9_]+)\s*=)");
    for (std::sregex_iterator it(enum_block.begin(), enum_block.end(), enum_re), end;
         it != end;
         ++it) {
        names.insert((*it)[1].str());
    }
    return names;
}

std::unordered_set<std::string> ExtractMsgIdCaseNames(const std::string& block) {
    std::unordered_set<std::string> names;
    if (block.empty()) {
        return names;
    }

    static const std::regex case_re(
        R"(case\s+static_cast<uint16_t>\(MsgId::(k[A-Za-z0-9_]+)\)\s*:)");
    for (std::sregex_iterator it(block.begin(), block.end(), case_re), end;
         it != end;
         ++it) {
        names.insert((*it)[1].str());
    }
    return names;
}

std::string JoinSorted(const std::unordered_set<std::string>& values) {
    std::vector<std::string> sorted(values.begin(), values.end());
    std::sort(sorted.begin(), sorted.end());
    std::ostringstream out;
    for (size_t i = 0; i < sorted.size(); ++i) {
        if (i > 0) {
            out << ", ";
        }
        out << sorted[i];
    }
    return out.str();
}

}  // namespace

TEST(packet_codec, ServerCodecMatchesCommon) {
    const auto payload = BuildValidPayload(mir2::common::MsgId::kChatReq);
    const auto common_encoded = mir2::common::EncodePacketV2(
        static_cast<uint16_t>(mir2::common::MsgId::kChatReq),
        payload.data(),
        payload.size(),
        /*sequence=*/0,
        /*flags=*/0);
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
        const auto payload = BuildValidPayload(msg_id);
        const uint16_t sequence = static_cast<uint16_t>(100 + i);
        const uint8_t flags = (i % 2 == 0)
                                  ? mir2::common::PacketHeaderV2::kFlagChannelKcp
                                  : 0;

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

TEST(packet_codec, V2BackpressureControlRoundTrip) {
    flatbuffers::FlatBufferBuilder builder;
    const auto control = mir2::proto::CreateBackpressureControl(
        builder,
        12345,
        mir2::proto::BackpressureAction::PAUSE_READ,
        100);
    builder.Finish(control);

    const uint8_t* payload_data = builder.GetBufferPointer();
    const size_t payload_size = builder.GetSize();
    const auto encoded = mir2::common::EncodePacketV2(
        static_cast<uint16_t>(mir2::common::InternalMsgId::kBackpressureControl),
        payload_data,
        payload_size,
        18,
        0);
    ASSERT_FALSE(encoded.empty());

    mir2::common::NetworkPacket decoded;
    uint16_t sequence = 0;
    const auto status =
        mir2::common::DecodePacketV2(encoded.data(), encoded.size(), &decoded, &sequence, nullptr);
    EXPECT_EQ(status, mir2::common::DecodeStatus::kOk);
    EXPECT_EQ(decoded.msg_id,
              static_cast<uint16_t>(mir2::common::InternalMsgId::kBackpressureControl));
    EXPECT_EQ(decoded.payload.size(), payload_size);
    EXPECT_EQ(decoded.payload,
              std::vector<uint8_t>(payload_data, payload_data + payload_size));
    EXPECT_EQ(sequence, 18);
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
    size_t content_size = mir2::common::kMaxPayloadSize - 256;
    auto payload = BuildValidPayload(mir2::common::MsgId::kSystemMsg,
                                     content_size,
                                     false);
    while (payload.size() > mir2::common::kMaxPayloadSize && content_size > 256) {
        content_size -= 128;
        payload = BuildValidPayload(mir2::common::MsgId::kSystemMsg,
                                    content_size,
                                    false);
    }
    ASSERT_LE(payload.size(), mir2::common::kMaxPayloadSize);
    const auto encoded = mir2::common::EncodePacketV2(
        static_cast<uint16_t>(mir2::common::MsgId::kSystemMsg),
        payload.data(),
        payload.size(),
        42,
        0);

    mir2::common::NetworkPacket decoded;
    const auto status = mir2::common::DecodePacketV2(encoded.data(), encoded.size(), &decoded);
    EXPECT_EQ(status, mir2::common::DecodeStatus::kOk);
    EXPECT_EQ(decoded.msg_id, static_cast<uint16_t>(mir2::common::MsgId::kSystemMsg));
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
    const auto payload = BuildValidPayload(mir2::common::MsgId::kChatReq);
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
    uint32_t v1_magic = 0x4D495232;  // Legacy "MIR2"
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
    const auto payload = BuildValidPayload(mir2::common::MsgId::kMoveReq);
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
    auto payload = BuildValidPayload(mir2::common::MsgId::kChatReq, 4096, true);
    const uint8_t flags = mir2::common::PacketHeaderV2::kFlagChannelKcp;
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
    EXPECT_EQ(decoded.payload, payload);
    const uint8_t expected_flags = static_cast<uint8_t>(
        mir2::common::PacketHeaderV2::kFlagCompressed |
        mir2::common::PacketHeaderV2::kFlagChannelKcp);
    EXPECT_EQ(decoded_flags, expected_flags);
}

TEST(packet_codec, ValidateChannelFlagTcpRejectsKcpFlag) {
    const uint8_t kcp_flag = mir2::common::PacketHeaderV2::kFlagChannelKcp;
    const uint8_t compressed_flag = mir2::common::PacketHeaderV2::kFlagCompressed;

    EXPECT_FALSE(mir2::common::ValidateChannelFlag(
        kcp_flag, mir2::common::ChannelType::kTcp));
    EXPECT_TRUE(mir2::common::ValidateChannelFlag(
        0, mir2::common::ChannelType::kTcp));
    EXPECT_TRUE(mir2::common::ValidateChannelFlag(
        compressed_flag, mir2::common::ChannelType::kTcp));
}

TEST(packet_codec, ValidateChannelFlagKcpRequiresFlag) {
    const uint8_t kcp_flag = mir2::common::PacketHeaderV2::kFlagChannelKcp;
    const uint8_t compressed_flag = mir2::common::PacketHeaderV2::kFlagCompressed;

    EXPECT_TRUE(mir2::common::ValidateChannelFlag(
        kcp_flag, mir2::common::ChannelType::kKcp));
    EXPECT_TRUE(mir2::common::ValidateChannelFlag(
        static_cast<uint8_t>(kcp_flag | compressed_flag),
        mir2::common::ChannelType::kKcp));
    EXPECT_FALSE(mir2::common::ValidateChannelFlag(
        0, mir2::common::ChannelType::kKcp));
}

TEST(packet_codec, MsgIdValidationCoverageMatchesEnum) {
    const auto repo_root = FindRepoRoot();
    ASSERT_FALSE(repo_root.empty()) << "Failed to locate repository root";

    const auto enums_text = ReadTextFile(repo_root / "src/common/enums.h");
    const auto codec_text = ReadTextFile(repo_root / "src/common/protocol/packet_codec.cpp");
    ASSERT_FALSE(enums_text.empty()) << "Failed to read src/common/enums.h";
    ASSERT_FALSE(codec_text.empty()) << "Failed to read src/common/protocol/packet_codec.cpp";

    auto enum_names = ExtractEnumMsgIdNames(enums_text);
    ASSERT_FALSE(enum_names.empty()) << "Failed to parse MsgId enum";
    enum_names.erase("kNone");

    const auto verify_block = ExtractBlock(codec_text, "bool VerifyFlatBufferPayload");
    const auto npc_block = ExtractBlock(codec_text, "bool IsNpcMessage");
    ASSERT_FALSE(verify_block.empty()) << "Failed to parse VerifyFlatBufferPayload";
    ASSERT_FALSE(npc_block.empty()) << "Failed to parse IsNpcMessage";

    auto covered_names = ExtractMsgIdCaseNames(verify_block);
    const auto npc_names = ExtractMsgIdCaseNames(npc_block);
    covered_names.insert(npc_names.begin(), npc_names.end());

    std::unordered_set<std::string> missing;
    for (const auto& name : enum_names) {
        if (covered_names.count(name) == 0) {
            missing.insert(name);
        }
    }

    std::unordered_set<std::string> stale;
    for (const auto& name : covered_names) {
        if (enum_names.count(name) == 0) {
            stale.insert(name);
        }
    }

    EXPECT_TRUE(missing.empty())
        << "MsgId values missing protocol validation coverage: " << JoinSorted(missing);
    EXPECT_TRUE(stale.empty())
        << "Protocol validation references unknown MsgId values: " << JoinSorted(stale);
}
