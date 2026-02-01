#include <gtest/gtest.h>

#include <flatbuffers/flatbuffers.h>

#include <atomic>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "client/network/network_manager.h"
#include "common/enums.h"
#include "login_generated.h"
#include "system_generated.h"

namespace {

using legend2::ConnectionState;
using mir2::common::ErrorCode;
using legend2::INetworkClient;
using legend2::NetworkManager;
using mir2::common::MsgId;
using mir2::common::NetworkPacket;

struct SentMessage {
    uint16_t msg_id = 0;
    std::vector<uint8_t> payload;
};

class FakeNetworkClient : public INetworkClient {
public:
    bool connect(const std::string& host, uint16_t port) override {
        host_ = host;
        port_ = port;
        ++connect_calls_;
        connected_ = connect_result_;
        if (connect_result_ && on_connect_) {
            on_connect_();
        }
        return connect_result_;
    }

    void disconnect() override {
        ++disconnect_calls_;
        connected_ = false;
    }

    bool is_connected() const override {
        return connected_;
    }

    void send(uint16_t msg_id, const std::vector<uint8_t>& payload) override {
        sent_messages_.push_back({msg_id, payload});
    }

    std::optional<NetworkPacket> receive() override {
        return std::nullopt;
    }

    void update() override {
        ++update_calls_;
    }

    ConnectionState get_state() const override {
        return connected_ ? ConnectionState::CONNECTED : ConnectionState::DISCONNECTED;
    }

    ErrorCode get_last_error() const override {
        return last_error_;
    }

    void set_on_message(MessageCallback callback) override {
        on_message_ = std::move(callback);
    }

    void set_on_disconnect(EventCallback callback) override {
        on_disconnect_ = std::move(callback);
    }

    void set_on_connect(EventCallback callback) override {
        on_connect_ = std::move(callback);
    }

    void EmitPacket(const NetworkPacket& packet) {
        if (on_message_) {
            on_message_(packet);
        }
    }

    void EmitDisconnect() {
        connected_ = false;
        if (on_disconnect_) {
            on_disconnect_();
        }
    }

    void set_last_error(ErrorCode code) {
        last_error_ = code;
    }

    void set_connect_result(bool result) {
        connect_result_ = result;
    }

    int connect_calls() const {
        return connect_calls_;
    }

    int disconnect_calls() const {
        return disconnect_calls_;
    }

    int update_calls() const {
        return update_calls_;
    }

    const std::vector<SentMessage>& sent_messages() const {
        return sent_messages_;
    }

    const std::string& host() const {
        return host_;
    }

    uint16_t port() const {
        return port_;
    }

private:
    bool connected_ = false;
    bool connect_result_ = true;
    int connect_calls_ = 0;
    int disconnect_calls_ = 0;
    int update_calls_ = 0;
    std::string host_;
    uint16_t port_ = 0;
    ErrorCode last_error_{ErrorCode::SUCCESS};
    std::vector<SentMessage> sent_messages_;
    MessageCallback on_message_;
    EventCallback on_disconnect_;
    EventCallback on_connect_;
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

TEST(network_manager, ConnectDisconnectLifecycle) {
    auto fake = std::make_unique<FakeNetworkClient>();
    FakeNetworkClient* fake_ptr = fake.get();
    NetworkManager manager(std::move(fake));

    int connect_notifications = 0;
    int disconnect_notifications = 0;
    manager.set_on_connect([&connect_notifications]() { ++connect_notifications; });
    manager.set_on_disconnect([&disconnect_notifications]() { ++disconnect_notifications; });

    EXPECT_TRUE(manager.connect("127.0.0.1", 7000));
    EXPECT_EQ(fake_ptr->connect_calls(), 1);
    EXPECT_EQ(fake_ptr->host(), "127.0.0.1");
    EXPECT_EQ(fake_ptr->port(), 7000);
    EXPECT_TRUE(manager.is_connected());
    EXPECT_EQ(manager.get_state(), ConnectionState::CONNECTED);
    EXPECT_EQ(connect_notifications, 1);

    manager.disconnect();
    EXPECT_EQ(fake_ptr->disconnect_calls(), 1);
    EXPECT_FALSE(manager.is_connected());
    EXPECT_EQ(manager.get_state(), ConnectionState::DISCONNECTED);
    EXPECT_EQ(disconnect_notifications, 1);
}

TEST(network_manager, UpdateForwardsAndReturnsErrorState) {
    auto fake = std::make_unique<FakeNetworkClient>();
    FakeNetworkClient* fake_ptr = fake.get();
    NetworkManager manager(std::move(fake));

    fake_ptr->set_last_error(ErrorCode::CONNECTION_TIMEOUT);
    EXPECT_EQ(manager.get_last_error(), ErrorCode::CONNECTION_TIMEOUT);

    manager.update();
    EXPECT_EQ(fake_ptr->update_calls(), 1);
}

TEST(network_manager, SendMessagePassThrough) {
    auto fake = std::make_unique<FakeNetworkClient>();
    FakeNetworkClient* fake_ptr = fake.get();
    NetworkManager manager(std::move(fake));

    const std::vector<uint8_t> payload = {1, 2, 3, 4};
    manager.send_message(MsgId::kLoginReq, payload);

    ASSERT_EQ(fake_ptr->sent_messages().size(), 1u);
    EXPECT_EQ(fake_ptr->sent_messages()[0].msg_id,
              static_cast<uint16_t>(MsgId::kLoginReq));
    EXPECT_EQ(fake_ptr->sent_messages()[0].payload, payload);
}

TEST(network_manager, DispatchesRegisteredHandlers) {
    auto fake = std::make_unique<FakeNetworkClient>();
    FakeNetworkClient* fake_ptr = fake.get();
    NetworkManager manager(std::move(fake));

    std::atomic<int> handled{0};
    manager.register_handler(MsgId::kRoleListRsp,
                             [&handled](const NetworkPacket& packet) {
                                 if (packet.msg_id ==
                                     static_cast<uint16_t>(MsgId::kRoleListRsp)) {
                                     ++handled;
                                 }
                             });

    fake_ptr->EmitPacket(MakePacket(MsgId::kRoleListRsp, {9, 8, 7}));
    EXPECT_EQ(handled.load(), 1);
}

TEST(network_manager, DefaultHandlerHandlesUnregisteredMessage) {
    auto fake = std::make_unique<FakeNetworkClient>();
    FakeNetworkClient* fake_ptr = fake.get();
    NetworkManager manager(std::move(fake));

    std::atomic<int> fallback_calls{0};
    manager.set_default_handler([&fallback_calls](const NetworkPacket& packet) {
        if (packet.msg_id == static_cast<uint16_t>(MsgId::kServerNotice)) {
            ++fallback_calls;
        }
    });

    fake_ptr->EmitPacket(MakePacket(MsgId::kServerNotice, {1}));
    EXPECT_EQ(fallback_calls.load(), 1);
}

TEST(network_manager, DisconnectCallbackFiresOnRemoteDisconnect) {
    auto fake = std::make_unique<FakeNetworkClient>();
    FakeNetworkClient* fake_ptr = fake.get();
    NetworkManager manager(std::move(fake));

    int disconnect_notifications = 0;
    manager.set_on_disconnect([&disconnect_notifications]() { ++disconnect_notifications; });

    EXPECT_TRUE(manager.connect("127.0.0.1", 7000));
    fake_ptr->EmitDisconnect();

    EXPECT_FALSE(manager.is_connected());
    EXPECT_EQ(disconnect_notifications, 1);
}

TEST(network_manager, DispatchesLoginAndHeartbeatResponses) {
    auto fake = std::make_unique<FakeNetworkClient>();
    FakeNetworkClient* fake_ptr = fake.get();
    NetworkManager manager(std::move(fake));

    std::atomic<int> login_calls{0};
    std::atomic<int> heartbeat_calls{0};

    manager.register_handler(MsgId::kLoginRsp,
                             [&login_calls](const NetworkPacket& packet) {
                                 flatbuffers::Verifier verifier(
                                     packet.payload.data(), packet.payload.size());
                                 const bool valid = verifier.VerifyBuffer<mir2::proto::LoginRsp>(nullptr);
                                 EXPECT_TRUE(valid);
                                 if (!valid) {
                                     return;
                                 }
                                 const auto* rsp =
                                     flatbuffers::GetRoot<mir2::proto::LoginRsp>(packet.payload.data());
                                 ASSERT_NE(rsp, nullptr);
                                 EXPECT_EQ(rsp->account_id(), 42u);
                                 EXPECT_EQ(rsp->session_token()->str(), "token");
                                 ++login_calls;
                             });

    manager.register_handler(MsgId::kHeartbeatRsp,
                             [&heartbeat_calls](const NetworkPacket& packet) {
                                 flatbuffers::Verifier verifier(
                                     packet.payload.data(), packet.payload.size());
                                 const bool valid = verifier.VerifyBuffer<mir2::proto::HeartbeatRsp>(nullptr);
                                 EXPECT_TRUE(valid);
                                 if (!valid) {
                                     return;
                                 }
                                 const auto* rsp =
                                     flatbuffers::GetRoot<mir2::proto::HeartbeatRsp>(packet.payload.data());
                                 ASSERT_NE(rsp, nullptr);
                                 EXPECT_EQ(rsp->seq(), 99u);
                                 EXPECT_EQ(rsp->server_time(), 123u);
                                 ++heartbeat_calls;
                             });

    flatbuffers::FlatBufferBuilder login_builder;
    const auto token = login_builder.CreateString("token");
    const auto login_rsp = mir2::proto::CreateLoginRsp(
        login_builder, mir2::proto::ErrorCode::ERR_OK, 42u, token);
    login_builder.Finish(login_rsp);
    fake_ptr->EmitPacket(MakePacket(MsgId::kLoginRsp, BuildPayload(login_builder)));

    flatbuffers::FlatBufferBuilder heartbeat_builder;
    const auto heartbeat_rsp = mir2::proto::CreateHeartbeatRsp(heartbeat_builder, 99u, 123u);
    heartbeat_builder.Finish(heartbeat_rsp);
    fake_ptr->EmitPacket(MakePacket(MsgId::kHeartbeatRsp, BuildPayload(heartbeat_builder)));

    EXPECT_EQ(login_calls.load(), 1);
    EXPECT_EQ(heartbeat_calls.load(), 1);
}
