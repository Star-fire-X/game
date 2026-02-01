#include <gtest/gtest.h>

#include <flatbuffers/flatbuffers.h>

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "client/handlers/combat_handler.h"
#include "client/handlers/login_handler.h"
#include "client/handlers/movement_handler.h"
#include "client/network/network_manager.h"
#include "common/enums.h"
#include "combat_generated.h"
#include "game_generated.h"
#include "login_generated.h"

namespace {

using legend2::ConnectionState;
using mir2::common::ErrorCode;
using legend2::INetworkClient;
using legend2::NetworkManager;
using mir2::common::MsgId;
using mir2::common::NetworkPacket;
using mir2::game::handlers::CombatHandler;
using mir2::game::handlers::LoginHandler;
using mir2::game::handlers::MovementHandler;

struct SentMessage {
    uint16_t msg_id = 0;
    std::vector<uint8_t> payload;
};

class FakeNetworkClient : public INetworkClient {
public:
    bool connect(const std::string& host, uint16_t port) override {
        host_ = host;
        port_ = port;
        connected_ = true;
        if (on_connect_) {
            on_connect_();
        }
        return true;
    }

    void disconnect() override {
        connected_ = false;
        if (on_disconnect_) {
            on_disconnect_();
        }
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
    }

    ConnectionState get_state() const override {
        return connected_ ? ConnectionState::CONNECTED : ConnectionState::DISCONNECTED;
    }

    ErrorCode get_last_error() const override {
        return ErrorCode::SUCCESS;
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

    const std::vector<SentMessage>& sent_messages() const {
        return sent_messages_;
    }

private:
    bool connected_ = false;
    std::string host_;
    uint16_t port_ = 0;
    std::vector<SentMessage> sent_messages_;
    MessageCallback on_message_;
    EventCallback on_disconnect_;
    EventCallback on_connect_;
};

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

TEST(handlers_integration, LoginFlowDispatchesCallbacks) {
    auto fake = std::make_unique<FakeNetworkClient>();
    FakeNetworkClient* fake_ptr = fake.get();
    NetworkManager manager(std::move(fake));

    flatbuffers::FlatBufferBuilder req_builder;
    const auto username = req_builder.CreateString("user");
    const auto password = req_builder.CreateString("pass");
    const auto version = req_builder.CreateString("0.1.0");
    const auto req = mir2::proto::CreateLoginReq(req_builder, username, password, version);
    req_builder.Finish(req);

    manager.send_message(MsgId::kLoginReq, BuildPayload(req_builder));

    ASSERT_EQ(fake_ptr->sent_messages().size(), 1u);
    EXPECT_EQ(fake_ptr->sent_messages()[0].msg_id, static_cast<uint16_t>(MsgId::kLoginReq));

    int login_success_calls = 0;
    int request_calls = 0;
    uint64_t captured_account_id = 0;
    std::string captured_token;

    LoginHandler::Callbacks callbacks;
    callbacks.on_login_success = [&](uint64_t account_id, const std::string& token) {
        captured_account_id = account_id;
        captured_token = token;
        ++login_success_calls;
    };
    callbacks.request_character_list = [&]() { ++request_calls; };

    LoginHandler handler(callbacks);
    handler.BindHandlers(manager);

    flatbuffers::FlatBufferBuilder rsp_builder;
    const auto token = rsp_builder.CreateString("token");
    const auto rsp = mir2::proto::CreateLoginRsp(
        rsp_builder, mir2::proto::ErrorCode::ERR_OK, 42u, token);
    rsp_builder.Finish(rsp);

    fake_ptr->EmitPacket(MakePacket(MsgId::kLoginRsp, BuildPayload(rsp_builder)));

    EXPECT_EQ(login_success_calls, 1);
    EXPECT_EQ(request_calls, 1);
    EXPECT_EQ(captured_account_id, 42u);
    EXPECT_EQ(captured_token, "token");
}

TEST(MovementHandlerIntegration, MovementHandlerDispatchesMoveResponse) {
    auto fake = std::make_unique<FakeNetworkClient>();
    FakeNetworkClient* fake_ptr = fake.get();
    NetworkManager manager(std::move(fake));

    int move_calls = 0;
    int captured_x = 0;
    int captured_y = 0;

    MovementHandler::Callbacks callbacks;
    callbacks.on_move_response = [&](int x, int y) {
        captured_x = x;
        captured_y = y;
        ++move_calls;
    };

    MovementHandler handler(callbacks);
    handler.BindHandlers(manager);

    flatbuffers::FlatBufferBuilder builder;
    const auto rsp = mir2::proto::CreateMoveRsp(
        builder, mir2::proto::ErrorCode::ERR_OK, 7, 8);
    builder.Finish(rsp);

    fake_ptr->EmitPacket(MakePacket(MsgId::kMoveRsp, BuildPayload(builder)));

    EXPECT_EQ(move_calls, 1);
    EXPECT_EQ(captured_x, 7);
    EXPECT_EQ(captured_y, 8);
}

TEST(CombatHandlerIntegration, CombatHandlerDispatchesAttackResponse) {
    auto fake = std::make_unique<FakeNetworkClient>();
    FakeNetworkClient* fake_ptr = fake.get();
    NetworkManager manager(std::move(fake));

    int attack_calls = 0;
    mir2::proto::ErrorCode captured_code = mir2::proto::ErrorCode::ERR_UNKNOWN;

    CombatHandler::Callbacks callbacks;
    callbacks.on_attack_response = [&](mir2::proto::ErrorCode code,
                                       uint64_t,
                                       uint64_t,
                                       int,
                                       int,
                                       bool) {
        captured_code = code;
        ++attack_calls;
    };

    CombatHandler handler(callbacks);
    handler.BindHandlers(manager);

    flatbuffers::FlatBufferBuilder builder;
    const auto rsp = mir2::proto::CreateAttackRsp(
        builder, mir2::proto::ErrorCode::ERR_OK, 11u, 22u, 5, 9, false);
    builder.Finish(rsp);

    fake_ptr->EmitPacket(MakePacket(MsgId::kAttackRsp, BuildPayload(builder)));

    EXPECT_EQ(attack_calls, 1);
    EXPECT_EQ(captured_code, mir2::proto::ErrorCode::ERR_OK);
}
