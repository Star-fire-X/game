#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "client/network/dual_channel_client.h"
#include "common/enums.h"
#include "common/network/kcp_config.h"

namespace {

using ::testing::_;
using ::testing::Invoke;
using ::testing::Return;

using mir2::client::ConnectionState;
using mir2::client::DualChannelClient;
using mir2::client::INetworkClient;
using mir2::client::KcpChannel;
using mir2::client::NetworkClient;
using mir2::client::NetworkPacket;
using mir2::common::ChannelType;
using mir2::common::ErrorCode;
using mir2::common::KcpConfig;
using mir2::common::MsgId;

using MessageCallback = INetworkClient::MessageCallback;
using EventCallback = INetworkClient::EventCallback;

class MockNetworkClient : public NetworkClient {
public:
    MOCK_METHOD(bool, connect, (const std::string& host, uint16_t port), (override));
    MOCK_METHOD(void, disconnect, (), (override));
    MOCK_METHOD(bool, is_connected, (), (const, override));
    MOCK_METHOD(void, send, (uint16_t msg_id, const std::vector<uint8_t>& payload), (override));
    MOCK_METHOD(std::optional<NetworkPacket>, receive, (), (override));
    MOCK_METHOD(void, update, (), (override));
    MOCK_METHOD(ConnectionState, get_state, (), (const, override));
    MOCK_METHOD(ErrorCode, get_last_error, (), (const, override));
    MOCK_METHOD(void, set_on_message, (MessageCallback callback), (override));
    MOCK_METHOD(void, set_on_disconnect, (EventCallback callback), (override));
    MOCK_METHOD(void, set_on_connect, (EventCallback callback), (override));

    void CaptureOnMessage(MessageCallback callback) {
        on_message = std::move(callback);
    }

    void CaptureOnDisconnect(EventCallback callback) {
        on_disconnect = std::move(callback);
    }

    void CaptureOnConnect(EventCallback callback) {
        on_connect = std::move(callback);
    }

    MessageCallback on_message;
    EventCallback on_disconnect;
    EventCallback on_connect;
};

class MockKcpChannel : public KcpChannel {
public:
    explicit MockKcpChannel(asio::io_context& io_context,
                            mir2::common::KcpConfig config = {})
        : KcpChannel(io_context, config) {
    }

    MOCK_METHOD(bool, Connect, (const std::string& host, uint16_t port), (override));
    MOCK_METHOD(void, Disconnect, (), (override));
    MOCK_METHOD(bool, IsConnected, (), (const, override));
    MOCK_METHOD(void, Send, (uint16_t msg_id, const std::vector<uint8_t>& payload), (override));
    MOCK_METHOD(void, Update, (), (override));
    MOCK_METHOD(ChannelType, Type, (), (const, override));
};

class MockMessageSink {
public:
    MOCK_METHOD(void, OnMessage, (const NetworkPacket& packet), ());
};

void PrepareTcpMock(MockNetworkClient* tcp_client) {
    ON_CALL(*tcp_client, set_on_message(_))
        .WillByDefault(Invoke(tcp_client, &MockNetworkClient::CaptureOnMessage));
    ON_CALL(*tcp_client, set_on_disconnect(_))
        .WillByDefault(Invoke(tcp_client, &MockNetworkClient::CaptureOnDisconnect));
    ON_CALL(*tcp_client, set_on_connect(_))
        .WillByDefault(Invoke(tcp_client, &MockNetworkClient::CaptureOnConnect));
}

NetworkPacket MakePacket(uint16_t msg_id, std::vector<uint8_t> payload = {1}) {
    NetworkPacket packet;
    packet.msg_id = msg_id;
    packet.payload = std::move(payload);
    return packet;
}

} // namespace

TEST(dual_channel_client, SendRoutesToKcpWhenAvailable) {
    auto tcp_client = std::make_unique<::testing::NiceMock<MockNetworkClient>>();
    auto* tcp_ptr = tcp_client.get();
    PrepareTcpMock(tcp_ptr);

    asio::io_context io_context;
    auto kcp_channel = std::make_unique<::testing::NiceMock<MockKcpChannel>>(io_context);
    auto* kcp_ptr = kcp_channel.get();
    ON_CALL(*kcp_ptr, IsConnected()).WillByDefault(Return(true));

    DualChannelClient client(std::move(tcp_client), std::move(kcp_channel));

    client.mark_kcp_confirmed();
    const uint16_t msg_id = 7777;
    const std::vector<uint8_t> payload = {9, 8, 7};
    client.set_route(msg_id, ChannelType::kKcp);

    EXPECT_CALL(*kcp_ptr, Send(msg_id, payload)).Times(1);
    EXPECT_CALL(*tcp_ptr, send(_, _)).Times(0);

    client.send(msg_id, payload);
}

TEST(dual_channel_client, SendFallsBackToTcpWhenKcpUnavailable) {
    auto tcp_client = std::make_unique<::testing::NiceMock<MockNetworkClient>>();
    auto* tcp_ptr = tcp_client.get();
    PrepareTcpMock(tcp_ptr);

    asio::io_context io_context;
    auto kcp_channel = std::make_unique<::testing::NiceMock<MockKcpChannel>>(io_context);
    auto* kcp_ptr = kcp_channel.get();
    ON_CALL(*kcp_ptr, IsConnected()).WillByDefault(Return(true));

    DualChannelClient client(std::move(tcp_client), std::move(kcp_channel));

    client.mark_kcp_failed();
    const uint16_t msg_id = 8888;
    const std::vector<uint8_t> payload = {1, 2};
    client.set_route(msg_id, ChannelType::kKcp);

    EXPECT_CALL(*tcp_ptr, send(msg_id, payload)).Times(1);
    EXPECT_CALL(*kcp_ptr, Send(_, _)).Times(0);

    client.send(msg_id, payload);
}

TEST(dual_channel_client, PollingModeKeepsQueueUntilReceive) {
    auto tcp_client = std::make_unique<::testing::NiceMock<MockNetworkClient>>();
    auto* tcp_ptr = tcp_client.get();
    PrepareTcpMock(tcp_ptr);

    asio::io_context io_context;
    auto kcp_channel = std::make_unique<::testing::NiceMock<MockKcpChannel>>(io_context);

    DualChannelClient client(std::move(tcp_client), std::move(kcp_channel));

    const uint16_t msg_id = static_cast<uint16_t>(MsgId::kChatRsp);
    const std::vector<uint8_t> payload = {3, 4, 5};
    const NetworkPacket packet{msg_id, payload};

    ASSERT_TRUE(static_cast<bool>(tcp_ptr->on_message));
    tcp_ptr->on_message(packet);

    client.update();

    const auto received = client.receive();
    ASSERT_TRUE(received.has_value());
    EXPECT_EQ(received->msg_id, msg_id);
    EXPECT_EQ(received->payload, payload);
    EXPECT_FALSE(client.receive().has_value());
}

TEST(dual_channel_client, CallbackModeDrainsQueue) {
    auto tcp_client = std::make_unique<::testing::NiceMock<MockNetworkClient>>();
    auto* tcp_ptr = tcp_client.get();
    PrepareTcpMock(tcp_ptr);

    asio::io_context io_context;
    auto kcp_channel = std::make_unique<::testing::NiceMock<MockKcpChannel>>(io_context);

    DualChannelClient client(std::move(tcp_client), std::move(kcp_channel));

    ::testing::StrictMock<MockMessageSink> sink;
    client.set_on_message([&sink](const NetworkPacket& packet) { sink.OnMessage(packet); });

    const uint16_t msg_id = static_cast<uint16_t>(MsgId::kChatRsp);
    const NetworkPacket packet = MakePacket(msg_id, {7});

    EXPECT_CALL(sink, OnMessage(::testing::Field(&NetworkPacket::msg_id, msg_id))).Times(1);

    ASSERT_TRUE(static_cast<bool>(tcp_ptr->on_message));
    tcp_ptr->on_message(packet);

    client.update();

    EXPECT_FALSE(client.receive().has_value());
}

TEST(dual_channel_client, ReceiveQueueDropsPacketsOverCapacity) {
    auto tcp_client = std::make_unique<::testing::NiceMock<MockNetworkClient>>();
    auto* tcp_ptr = tcp_client.get();
    PrepareTcpMock(tcp_ptr);

    asio::io_context io_context;
    auto kcp_channel = std::make_unique<::testing::NiceMock<MockKcpChannel>>(io_context);

    DualChannelClient client(std::move(tcp_client), std::move(kcp_channel));

    ASSERT_TRUE(static_cast<bool>(tcp_ptr->on_message));

    const uint16_t base_msg = 6000;
    for (int i = 0; i < 1001; ++i) {
        tcp_ptr->on_message(MakePacket(static_cast<uint16_t>(base_msg + i)));
    }

    int received_count = 0;
    while (client.receive().has_value()) {
        ++received_count;
    }

    EXPECT_EQ(received_count, 1000);
}

TEST(dual_channel_client, KcpUpgradeStateMachineProgresses) {
    auto tcp_client = std::make_unique<::testing::NiceMock<MockNetworkClient>>();
    auto* tcp_ptr = tcp_client.get();
    PrepareTcpMock(tcp_ptr);

    asio::io_context io_context;
    auto kcp_channel = std::make_unique<::testing::NiceMock<MockKcpChannel>>(io_context);

    DualChannelClient client(std::move(tcp_client), std::move(kcp_channel));

    EXPECT_EQ(client.get_kcp_state(), DualChannelClient::KcpUpgradeState::kIdle);

    client.set_kcp_session(42, std::string("token"), 3000);
    EXPECT_EQ(client.get_kcp_state(), DualChannelClient::KcpUpgradeState::kRequested);

    client.mark_kcp_confirmed();
    EXPECT_EQ(client.get_kcp_state(), DualChannelClient::KcpUpgradeState::kConfirmed);
}

TEST(dual_channel_client, RecoveryRequestsUpgradeAfterFallbackBackoff) {
    KcpConfig config;
    config.timeout_ms = 1000;
    config.recovery_interval_ms = 0;

    auto tcp_client = std::make_unique<::testing::NiceMock<MockNetworkClient>>();
    auto* tcp_ptr = tcp_client.get();
    PrepareTcpMock(tcp_ptr);
    ON_CALL(*tcp_ptr, is_connected()).WillByDefault(Return(true));

    asio::io_context io_context;
    auto kcp_channel = std::make_unique<::testing::NiceMock<MockKcpChannel>>(io_context, config);
    auto* kcp_ptr = kcp_channel.get();
    ON_CALL(*kcp_ptr, IsConnected()).WillByDefault(Return(false));

    DualChannelClient client(std::move(tcp_client), std::move(kcp_channel), config);

    const uint16_t upgrade_msg = static_cast<uint16_t>(MsgId::kKcpUpgradeRequest);
    EXPECT_CALL(*tcp_ptr, send(upgrade_msg, _)).Times(2);

    client.update();

    client.mark_kcp_failed();
    EXPECT_EQ(client.get_kcp_state(), DualChannelClient::KcpUpgradeState::kFailed);

    client.update();
    EXPECT_EQ(client.get_kcp_state(), DualChannelClient::KcpUpgradeState::kIdle);

    client.update();
}
