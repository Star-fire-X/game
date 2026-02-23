#include "client/handlers/ranking_handler.h"

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
#include "ranking_generated.h"

namespace {

using mir2::client::ConnectionState;
using mir2::client::INetworkManager;
using mir2::common::MsgId;
using mir2::common::NetworkPacket;
using mir2::game::handlers::RankingHandler;
using mir2::game::handlers::RankingResponseData;

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

class RankingClientHandlerTest : public ::testing::Test {
protected:
    RankingHandler::Callbacks MakeCallbacks() {
        RankingHandler::Callbacks callbacks;
        callbacks.on_ranking_response =
            [this](bool success, mir2::proto::ErrorCode code, const RankingResponseData& rsp) {
                ranking_response_ = std::make_tuple(success, code, rsp);
            };
        return callbacks;
    }

    std::optional<std::tuple<bool, mir2::proto::ErrorCode, RankingResponseData>> ranking_response_;
};

TEST_F(RankingClientHandlerTest, BindHandlersRegistersRankingMessages) {
    auto handler = std::make_shared<RankingHandler>(MakeCallbacks());
    MockNetworkManager manager;

    handler->BindHandlers(manager);

    EXPECT_TRUE(manager.HasHandler(MsgId::kRankingRsp));
    EXPECT_TRUE(manager.HasHandler(MsgId::kRankingMyRankRsp));
}

TEST_F(RankingClientHandlerTest, HandleRankingResponseParsesEntries) {
    flatbuffers::FlatBufferBuilder builder;
    std::vector<flatbuffers::Offset<mir2::proto::RankEntry>> entries;
    const auto name = builder.CreateString("TopPlayer");
    const auto extra = builder.CreateString("x");
    entries.emplace_back(
        mir2::proto::CreateRankEntry(builder, 1, 2001, name, 88, extra));
    const auto entries_vec = builder.CreateVector(entries);
    const auto rsp = mir2::proto::CreateRankingRsp(
        builder,
        true,
        static_cast<int>(mir2::proto::ErrorCode::ERR_OK),
        mir2::proto::RankingType::LEVEL,
        1,
        entries_vec);
    builder.Finish(rsp);

    auto handler = std::make_shared<RankingHandler>(MakeCallbacks());
    NetworkPacket packet = MakePacket(MsgId::kRankingRsp, builder);
    handler->HandleRankingResponse(packet);

    ASSERT_TRUE(ranking_response_.has_value());
    EXPECT_TRUE(std::get<0>(*ranking_response_));
    EXPECT_EQ(std::get<1>(*ranking_response_), mir2::proto::ErrorCode::ERR_OK);
    const auto& response = std::get<2>(*ranking_response_);
    EXPECT_EQ(response.total_count, 1u);
    ASSERT_EQ(response.entries.size(), 1u);
    EXPECT_EQ(response.entries[0].entity_id, 2001u);
    EXPECT_EQ(response.entries[0].value, 88);
}

TEST_F(RankingClientHandlerTest, SendRankingRequestEncodesPayload) {
    MockNetworkManager manager;

    RankingHandler::SendRankingRequest(manager, mir2::proto::RankingType::GOLD, 2, 50);

    ASSERT_EQ(manager.sent_messages().size(), 1u);
    EXPECT_EQ(manager.sent_messages()[0].msg_id, MsgId::kRankingReq);
    flatbuffers::Verifier verifier(manager.sent_messages()[0].payload.data(),
                                   manager.sent_messages()[0].payload.size());
    ASSERT_TRUE(verifier.VerifyBuffer<mir2::proto::RankingReq>(nullptr));
    const auto* req =
        flatbuffers::GetRoot<mir2::proto::RankingReq>(manager.sent_messages()[0].payload.data());
    ASSERT_NE(req, nullptr);
    EXPECT_EQ(req->ranking_type(), mir2::proto::RankingType::GOLD);
    EXPECT_EQ(req->page(), 2u);
    EXPECT_EQ(req->page_size(), 50u);
}

} // namespace
