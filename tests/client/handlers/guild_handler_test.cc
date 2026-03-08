#include "client/handlers/guild_handler.h"

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
#include "guild_generated.h"

namespace {

using mir2::client::ConnectionState;
using mir2::client::INetworkManager;
using mir2::common::MsgId;
using mir2::common::NetworkPacket;
using mir2::game::handlers::GuildHandler;
using mir2::game::handlers::GuildInfoSyncData;

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

class GuildClientHandlerTest : public ::testing::Test {
protected:
    void SetUp() override {
        parse_errors_.clear();
        create_response_.reset();
        guild_sync_.reset();
    }

    GuildHandler::Callbacks MakeCallbacks() {
        GuildHandler::Callbacks callbacks;
        callbacks.on_create_response =
            [this](bool success, mir2::proto::ErrorCode code, uint64_t guild_id) {
                create_response_ = std::make_tuple(success, code, guild_id);
            };
        callbacks.on_guild_info_sync = [this](const GuildInfoSyncData& sync) {
            guild_sync_ = sync;
        };
        callbacks.on_parse_error = [this](const std::string& error) {
            parse_errors_.push_back(error);
        };
        return callbacks;
    }

    std::vector<std::string> parse_errors_;
    std::optional<std::tuple<bool, mir2::proto::ErrorCode, uint64_t>> create_response_;
    std::optional<GuildInfoSyncData> guild_sync_;
};

TEST_F(GuildClientHandlerTest, BindHandlersRegistersGuildMessages) {
    auto handler = std::make_shared<GuildHandler>(MakeCallbacks());
    MockNetworkManager manager;

    handler->BindHandlers(manager);

    EXPECT_TRUE(manager.HasHandler(MsgId::kGuildCreateRsp));
    EXPECT_TRUE(manager.HasHandler(MsgId::kGuildJoinRsp));
    EXPECT_TRUE(manager.HasHandler(MsgId::kGuildLeaveRsp));
    EXPECT_TRUE(manager.HasHandler(MsgId::kGuildKickRsp));
    EXPECT_TRUE(manager.HasHandler(MsgId::kGuildDeclareWarRsp));
    EXPECT_TRUE(manager.HasHandler(MsgId::kGuildCancelWarRsp));
    EXPECT_TRUE(manager.HasHandler(MsgId::kGuildMakeAllyRsp));
    EXPECT_TRUE(manager.HasHandler(MsgId::kGuildBreakAllyRsp));
    EXPECT_TRUE(manager.HasHandler(MsgId::kGuildUpdateNoticeRsp));
    EXPECT_TRUE(manager.HasHandler(MsgId::kGuildUpdateRankRsp));
    EXPECT_TRUE(manager.HasHandler(MsgId::kGuildInfoSync));
    EXPECT_TRUE(manager.HasHandler(
        static_cast<MsgId>(mir2::proto::GuildMessageType::CREATE)));
}

TEST_F(GuildClientHandlerTest, HandleCreateResponseParsesPayload) {
    flatbuffers::FlatBufferBuilder builder;
    const auto name = builder.CreateString("Knights");
    const auto guild_info = mir2::proto::CreateGuildInfo(
        builder, 100, name, 1, 1, 1001, name, 100);
    const auto rsp = mir2::proto::CreateCreateGuildResponse(
        builder, true, static_cast<int>(mir2::proto::ErrorCode::ERR_OK), guild_info);
    builder.Finish(rsp);

    auto handler = std::make_shared<GuildHandler>(MakeCallbacks());
    NetworkPacket packet = MakePacket(MsgId::kGuildCreateRsp, builder);
    handler->HandleCreateGuildResponse(packet);

    ASSERT_TRUE(create_response_.has_value());
    EXPECT_TRUE(std::get<0>(*create_response_));
    EXPECT_EQ(std::get<1>(*create_response_), mir2::proto::ErrorCode::ERR_OK);
    EXPECT_EQ(std::get<2>(*create_response_), 100u);
    EXPECT_TRUE(parse_errors_.empty());
}

TEST_F(GuildClientHandlerTest, HandleGuildInfoSyncParsesPayload) {
    flatbuffers::FlatBufferBuilder builder;
    const auto guild_name = builder.CreateString("GuildA");
    const auto leader_name = builder.CreateString("Leader");
    std::vector<flatbuffers::Offset<flatbuffers::String>> notices;
    notices.push_back(builder.CreateString("Line1"));
    const auto notice_vec = builder.CreateVector(notices);
    const auto guild_info = mir2::proto::CreateGuildInfo(
        builder,
        200,
        guild_name,
        1,
        2,
        3001,
        leader_name,
        100,
        notice_vec);
    const auto sync = mir2::proto::CreateGuildInfoSync(builder, guild_info);
    builder.Finish(sync);

    auto handler = std::make_shared<GuildHandler>(MakeCallbacks());
    NetworkPacket packet = MakePacket(MsgId::kGuildInfoSync, builder);
    handler->HandleGuildInfoSync(packet);

    ASSERT_TRUE(guild_sync_.has_value());
    EXPECT_TRUE(guild_sync_->has_guild);
    EXPECT_EQ(guild_sync_->guild_id, 200u);
    EXPECT_EQ(guild_sync_->guild_name, "GuildA");
    EXPECT_EQ(guild_sync_->leader_name, "Leader");
    ASSERT_EQ(guild_sync_->notice_list.size(), 1u);
    EXPECT_EQ(guild_sync_->notice_list[0], "Line1");
}

TEST_F(GuildClientHandlerTest, SendCreateGuildRequestEncodesPayload) {
    MockNetworkManager manager;

    GuildHandler::SendCreateGuildRequest(manager, "Wolves");

    ASSERT_EQ(manager.sent_messages().size(), 1u);
    EXPECT_EQ(manager.sent_messages()[0].msg_id, MsgId::kGuildCreateReq);

    flatbuffers::Verifier verifier(manager.sent_messages()[0].payload.data(),
                                   manager.sent_messages()[0].payload.size());
    ASSERT_TRUE(verifier.VerifyBuffer<mir2::proto::CreateGuildRequest>(nullptr));
    const auto* req = flatbuffers::GetRoot<mir2::proto::CreateGuildRequest>(
        manager.sent_messages()[0].payload.data());
    ASSERT_NE(req, nullptr);
    EXPECT_EQ(req->guild_name()->str(), "Wolves");
}

TEST_F(GuildClientHandlerTest, SendUpdateRankRequestEncodesMembers) {
    MockNetworkManager manager;

    std::vector<mir2::game::handlers::GuildRankUpdateMember> members;
    members.push_back({1001, 1});
    members.push_back({1002, 2});
    GuildHandler::SendUpdateRankRequest(manager, members);

    ASSERT_EQ(manager.sent_messages().size(), 1u);
    EXPECT_EQ(manager.sent_messages()[0].msg_id, MsgId::kGuildUpdateRankReq);

    flatbuffers::Verifier verifier(manager.sent_messages()[0].payload.data(),
                                   manager.sent_messages()[0].payload.size());
    ASSERT_TRUE(verifier.VerifyBuffer<mir2::proto::UpdateRankRequest>(nullptr));
    const auto* req = flatbuffers::GetRoot<mir2::proto::UpdateRankRequest>(
        manager.sent_messages()[0].payload.data());
    ASSERT_NE(req, nullptr);
    ASSERT_NE(req->members(), nullptr);
    ASSERT_EQ(req->members()->size(), 2u);
    EXPECT_EQ(req->members()->Get(0)->character_id(), 1001u);
    EXPECT_EQ(req->members()->Get(0)->rank(), 1u);
    EXPECT_EQ(req->members()->Get(1)->character_id(), 1002u);
    EXPECT_EQ(req->members()->Get(1)->rank(), 2u);
}

}  // namespace
