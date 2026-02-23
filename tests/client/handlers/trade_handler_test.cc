#include "client/handlers/trade_handler.h"

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
#include "trade_generated.h"

namespace {

using mir2::client::ConnectionState;
using mir2::client::INetworkManager;
using mir2::common::MsgId;
using mir2::common::NetworkPacket;
using mir2::game::handlers::TradeHandler;
using mir2::game::handlers::TradeUpdateData;

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

    void Dispatch(MsgId msg_id, const NetworkPacket& packet) {
        auto it = handlers_.find(msg_id);
        if (it != handlers_.end()) {
            it->second(packet);
            return;
        }
        if (default_handler_) {
            default_handler_(packet);
        }
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

class TradeClientHandlerTest : public ::testing::Test {
protected:
    void SetUp() override {
        parse_errors_.clear();
        trade_response_.reset();
        update_.reset();
    }

    TradeHandler::Callbacks MakeCallbacks() {
        TradeHandler::Callbacks callbacks;
        callbacks.on_trade_response =
            [this](bool success, mir2::proto::ErrorCode code, uint64_t trade_id) {
                trade_response_ = std::make_tuple(success, code, trade_id);
            };
        callbacks.on_trade_update = [this](const TradeUpdateData& update) {
            update_ = update;
        };
        callbacks.on_parse_error = [this](const std::string& error) {
            parse_errors_.push_back(error);
        };
        return callbacks;
    }

    std::vector<std::string> parse_errors_;
    std::optional<std::tuple<bool, mir2::proto::ErrorCode, uint64_t>> trade_response_;
    std::optional<TradeUpdateData> update_;
};

TEST_F(TradeClientHandlerTest, BindHandlersRegistersAllTradeMessages) {
    auto handler = std::make_shared<TradeHandler>(MakeCallbacks());
    MockNetworkManager manager;

    handler->BindHandlers(manager);

    EXPECT_TRUE(manager.HasHandler(MsgId::kTradeRsp));
    EXPECT_TRUE(manager.HasHandler(MsgId::kTradeAddItemRsp));
    EXPECT_TRUE(manager.HasHandler(MsgId::kTradeSetGoldRsp));
    EXPECT_TRUE(manager.HasHandler(MsgId::kTradeConfirmRsp));
    EXPECT_TRUE(manager.HasHandler(MsgId::kTradeCancelRsp));
    EXPECT_TRUE(manager.HasHandler(MsgId::kTradeUpdate));
    EXPECT_TRUE(manager.HasHandler(MsgId::kTradeComplete));
}

TEST_F(TradeClientHandlerTest, HandleTradeResponseParsesPayload) {
    flatbuffers::FlatBufferBuilder builder;
    const auto rsp = mir2::proto::CreateTradeRsp(
        builder,
        true,
        static_cast<int>(mir2::proto::ErrorCode::ERR_OK),
        1234);
    builder.Finish(rsp);

    auto handler = std::make_shared<TradeHandler>(MakeCallbacks());
    NetworkPacket packet = MakePacket(MsgId::kTradeRsp, builder);
    handler->HandleTradeResponse(packet);

    ASSERT_TRUE(trade_response_.has_value());
    EXPECT_TRUE(std::get<0>(*trade_response_));
    EXPECT_EQ(std::get<1>(*trade_response_), mir2::proto::ErrorCode::ERR_OK);
    EXPECT_EQ(std::get<2>(*trade_response_), 1234u);
    EXPECT_TRUE(parse_errors_.empty());
}

TEST_F(TradeClientHandlerTest, HandleTradeUpdateParsesItemsAndFlags) {
    flatbuffers::FlatBufferBuilder builder;

    std::vector<flatbuffers::Offset<mir2::proto::TradeItemInfo>> left_items;
    left_items.emplace_back(
        mir2::proto::CreateTradeItemInfo(builder, 1, 2001, 2));
    const auto left_vec = builder.CreateVector(left_items);

    std::vector<flatbuffers::Offset<mir2::proto::TradeItemInfo>> right_items;
    right_items.emplace_back(
        mir2::proto::CreateTradeItemInfo(builder, 3, 3001, 1));
    const auto right_vec = builder.CreateVector(right_items);

    const auto update = mir2::proto::CreateTradeUpdate(
        builder,
        5555,
        1001,
        1002,
        left_vec,
        right_vec,
        100,
        200,
        true,
        false);
    builder.Finish(update);

    auto handler = std::make_shared<TradeHandler>(MakeCallbacks());
    NetworkPacket packet = MakePacket(MsgId::kTradeUpdate, builder);
    handler->HandleTradeUpdate(packet);

    ASSERT_TRUE(update_.has_value());
    EXPECT_EQ(update_->trade_id, 5555u);
    EXPECT_EQ(update_->left_character_id, 1001u);
    EXPECT_EQ(update_->right_character_id, 1002u);
    EXPECT_EQ(update_->left_gold, 100u);
    EXPECT_EQ(update_->right_gold, 200u);
    EXPECT_TRUE(update_->left_confirmed);
    EXPECT_FALSE(update_->right_confirmed);
    ASSERT_EQ(update_->left_items.size(), 1u);
    ASSERT_EQ(update_->right_items.size(), 1u);
    EXPECT_EQ(update_->left_items[0].inventory_slot, 1u);
    EXPECT_EQ(update_->left_items[0].item_id, 2001u);
    EXPECT_EQ(update_->left_items[0].count, 2u);
    EXPECT_EQ(update_->right_items[0].inventory_slot, 3u);
    EXPECT_EQ(update_->right_items[0].item_id, 3001u);
    EXPECT_EQ(update_->right_items[0].count, 1u);
}

TEST_F(TradeClientHandlerTest, SendTradeRequestEncodesPayload) {
    MockNetworkManager manager;

    TradeHandler::SendTradeRequest(manager, 9876);

    ASSERT_EQ(manager.sent_messages().size(), 1u);
    EXPECT_EQ(manager.sent_messages()[0].msg_id, MsgId::kTradeReq);

    flatbuffers::Verifier verifier(manager.sent_messages()[0].payload.data(),
                                   manager.sent_messages()[0].payload.size());
    ASSERT_TRUE(verifier.VerifyBuffer<mir2::proto::TradeReq>(nullptr));
    const auto* req =
        flatbuffers::GetRoot<mir2::proto::TradeReq>(manager.sent_messages()[0].payload.data());
    ASSERT_NE(req, nullptr);
    EXPECT_EQ(req->target_character_id(), 9876u);
}

} // namespace
