/**
 * @file chat_handler_test.cc
 * @brief Unit tests for the client ChatHandler.
 *
 * Validates FlatBuffers deserialization, callback dispatch, error handling,
 * and the owner weak_ptr lifetime guard.
 */

#include "client/handlers/chat_handler.h"

#include <gtest/gtest.h>
#include <flatbuffers/flatbuffers.h>

#include "chat_generated.h"
#include "system_generated.h"
#include "common_generated.h"
#include "common/enums.h"

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace {

using mir2::client::ConnectionState;
using mir2::client::INetworkManager;
using mir2::common::MsgId;
using mir2::common::NetworkPacket;
using mir2::game::handlers::ChatChannel;
using mir2::game::handlers::ChatHandler;
using mir2::game::handlers::ChatMessageData;

// ---------------------------------------------------------------------------
// Mock INetworkManager -- records registered handlers so that BindHandlers
// can be verified without pulling in real network dependencies.
// ---------------------------------------------------------------------------
class MockNetworkManager : public INetworkManager {
public:
    bool connect(const std::string& /*host*/, uint16_t /*port*/) override {
        return false;
    }

    void disconnect() override {}

    bool is_connected() const override {
        return false;
    }

    void send_message(mir2::common::MsgId /*msg_id*/,
                      const std::vector<uint8_t>& /*payload*/) override {}

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

    // Test accessors.
    bool HasHandler(MsgId msg_id) const {
        return handlers_.find(msg_id) != handlers_.end();
    }

    size_t handler_count() const {
        return handlers_.size();
    }

    // Invoke a registered handler by MsgId.
    void Dispatch(MsgId msg_id, const NetworkPacket& packet) {
        auto it = handlers_.find(msg_id);
        if (it != handlers_.end()) {
            it->second(packet);
        }
    }

private:
    std::map<MsgId, HandlerFunc> handlers_;
    HandlerFunc default_handler_;
    EventCallback on_connect_;
    EventCallback on_disconnect_;
};

// ---------------------------------------------------------------------------
// Helper: build a NetworkPacket from a finished FlatBufferBuilder.
// ---------------------------------------------------------------------------
NetworkPacket MakePacketFromBuilder(uint16_t msg_id,
                                   flatbuffers::FlatBufferBuilder& fbb) {
    NetworkPacket packet;
    packet.msg_id = msg_id;
    const uint8_t* buf = fbb.GetBufferPointer();
    packet.payload.assign(buf, buf + fbb.GetSize());
    return packet;
}

// ---------------------------------------------------------------------------
// Helper: build an empty NetworkPacket (no payload).
// ---------------------------------------------------------------------------
NetworkPacket MakeEmptyPacket(uint16_t msg_id) {
    NetworkPacket packet;
    packet.msg_id = msg_id;
    return packet;
}

// ---------------------------------------------------------------------------
// Test fixture providing reusable callback state.
// ---------------------------------------------------------------------------
class ChatHandlerTest : public ::testing::Test {
protected:
    void SetUp() override {
        received_error_code.reset();
        received_message.reset();
        received_system_message.clear();
        received_system_timestamp = -1;
        parse_errors.clear();
    }

    ChatHandler::Callbacks MakeCallbacks(
        std::optional<std::weak_ptr<void>> owner = std::nullopt) {
        ChatHandler::Callbacks cb;
        cb.owner = owner;
        cb.on_chat_response = [this](mir2::proto::ErrorCode code) {
            received_error_code = code;
        };
        cb.on_chat_message = [this](const ChatMessageData& msg) {
            received_message = msg;
        };
        cb.on_system_message = [this](const std::string& message,
                                       int64_t timestamp) {
            received_system_message = message;
            received_system_timestamp = timestamp;
        };
        cb.on_parse_error = [this](const std::string& error) {
            parse_errors.push_back(error);
        };
        return cb;
    }

    // Captured state from callbacks.
    std::optional<mir2::proto::ErrorCode> received_error_code;
    std::optional<ChatMessageData> received_message;
    std::string received_system_message;
    int64_t received_system_timestamp = -1;
    std::vector<std::string> parse_errors;
};

// ===========================================================================
// HandleChatResponse tests
// ===========================================================================

TEST_F(ChatHandlerTest, HandleChatResponse_EmptyPayloadTriggersParseError) {
    auto handler = std::make_shared<ChatHandler>(MakeCallbacks());
    NetworkPacket packet = MakeEmptyPacket(
        static_cast<uint16_t>(MsgId::kChatRsp));

    handler->HandleChatResponse(packet);

    ASSERT_EQ(parse_errors.size(), 1u);
    EXPECT_NE(parse_errors[0].find("Empty"), std::string::npos);
    EXPECT_FALSE(received_error_code.has_value());
}

TEST_F(ChatHandlerTest, HandleChatResponse_ValidChatRspWithErrorCode) {
    flatbuffers::FlatBufferBuilder fbb(128);
    auto rsp = mir2::proto::CreateChatRsp(
        fbb, mir2::proto::ErrorCode::ERR_TARGET_NOT_FOUND);
    fbb.Finish(rsp);

    auto handler = std::make_shared<ChatHandler>(MakeCallbacks());
    NetworkPacket packet = MakePacketFromBuilder(
        static_cast<uint16_t>(MsgId::kChatRsp), fbb);

    handler->HandleChatResponse(packet);

    EXPECT_TRUE(parse_errors.empty());
    ASSERT_TRUE(received_error_code.has_value());
    EXPECT_EQ(*received_error_code, mir2::proto::ErrorCode::ERR_TARGET_NOT_FOUND);
}

TEST_F(ChatHandlerTest, HandleChatResponse_ValidChatRspWithOkCode) {
    flatbuffers::FlatBufferBuilder fbb(128);
    auto rsp = mir2::proto::CreateChatRsp(fbb, mir2::proto::ErrorCode::ERR_OK);
    fbb.Finish(rsp);

    auto handler = std::make_shared<ChatHandler>(MakeCallbacks());
    NetworkPacket packet = MakePacketFromBuilder(
        static_cast<uint16_t>(MsgId::kChatRsp), fbb);

    handler->HandleChatResponse(packet);

    EXPECT_TRUE(parse_errors.empty());
    ASSERT_TRUE(received_error_code.has_value());
    EXPECT_EQ(*received_error_code, mir2::proto::ErrorCode::ERR_OK);
}

TEST_F(ChatHandlerTest, HandleChatResponse_GarbagePayloadTriggersParseError) {
    NetworkPacket packet;
    packet.msg_id = static_cast<uint16_t>(MsgId::kChatRsp);
    packet.payload = {0xDE, 0xAD, 0xBE, 0xEF};

    auto handler = std::make_shared<ChatHandler>(MakeCallbacks());
    handler->HandleChatResponse(packet);

    EXPECT_FALSE(parse_errors.empty());
    EXPECT_FALSE(received_error_code.has_value());
}

// ===========================================================================
// HandleChatMessage tests
// ===========================================================================

TEST_F(ChatHandlerTest, HandleChatMessage_EmptyPayloadTriggersParseError) {
    auto handler = std::make_shared<ChatHandler>(MakeCallbacks());
    NetworkPacket packet = MakeEmptyPacket(
        static_cast<uint16_t>(MsgId::kPrivateChat));

    handler->HandleChatMessage(packet);

    ASSERT_EQ(parse_errors.size(), 1u);
    EXPECT_NE(parse_errors[0].find("Empty"), std::string::npos);
    EXPECT_FALSE(received_message.has_value());
}

TEST_F(ChatHandlerTest, HandleChatMessage_ValidMessageWithAllFields) {
    flatbuffers::FlatBufferBuilder fbb(256);
    auto msg = mir2::proto::CreateChatMessageDirect(
        fbb,
        mir2::proto::ChatChannel::GUILD,
        /*from_id=*/12345,
        /*from_name=*/"TestPlayer",
        /*to_id=*/67890,
        /*content=*/"Hello Guild!",
        /*color=*/0xFF00FF00,
        /*timestamp=*/1700000000);
    fbb.Finish(msg);

    auto handler = std::make_shared<ChatHandler>(MakeCallbacks());
    NetworkPacket packet = MakePacketFromBuilder(
        static_cast<uint16_t>(MsgId::kGuildChat), fbb);

    handler->HandleChatMessage(packet);

    EXPECT_TRUE(parse_errors.empty());
    ASSERT_TRUE(received_message.has_value());

    const auto& data = *received_message;
    EXPECT_EQ(data.channel, ChatChannel::kGuild);
    EXPECT_EQ(data.from_id, 12345u);
    EXPECT_EQ(data.from_name, "TestPlayer");
    EXPECT_EQ(data.to_id, 67890u);
    EXPECT_EQ(data.content, "Hello Guild!");
    EXPECT_EQ(data.color, 0xFF00FF00u);
    EXPECT_EQ(data.timestamp, 1700000000);
}

TEST_F(ChatHandlerTest, HandleChatMessage_PrivateChannelMapsCorrectly) {
    flatbuffers::FlatBufferBuilder fbb(256);
    auto msg = mir2::proto::CreateChatMessageDirect(
        fbb,
        mir2::proto::ChatChannel::PRIVATE,
        /*from_id=*/111,
        /*from_name=*/"Whisperer",
        /*to_id=*/222,
        /*content=*/"Secret message",
        /*color=*/0xFFFFFFFF,
        /*timestamp=*/1000);
    fbb.Finish(msg);

    auto handler = std::make_shared<ChatHandler>(MakeCallbacks());
    NetworkPacket packet = MakePacketFromBuilder(
        static_cast<uint16_t>(MsgId::kPrivateChat), fbb);

    handler->HandleChatMessage(packet);

    ASSERT_TRUE(received_message.has_value());
    EXPECT_EQ(received_message->channel, ChatChannel::kPrivate);
    EXPECT_EQ(received_message->content, "Secret message");
}

TEST_F(ChatHandlerTest, HandleChatMessage_TeamChannelMapsCorrectly) {
    flatbuffers::FlatBufferBuilder fbb(256);
    auto msg = mir2::proto::CreateChatMessageDirect(
        fbb,
        mir2::proto::ChatChannel::TEAM,
        /*from_id=*/333,
        /*from_name=*/"TeamMember",
        /*to_id=*/0,
        /*content=*/"Group up!",
        /*color=*/0x00FF0000,
        /*timestamp=*/2000);
    fbb.Finish(msg);

    auto handler = std::make_shared<ChatHandler>(MakeCallbacks());
    NetworkPacket packet = MakePacketFromBuilder(
        static_cast<uint16_t>(MsgId::kTeamChat), fbb);

    handler->HandleChatMessage(packet);

    ASSERT_TRUE(received_message.has_value());
    EXPECT_EQ(received_message->channel, ChatChannel::kTeam);
    EXPECT_EQ(received_message->content, "Group up!");
}

TEST_F(ChatHandlerTest, HandleChatMessage_AreaChannelMapsCorrectly) {
    flatbuffers::FlatBufferBuilder fbb(256);
    auto msg = mir2::proto::CreateChatMessageDirect(
        fbb,
        mir2::proto::ChatChannel::AREA,
        /*from_id=*/444,
        /*from_name=*/"AreaPlayer",
        /*to_id=*/0,
        /*content=*/"Local broadcast",
        /*color=*/0,
        /*timestamp=*/3000);
    fbb.Finish(msg);

    auto handler = std::make_shared<ChatHandler>(MakeCallbacks());
    NetworkPacket packet = MakePacketFromBuilder(
        static_cast<uint16_t>(MsgId::kAreaChat), fbb);

    handler->HandleChatMessage(packet);

    ASSERT_TRUE(received_message.has_value());
    EXPECT_EQ(received_message->channel, ChatChannel::kArea);
    EXPECT_EQ(received_message->content, "Local broadcast");
}

TEST_F(ChatHandlerTest, HandleChatMessage_WorldChannelMapsCorrectly) {
    flatbuffers::FlatBufferBuilder fbb(256);
    auto msg = mir2::proto::CreateChatMessageDirect(
        fbb,
        mir2::proto::ChatChannel::WORLD,
        /*from_id=*/555,
        /*from_name=*/"WorldShouter",
        /*to_id=*/0,
        /*content=*/"Selling sword!",
        /*color=*/0,
        /*timestamp=*/4000);
    fbb.Finish(msg);

    auto handler = std::make_shared<ChatHandler>(MakeCallbacks());
    // Use kPrivateChat msg_id but with WORLD channel in the payload.
    // The channel is determined by the FlatBuffers payload, not the msg_id.
    NetworkPacket packet = MakePacketFromBuilder(
        static_cast<uint16_t>(MsgId::kPrivateChat), fbb);

    handler->HandleChatMessage(packet);

    ASSERT_TRUE(received_message.has_value());
    EXPECT_EQ(received_message->channel, ChatChannel::kWorld);
}

TEST_F(ChatHandlerTest, HandleChatMessage_NullStringFieldsDefault) {
    // Build a ChatMessage with no from_name and no content (null strings).
    flatbuffers::FlatBufferBuilder fbb(128);
    auto msg = mir2::proto::CreateChatMessage(
        fbb,
        mir2::proto::ChatChannel::SYSTEM,
        /*from_id=*/999,
        /*from_name=*/0,  // null
        /*to_id=*/0,
        /*content=*/0,    // null
        /*color=*/0,
        /*timestamp=*/0);
    fbb.Finish(msg);

    auto handler = std::make_shared<ChatHandler>(MakeCallbacks());
    NetworkPacket packet = MakePacketFromBuilder(
        static_cast<uint16_t>(MsgId::kPrivateChat), fbb);

    handler->HandleChatMessage(packet);

    EXPECT_TRUE(parse_errors.empty());
    ASSERT_TRUE(received_message.has_value());
    EXPECT_EQ(received_message->from_name, "");
    EXPECT_EQ(received_message->content, "");
    EXPECT_EQ(received_message->channel, ChatChannel::kSystem);
}

TEST_F(ChatHandlerTest, HandleChatMessage_GarbagePayloadTriggersParseError) {
    NetworkPacket packet;
    packet.msg_id = static_cast<uint16_t>(MsgId::kGuildChat);
    packet.payload = {0x01, 0x02, 0x03};

    auto handler = std::make_shared<ChatHandler>(MakeCallbacks());
    handler->HandleChatMessage(packet);

    EXPECT_FALSE(parse_errors.empty());
    EXPECT_FALSE(received_message.has_value());
}

// ===========================================================================
// HandleSystemMessage tests
// ===========================================================================

TEST_F(ChatHandlerTest, HandleSystemMessage_EmptyPayloadTriggersParseError) {
    auto handler = std::make_shared<ChatHandler>(MakeCallbacks());
    NetworkPacket packet = MakeEmptyPacket(
        static_cast<uint16_t>(MsgId::kSystemMsg));

    handler->HandleSystemMessage(packet);

    ASSERT_EQ(parse_errors.size(), 1u);
    EXPECT_NE(parse_errors[0].find("Empty"), std::string::npos);
    EXPECT_TRUE(received_system_message.empty());
}

TEST_F(ChatHandlerTest, HandleSystemMessage_ValidServerNotice) {
    flatbuffers::FlatBufferBuilder fbb(256);
    auto notice = mir2::proto::CreateServerNoticeDirect(
        fbb,
        /*level=*/1,
        /*message=*/"Server will restart in 10 minutes.",
        /*timestamp=*/1700000500);
    fbb.Finish(notice);

    auto handler = std::make_shared<ChatHandler>(MakeCallbacks());
    NetworkPacket packet = MakePacketFromBuilder(
        static_cast<uint16_t>(MsgId::kSystemMsg), fbb);

    handler->HandleSystemMessage(packet);

    EXPECT_TRUE(parse_errors.empty());
    EXPECT_EQ(received_system_message, "Server will restart in 10 minutes.");
    EXPECT_EQ(received_system_timestamp, 1700000500);
}

TEST_F(ChatHandlerTest, HandleSystemMessage_NullMessageTriggersParseError) {
    // Build a ServerNotice with null message field.
    flatbuffers::FlatBufferBuilder fbb(128);
    auto notice = mir2::proto::CreateServerNotice(
        fbb,
        /*level=*/0,
        /*message=*/0,  // null
        /*timestamp=*/100);
    fbb.Finish(notice);

    auto handler = std::make_shared<ChatHandler>(MakeCallbacks());
    NetworkPacket packet = MakePacketFromBuilder(
        static_cast<uint16_t>(MsgId::kSystemMsg), fbb);

    handler->HandleSystemMessage(packet);

    // The implementation checks (!notice || !notice->message()) and reports
    // a parse error when the message field is null.
    ASSERT_EQ(parse_errors.size(), 1u);
    EXPECT_NE(parse_errors[0].find("parse failed"), std::string::npos);
    EXPECT_TRUE(received_system_message.empty());
}

TEST_F(ChatHandlerTest, HandleSystemMessage_GarbagePayloadTriggersParseError) {
    NetworkPacket packet;
    packet.msg_id = static_cast<uint16_t>(MsgId::kSystemMsg);
    packet.payload = {0xCA, 0xFE, 0xBA, 0xBE};

    auto handler = std::make_shared<ChatHandler>(MakeCallbacks());
    handler->HandleSystemMessage(packet);

    EXPECT_FALSE(parse_errors.empty());
    EXPECT_TRUE(received_system_message.empty());
}

// ===========================================================================
// Owner weak_ptr expiry guard tests
// ===========================================================================

TEST_F(ChatHandlerTest, OwnerExpired_HandleChatResponseDoesNotInvokeCallbacks) {
    auto owner = std::make_shared<int>(42);
    auto handler = std::make_shared<ChatHandler>(
        MakeCallbacks(std::weak_ptr<void>(owner)));

    // Build a valid ChatRsp.
    flatbuffers::FlatBufferBuilder fbb(128);
    auto rsp = mir2::proto::CreateChatRsp(fbb, mir2::proto::ErrorCode::ERR_OK);
    fbb.Finish(rsp);
    NetworkPacket packet = MakePacketFromBuilder(
        static_cast<uint16_t>(MsgId::kChatRsp), fbb);

    // Destroy the owner to expire the weak_ptr.
    owner.reset();

    handler->HandleChatResponse(packet);

    // No callbacks should fire -- including on_parse_error.
    EXPECT_FALSE(received_error_code.has_value());
    EXPECT_TRUE(parse_errors.empty());
}

TEST_F(ChatHandlerTest, OwnerExpired_HandleChatMessageDoesNotInvokeCallbacks) {
    auto owner = std::make_shared<int>(42);
    auto handler = std::make_shared<ChatHandler>(
        MakeCallbacks(std::weak_ptr<void>(owner)));

    flatbuffers::FlatBufferBuilder fbb(256);
    auto msg = mir2::proto::CreateChatMessageDirect(
        fbb,
        mir2::proto::ChatChannel::WORLD,
        /*from_id=*/1,
        /*from_name=*/"X",
        /*to_id=*/0,
        /*content=*/"Y",
        /*color=*/0,
        /*timestamp=*/0);
    fbb.Finish(msg);
    NetworkPacket packet = MakePacketFromBuilder(
        static_cast<uint16_t>(MsgId::kPrivateChat), fbb);

    owner.reset();

    handler->HandleChatMessage(packet);

    EXPECT_FALSE(received_message.has_value());
    EXPECT_TRUE(parse_errors.empty());
}

TEST_F(ChatHandlerTest, OwnerExpired_HandleSystemMessageDoesNotInvokeCallbacks) {
    auto owner = std::make_shared<int>(42);
    auto handler = std::make_shared<ChatHandler>(
        MakeCallbacks(std::weak_ptr<void>(owner)));

    flatbuffers::FlatBufferBuilder fbb(256);
    auto notice = mir2::proto::CreateServerNoticeDirect(
        fbb, /*level=*/0, /*message=*/"Test", /*timestamp=*/0);
    fbb.Finish(notice);
    NetworkPacket packet = MakePacketFromBuilder(
        static_cast<uint16_t>(MsgId::kSystemMsg), fbb);

    owner.reset();

    handler->HandleSystemMessage(packet);

    EXPECT_TRUE(received_system_message.empty());
    EXPECT_TRUE(parse_errors.empty());
}

TEST_F(ChatHandlerTest, OwnerAlive_CallbacksStillFire) {
    auto owner = std::make_shared<int>(42);
    auto handler = std::make_shared<ChatHandler>(
        MakeCallbacks(std::weak_ptr<void>(owner)));

    flatbuffers::FlatBufferBuilder fbb(128);
    auto rsp = mir2::proto::CreateChatRsp(fbb, mir2::proto::ErrorCode::ERR_OK);
    fbb.Finish(rsp);
    NetworkPacket packet = MakePacketFromBuilder(
        static_cast<uint16_t>(MsgId::kChatRsp), fbb);

    // Owner is still alive.
    handler->HandleChatResponse(packet);

    ASSERT_TRUE(received_error_code.has_value());
    EXPECT_EQ(*received_error_code, mir2::proto::ErrorCode::ERR_OK);
    EXPECT_TRUE(parse_errors.empty());
}

TEST_F(ChatHandlerTest, NoOwner_CallbacksAlwaysFire) {
    // When no owner is set, callbacks fire unconditionally.
    auto handler = std::make_shared<ChatHandler>(MakeCallbacks());

    flatbuffers::FlatBufferBuilder fbb(128);
    auto rsp = mir2::proto::CreateChatRsp(fbb, mir2::proto::ErrorCode::ERR_UNKNOWN);
    fbb.Finish(rsp);
    NetworkPacket packet = MakePacketFromBuilder(
        static_cast<uint16_t>(MsgId::kChatRsp), fbb);

    handler->HandleChatResponse(packet);

    ASSERT_TRUE(received_error_code.has_value());
    EXPECT_EQ(*received_error_code, mir2::proto::ErrorCode::ERR_UNKNOWN);
}

// ===========================================================================
// BindHandlers tests
// ===========================================================================

TEST_F(ChatHandlerTest, BindHandlers_RegistersAllExpectedMsgIds) {
    auto handler = std::make_shared<ChatHandler>(MakeCallbacks());
    MockNetworkManager manager;

    handler->BindHandlers(manager);

    EXPECT_EQ(manager.handler_count(), 6u);
    EXPECT_TRUE(manager.HasHandler(MsgId::kChatRsp));
    EXPECT_TRUE(manager.HasHandler(MsgId::kPrivateChat));
    EXPECT_TRUE(manager.HasHandler(MsgId::kGuildChat));
    EXPECT_TRUE(manager.HasHandler(MsgId::kTeamChat));
    EXPECT_TRUE(manager.HasHandler(MsgId::kAreaChat));
    EXPECT_TRUE(manager.HasHandler(MsgId::kSystemMsg));
}

TEST_F(ChatHandlerTest, BindHandlers_DispatchViaRegisteredHandler) {
    auto handler = std::make_shared<ChatHandler>(MakeCallbacks());
    MockNetworkManager manager;
    handler->BindHandlers(manager);

    // Dispatch a valid ChatRsp through the registered handler.
    flatbuffers::FlatBufferBuilder fbb(128);
    auto rsp = mir2::proto::CreateChatRsp(fbb, mir2::proto::ErrorCode::ERR_OK);
    fbb.Finish(rsp);
    NetworkPacket packet = MakePacketFromBuilder(
        static_cast<uint16_t>(MsgId::kChatRsp), fbb);

    manager.Dispatch(MsgId::kChatRsp, packet);

    ASSERT_TRUE(received_error_code.has_value());
    EXPECT_EQ(*received_error_code, mir2::proto::ErrorCode::ERR_OK);
}

TEST_F(ChatHandlerTest, BindHandlers_WeakSelfExpiryPreventsDispatch) {
    MockNetworkManager manager;

    {
        auto handler = std::make_shared<ChatHandler>(MakeCallbacks());
        handler->BindHandlers(manager);
        // handler goes out of scope and is destroyed.
    }

    // Build a valid ChatRsp.
    flatbuffers::FlatBufferBuilder fbb(128);
    auto rsp = mir2::proto::CreateChatRsp(fbb, mir2::proto::ErrorCode::ERR_OK);
    fbb.Finish(rsp);
    NetworkPacket packet = MakePacketFromBuilder(
        static_cast<uint16_t>(MsgId::kChatRsp), fbb);

    // The handler has been destroyed; dispatching should safely no-op because
    // the internal weak_from_this() lock fails.
    manager.Dispatch(MsgId::kChatRsp, packet);

    EXPECT_FALSE(received_error_code.has_value());
    EXPECT_TRUE(parse_errors.empty());
}

// ===========================================================================
// RegisterHandlers static method test
// ===========================================================================

TEST_F(ChatHandlerTest, RegisterHandlers_DoesNotRegisterAnything) {
    MockNetworkManager manager;
    ChatHandler::RegisterHandlers(manager);

    // The static RegisterHandlers is a no-op for ChatHandler; actual
    // registration goes through BindHandlers on a live instance.
    EXPECT_EQ(manager.handler_count(), 0u);
}

// ===========================================================================
// Callback-not-set safety tests
// ===========================================================================

TEST_F(ChatHandlerTest, NullCallbacks_HandleChatResponseDoesNotCrash) {
    ChatHandler::Callbacks cb;
    // Leave all callbacks as nullptr.
    auto handler = std::make_shared<ChatHandler>(std::move(cb));

    flatbuffers::FlatBufferBuilder fbb(128);
    auto rsp = mir2::proto::CreateChatRsp(fbb, mir2::proto::ErrorCode::ERR_OK);
    fbb.Finish(rsp);
    NetworkPacket packet = MakePacketFromBuilder(
        static_cast<uint16_t>(MsgId::kChatRsp), fbb);

    // Should not crash even with no callbacks set.
    handler->HandleChatResponse(packet);

    // Also test the empty payload path with null on_parse_error.
    handler->HandleChatResponse(MakeEmptyPacket(
        static_cast<uint16_t>(MsgId::kChatRsp)));
}

TEST_F(ChatHandlerTest, NullCallbacks_HandleChatMessageDoesNotCrash) {
    ChatHandler::Callbacks cb;
    auto handler = std::make_shared<ChatHandler>(std::move(cb));

    flatbuffers::FlatBufferBuilder fbb(256);
    auto msg = mir2::proto::CreateChatMessageDirect(
        fbb,
        mir2::proto::ChatChannel::WORLD,
        /*from_id=*/1,
        /*from_name=*/"A",
        /*to_id=*/0,
        /*content=*/"B",
        /*color=*/0,
        /*timestamp=*/0);
    fbb.Finish(msg);
    NetworkPacket packet = MakePacketFromBuilder(
        static_cast<uint16_t>(MsgId::kPrivateChat), fbb);

    handler->HandleChatMessage(packet);
    handler->HandleChatMessage(MakeEmptyPacket(
        static_cast<uint16_t>(MsgId::kPrivateChat)));
}

TEST_F(ChatHandlerTest, NullCallbacks_HandleSystemMessageDoesNotCrash) {
    ChatHandler::Callbacks cb;
    auto handler = std::make_shared<ChatHandler>(std::move(cb));

    flatbuffers::FlatBufferBuilder fbb(256);
    auto notice = mir2::proto::CreateServerNoticeDirect(
        fbb, /*level=*/0, /*message=*/"Test", /*timestamp=*/0);
    fbb.Finish(notice);
    NetworkPacket packet = MakePacketFromBuilder(
        static_cast<uint16_t>(MsgId::kSystemMsg), fbb);

    handler->HandleSystemMessage(packet);
    handler->HandleSystemMessage(MakeEmptyPacket(
        static_cast<uint16_t>(MsgId::kSystemMsg)));
}

} // namespace
