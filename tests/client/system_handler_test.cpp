#include <gtest/gtest.h>

#include <flatbuffers/flatbuffers.h>

#include <string>
#include <vector>

#include "client/handlers/system_handler.h"
#include "system_generated.h"

namespace {

using mir2::common::MsgId;
using mir2::common::NetworkPacket;
using mir2::game::handlers::SystemHandler;

NetworkPacket MakePacket(MsgId msg_id, std::vector<uint8_t> payload) {
    NetworkPacket packet;
    packet.msg_id = static_cast<uint16_t>(msg_id);
    packet.payload = std::move(payload);
    return packet;
}

std::vector<uint8_t> BuildPayload(flatbuffers::FlatBufferBuilder& builder) {
    const uint8_t* data = builder.GetBufferPointer();
    return std::vector<uint8_t>(data, data + builder.GetSize());
}

} // namespace

TEST(handlers_system, HeartbeatResponseTriggersCallback) {
    int called = 0;
    uint32_t captured_seq = 0;
    uint32_t captured_time = 0;

    SystemHandler::Callbacks callbacks;
    callbacks.on_heartbeat_response = [&](uint32_t seq, uint32_t server_time) {
        captured_seq = seq;
        captured_time = server_time;
        ++called;
    };

    SystemHandler handler(callbacks);

    flatbuffers::FlatBufferBuilder builder;
    const auto rsp = mir2::proto::CreateHeartbeatRsp(builder, 99u, 123u);
    builder.Finish(rsp);

    handler.HandleHeartbeatResponse(MakePacket(MsgId::kHeartbeatRsp, BuildPayload(builder)));

    EXPECT_EQ(called, 1);
    EXPECT_EQ(captured_seq, 99u);
    EXPECT_EQ(captured_time, 123u);
}

TEST(handlers_system, ServerNoticeTriggersCallback) {
    int called = 0;
    uint16_t captured_level = 0;
    uint32_t captured_timestamp = 0;
    std::string captured_message;

    SystemHandler::Callbacks callbacks;
    callbacks.on_server_notice = [&](uint16_t level, const std::string& message, uint32_t timestamp) {
        captured_level = level;
        captured_message = message;
        captured_timestamp = timestamp;
        ++called;
    };

    SystemHandler handler(callbacks);

    flatbuffers::FlatBufferBuilder builder;
    const auto message = builder.CreateString("Maintenance soon");
    const auto notice = mir2::proto::CreateServerNotice(builder, 2, message, 456u);
    builder.Finish(notice);

    handler.HandleServerNotice(MakePacket(MsgId::kServerNotice, BuildPayload(builder)));

    EXPECT_EQ(called, 1);
    EXPECT_EQ(captured_level, 2);
    EXPECT_EQ(captured_message, "Maintenance soon");
    EXPECT_EQ(captured_timestamp, 456u);
}

TEST(handlers_system, KickTriggersCallback) {
    int called = 0;
    mir2::proto::ErrorCode captured_reason = mir2::proto::ErrorCode::ERR_UNKNOWN;
    std::string captured_reason_text;
    std::string captured_message;

    SystemHandler::Callbacks callbacks;
    callbacks.on_kick = [&](mir2::proto::ErrorCode reason,
                            const std::string& reason_text,
                            const std::string& message) {
        captured_reason = reason;
        captured_reason_text = reason_text;
        captured_message = message;
        ++called;
    };

    SystemHandler handler(callbacks);

    flatbuffers::FlatBufferBuilder builder;
    const auto message = builder.CreateString("Duplicate login");
    const auto kick = mir2::proto::CreateKick(
        builder, mir2::proto::ErrorCode::ERR_PASSWORD_WRONG, message, 0);
    builder.Finish(kick);

    handler.HandleKick(MakePacket(MsgId::kKick, BuildPayload(builder)));

    EXPECT_EQ(called, 1);
    EXPECT_EQ(captured_reason, mir2::proto::ErrorCode::ERR_PASSWORD_WRONG);
    EXPECT_EQ(captured_reason_text, "Password incorrect");
    EXPECT_EQ(captured_message, "Duplicate login");
}
