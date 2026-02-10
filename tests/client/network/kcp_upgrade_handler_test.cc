#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <flatbuffers/flatbuffers.h>

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "client/network/kcp_upgrade_handler.h"
#include "common/enums.h"
#include "system_generated.h"

namespace {

using mir2::client::KcpUpgradeHandler;
using mir2::common::MsgId;
using mir2::common::NetworkPacket;
using ::testing::_;
using ::testing::Invoke;
using ::testing::StrictMock;

struct SentPacket {
    uint16_t msg_id = 0;
    std::vector<uint8_t> payload;
};

class MockSender {
public:
    MOCK_METHOD(void, Send, (uint16_t, const std::vector<uint8_t>&), ());
};

class MockSession {
public:
    MOCK_METHOD(void, OnSession, (uint32_t, const std::string&, uint16_t), ());
};

class MockEvents {
public:
    MOCK_METHOD(void, OnConfirmed, (), ());
    MOCK_METHOD(void, OnFailed, (), ());
    MOCK_METHOD(void, OnDisconnect, (), ());
};

NetworkPacket MakePacket(MsgId msg_id, std::vector<uint8_t> payload = {}) {
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

TEST(kcp_upgrade_handler, SendsUpgradeRequestOnTcpConnect) {
    KcpUpgradeHandler handler;

    StrictMock<MockSender> tcp_sender;
    std::vector<SentPacket> sent;

    handler.SetTcpSender([&](uint16_t msg_id, const std::vector<uint8_t>& payload) {
        tcp_sender.Send(msg_id, payload);
    });

    EXPECT_CALL(tcp_sender, Send(static_cast<uint16_t>(MsgId::kKcpUpgradeRequest), _))
        .WillOnce(Invoke([&](uint16_t msg_id, const std::vector<uint8_t>& payload) {
            sent.push_back({msg_id, payload});
        }));

    handler.OnTcpConnected();

    ASSERT_EQ(sent.size(), 1u);
    flatbuffers::Verifier verifier(sent[0].payload.data(), sent[0].payload.size());
    EXPECT_TRUE(verifier.VerifyBuffer<mir2::proto::KcpUpgradeRequest>(nullptr));
    const auto* request =
        flatbuffers::GetRoot<mir2::proto::KcpUpgradeRequest>(sent[0].payload.data());
    ASSERT_NE(request, nullptr);
    EXPECT_EQ(request->client_udp_port(), 0);
}

TEST(kcp_upgrade_handler, HandlesUpgradeResponseAndInvokesSessionCallback) {
    KcpUpgradeHandler handler;
    StrictMock<MockSession> session;

    handler.SetSessionCallback(
        [&](uint32_t conv_id, const std::string& token, uint16_t udp_port) {
            session.OnSession(conv_id, token, udp_port);
        });

    flatbuffers::FlatBufferBuilder builder;
    auto token = builder.CreateString("token");
    const auto response =
        mir2::proto::CreateKcpUpgradeResponse(builder, true, 42u, 7001u, token, 0);
    builder.Finish(response);

    EXPECT_CALL(session, OnSession(42u, "token", 7001u)).Times(1);
    EXPECT_TRUE(handler.HandleTcpPacket(
        MakePacket(MsgId::kKcpUpgradeResponse, BuildPayload(builder))));
}

TEST(kcp_upgrade_handler, UpgradeResponseFailureInvokesFailedCallback) {
    KcpUpgradeHandler handler;
    StrictMock<MockEvents> events;

    handler.SetFailedCallback([&]() { events.OnFailed(); });

    flatbuffers::FlatBufferBuilder builder;
    auto token = builder.CreateString("bad");
    const auto response =
        mir2::proto::CreateKcpUpgradeResponse(builder, false, 1u, 2u, token, 5u);
    builder.Finish(response);

    EXPECT_CALL(events, OnFailed()).Times(1);
    EXPECT_TRUE(handler.HandleTcpPacket(
        MakePacket(MsgId::kKcpUpgradeResponse, BuildPayload(builder))));
}

TEST(kcp_upgrade_handler, SendsHeartbeatOnInterval) {
    KcpUpgradeHandler handler;
    StrictMock<MockSender> kcp_sender;
    std::vector<SentPacket> sent;

    handler.SetKcpSender([&](uint16_t msg_id, const std::vector<uint8_t>& payload) {
        kcp_sender.Send(msg_id, payload);
    });

    EXPECT_CALL(kcp_sender, Send(static_cast<uint16_t>(MsgId::kKcpHeartbeat), _))
        .Times(2)
        .WillRepeatedly(Invoke([&](uint16_t msg_id, const std::vector<uint8_t>& payload) {
            sent.push_back({msg_id, payload});
        }));

    handler.Update(1000, false, true, false);
    handler.Update(4000, false, true, false);
    handler.Update(6000, false, true, false);

    ASSERT_EQ(sent.size(), 2u);

    const uint32_t expected_timestamps[] = {1000u, 6000u};
    for (size_t i = 0; i < sent.size(); ++i) {
        flatbuffers::Verifier verifier(sent[i].payload.data(), sent[i].payload.size());
        EXPECT_TRUE(verifier.VerifyBuffer<mir2::proto::KcpHeartbeat>(nullptr));
        const auto* heartbeat =
            flatbuffers::GetRoot<mir2::proto::KcpHeartbeat>(sent[i].payload.data());
        ASSERT_NE(heartbeat, nullptr);
        EXPECT_EQ(heartbeat->timestamp(), expected_timestamps[i]);
    }
}

TEST(kcp_upgrade_handler, HeartbeatAckConfirmsOnce) {
    KcpUpgradeHandler handler;
    StrictMock<MockEvents> events;

    handler.SetConfirmedCallback([&]() { events.OnConfirmed(); });

    flatbuffers::FlatBufferBuilder builder;
    const auto ack = mir2::proto::CreateKcpHeartbeatAck(builder, 123u);
    builder.Finish(ack);
    const NetworkPacket packet =
        MakePacket(MsgId::kKcpHeartbeatAck, BuildPayload(builder));

    EXPECT_CALL(events, OnConfirmed()).Times(1);
    EXPECT_TRUE(handler.HandleKcpPacket(packet));
    EXPECT_TRUE(handler.HandleKcpPacket(packet));
}

TEST(kcp_upgrade_handler, DisconnectsAfterHeartbeatTimeoutWithoutAck) {
    KcpUpgradeHandler handler;
    StrictMock<MockSender> kcp_sender;
    StrictMock<MockEvents> events;

    handler.SetKcpSender([&](uint16_t msg_id, const std::vector<uint8_t>& payload) {
        kcp_sender.Send(msg_id, payload);
    });
    handler.SetConfirmedCallback([&]() { events.OnConfirmed(); });
    handler.SetDisconnectCallback([&]() { events.OnDisconnect(); });

    EXPECT_CALL(events, OnConfirmed()).Times(0);
    EXPECT_CALL(events, OnDisconnect()).Times(1);
    EXPECT_CALL(kcp_sender, Send(static_cast<uint16_t>(MsgId::kKcpHeartbeat), _))
        .Times(1);

    handler.Update(1000, false, true, true);
    handler.Update(16000, false, false, true);
}
