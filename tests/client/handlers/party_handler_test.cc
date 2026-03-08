#include "client/handlers/party_handler.h"

#include <gtest/gtest.h>

#include <flatbuffers/flatbuffers.h>

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "common/enums.h"
#include "party_generated.h"

namespace {

using mir2::client::ConnectionState;
using mir2::client::INetworkManager;
using mir2::common::MsgId;
using mir2::common::NetworkPacket;
using mir2::game::handlers::PartyHandler;
using mir2::game::handlers::PartyUpdateData;

class MockNetworkManager : public INetworkManager {
public:
    bool connect(const std::string&, uint16_t) override { return false; }
    void disconnect() override {}
    bool is_connected() const override { return false; }

    void send_message(mir2::common::MsgId msg_id,
                      const std::vector<uint8_t>& payload) override {
        sent_messages_.push_back({msg_id, payload});
    }

    void register_handler(mir2::common::MsgId msg_id, HandlerFunc handler) override {
        handlers_[msg_id] = std::move(handler);
    }

    void set_default_handler(HandlerFunc handler) override {
        default_handler_ = std::move(handler);
    }

    void set_on_connect(EventCallback callback) override {
        on_connect_ = std::move(callback);
    }

    void set_on_disconnect(EventCallback callback) override {
        on_disconnect_ = std::move(callback);
    }

    ConnectionState get_state() const override {
        return ConnectionState::DISCONNECTED;
    }

    mir2::common::ErrorCode get_last_error() const override {
        return mir2::common::ErrorCode::SUCCESS;
    }

    void update() override {}

    bool HasHandler(MsgId msg_id) const {
        return handlers_.find(msg_id) != handlers_.end();
    }

    struct SentMessage {
        mir2::common::MsgId msg_id = mir2::common::MsgId::kNone;
        std::vector<uint8_t> payload;
    };

    const std::vector<SentMessage>& sent_messages() const { return sent_messages_; }

private:
    std::map<mir2::common::MsgId, HandlerFunc> handlers_;
    std::vector<SentMessage> sent_messages_;
    HandlerFunc default_handler_;
    EventCallback on_connect_;
    EventCallback on_disconnect_;
};

NetworkPacket MakePacket(MsgId msg_id, flatbuffers::FlatBufferBuilder& builder) {
    NetworkPacket packet;
    packet.msg_id = static_cast<uint16_t>(msg_id);
    const uint8_t* data = builder.GetBufferPointer();
    packet.payload.assign(data, data + builder.GetSize());
    return packet;
}

class PartyClientHandlerTest : public ::testing::Test {
protected:
    void SetUp() override {
        parse_errors_.clear();
        invite_response_.reset();
        update_.reset();
    }

    PartyHandler::Callbacks MakeCallbacks() {
        PartyHandler::Callbacks callbacks;
        callbacks.on_invite_response =
            [this](bool success, mir2::proto::ErrorCode code) {
                invite_response_ = std::make_pair(success, code);
            };
        callbacks.on_party_update = [this](const PartyUpdateData& update) {
            update_ = update;
        };
        callbacks.on_parse_error = [this](const std::string& error) {
            parse_errors_.push_back(error);
        };
        return callbacks;
    }

    std::vector<std::string> parse_errors_;
    std::optional<std::pair<bool, mir2::proto::ErrorCode>> invite_response_;
    std::optional<PartyUpdateData> update_;
};

TEST_F(PartyClientHandlerTest, BindHandlersRegistersAllPartyMessages) {
    auto handler = std::make_shared<PartyHandler>(MakeCallbacks());
    MockNetworkManager manager;

    handler->BindHandlers(manager);

    EXPECT_TRUE(manager.HasHandler(MsgId::kPartyInviteRsp));
    EXPECT_TRUE(manager.HasHandler(MsgId::kPartyJoinRsp));
    EXPECT_TRUE(manager.HasHandler(MsgId::kPartyLeaveRsp));
    EXPECT_TRUE(manager.HasHandler(MsgId::kPartyKickRsp));
    EXPECT_TRUE(manager.HasHandler(MsgId::kPartyUpdate));
}

TEST_F(PartyClientHandlerTest, HandleInviteResponseParsesPayload) {
    flatbuffers::FlatBufferBuilder builder;
    const auto rsp = mir2::proto::CreatePartyInviteRsp(
        builder,
        true,
        static_cast<int>(mir2::proto::ErrorCode::ERR_OK));
    builder.Finish(rsp);

    auto handler = std::make_shared<PartyHandler>(MakeCallbacks());
    NetworkPacket packet = MakePacket(MsgId::kPartyInviteRsp, builder);
    handler->HandleInviteResponse(packet);

    ASSERT_TRUE(invite_response_.has_value());
    EXPECT_TRUE(invite_response_->first);
    EXPECT_EQ(invite_response_->second, mir2::proto::ErrorCode::ERR_OK);
    EXPECT_TRUE(parse_errors_.empty());
}

TEST_F(PartyClientHandlerTest, HandlePartyUpdateParsesMembers) {
    flatbuffers::FlatBufferBuilder builder;

    std::vector<flatbuffers::Offset<mir2::proto::PartyMemberInfo>> members;
    const auto name = builder.CreateString("MemberA");
    members.emplace_back(
        mir2::proto::CreatePartyMemberInfo(builder,
                                           1001,
                                           name,
                                           80,
                                           100,
                                           2,
                                           120,
                                           240,
                                           true));
    const auto members_vec = builder.CreateVector(members);

    const auto update =
        mir2::proto::CreatePartyUpdate(builder, 999, 1001, members_vec);
    builder.Finish(update);

    auto handler = std::make_shared<PartyHandler>(MakeCallbacks());
    NetworkPacket packet = MakePacket(MsgId::kPartyUpdate, builder);
    handler->HandlePartyUpdate(packet);

    ASSERT_TRUE(update_.has_value());
    EXPECT_EQ(update_->party_id, 999u);
    EXPECT_EQ(update_->leader_character_id, 1001u);
    ASSERT_EQ(update_->members.size(), 1u);
    EXPECT_EQ(update_->members[0].character_id, 1001u);
    EXPECT_EQ(update_->members[0].name, "MemberA");
    EXPECT_EQ(update_->members[0].hp, 80u);
    EXPECT_EQ(update_->members[0].max_hp, 100u);
    EXPECT_EQ(update_->members[0].map_id, 2u);
    EXPECT_EQ(update_->members[0].x, 120u);
    EXPECT_EQ(update_->members[0].y, 240u);
    EXPECT_TRUE(update_->members[0].online);
}

TEST_F(PartyClientHandlerTest, SendKickRequestEncodesPayload) {
    MockNetworkManager manager;

    PartyHandler::SendKickRequest(manager, 4321);

    ASSERT_EQ(manager.sent_messages().size(), 1u);
    EXPECT_EQ(manager.sent_messages()[0].msg_id, MsgId::kPartyKickReq);

    flatbuffers::Verifier verifier(manager.sent_messages()[0].payload.data(),
                                   manager.sent_messages()[0].payload.size());
    ASSERT_TRUE(verifier.VerifyBuffer<mir2::proto::PartyKickReq>(nullptr));
    const auto* req =
        flatbuffers::GetRoot<mir2::proto::PartyKickReq>(manager.sent_messages()[0].payload.data());
    ASSERT_NE(req, nullptr);
    EXPECT_EQ(req->target_character_id(), 4321u);
}

} // namespace
