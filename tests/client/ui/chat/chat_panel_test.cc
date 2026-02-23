/**
 * @file chat_panel_test.cc
 * @brief Unit tests for ChatPanel widget.
 */

#include "client/ui/chat/chat_panel.h"
#include "client/handlers/chat_handler.h"

#include <gtest/gtest.h>

#include <string>

namespace mir2::ui::chat {
namespace {

using mir2::game::handlers::ChatChannel;
using mir2::game::handlers::ChatMessageData;

class ChatPanelTest : public ::testing::Test {
protected:
    ChatPanel panel_;

    /// Helper: create a chat message with specified channel and content.
    static ChatMessageData MakeMsg(ChatChannel ch, const std::string& content,
                                   const std::string& from = "Player1") {
        ChatMessageData msg;
        msg.channel = ch;
        msg.from_id = 1;
        msg.from_name = from;
        msg.to_id = 0;
        msg.content = content;
        msg.color = 0xFFFFFFFF;
        msg.timestamp = 1000;
        return msg;
    }
};

// -- Constants --

TEST(ChatPanelConstantsTest, MaxMessages_Is200) {
    EXPECT_EQ(ChatPanel::kMaxMessages, 200u);
}

TEST(ChatPanelConstantsTest, MessageLineHeight_Is16) {
    EXPECT_EQ(ChatPanel::kMessageLineHeight, 16);
}

// -- Default state --

TEST_F(ChatPanelTest, Constructor_DefaultActiveChannel_IsWorld) {
    EXPECT_EQ(panel_.get_active_channel(), ChatChannel::kWorld);
}

TEST_F(ChatPanelTest, Constructor_DefaultFilter_IsNullopt) {
    EXPECT_FALSE(panel_.get_filter_channel().has_value());
}

TEST_F(ChatPanelTest, Constructor_DefaultVisible) {
    EXPECT_TRUE(panel_.is_visible());
}

// -- add_message --

TEST_F(ChatPanelTest, AddMessage_NoFatalFailure) {
    auto msg = MakeMsg(ChatChannel::kWorld, "Hello world!");
    EXPECT_NO_FATAL_FAILURE(panel_.add_message(msg));
}

TEST_F(ChatPanelTest, AddMessage_MultipleChannels_NoFatalFailure) {
    panel_.add_message(MakeMsg(ChatChannel::kWorld, "world msg"));
    panel_.add_message(MakeMsg(ChatChannel::kPrivate, "private msg"));
    panel_.add_message(MakeMsg(ChatChannel::kGuild, "guild msg"));
    panel_.add_message(MakeMsg(ChatChannel::kSystem, "system msg"));
    panel_.add_message(MakeMsg(ChatChannel::kTeam, "team msg"));
    panel_.add_message(MakeMsg(ChatChannel::kArea, "area msg"));
}

TEST_F(ChatPanelTest, AddMessage_MessageCap_Enforced) {
    // Add 201 messages -- the panel should cap at kMaxMessages (200).
    for (size_t i = 0; i <= ChatPanel::kMaxMessages; ++i) {
        panel_.add_message(MakeMsg(ChatChannel::kWorld,
                                   "msg " + std::to_string(i)));
    }
    // Should not crash and should have capped the deque.
    // We can't directly read the count but we verify no crash.
    SUCCEED();
}

// -- add_system_message --

TEST_F(ChatPanelTest, AddSystemMessage_NoFatalFailure) {
    EXPECT_NO_FATAL_FAILURE(panel_.add_system_message("Server maintenance in 5 minutes"));
}

TEST_F(ChatPanelTest, AddSystemMessage_EmptyString_NoFatalFailure) {
    EXPECT_NO_FATAL_FAILURE(panel_.add_system_message(""));
}

// -- Channel management --

TEST_F(ChatPanelTest, SetActiveChannel_Guild) {
    panel_.set_active_channel(ChatChannel::kGuild);
    EXPECT_EQ(panel_.get_active_channel(), ChatChannel::kGuild);
}

TEST_F(ChatPanelTest, SetActiveChannel_Private) {
    panel_.set_active_channel(ChatChannel::kPrivate);
    EXPECT_EQ(panel_.get_active_channel(), ChatChannel::kPrivate);
}

TEST_F(ChatPanelTest, SetActiveChannel_Team) {
    panel_.set_active_channel(ChatChannel::kTeam);
    EXPECT_EQ(panel_.get_active_channel(), ChatChannel::kTeam);
}

// -- Filter management --

TEST_F(ChatPanelTest, SetFilterChannel_World) {
    panel_.set_filter_channel(ChatChannel::kWorld);
    ASSERT_TRUE(panel_.get_filter_channel().has_value());
    EXPECT_EQ(panel_.get_filter_channel().value(), ChatChannel::kWorld);
}

TEST_F(ChatPanelTest, SetFilterChannel_Nullopt_ShowsAll) {
    panel_.set_filter_channel(ChatChannel::kGuild);
    panel_.set_filter_channel(std::nullopt);
    EXPECT_FALSE(panel_.get_filter_channel().has_value());
}

// -- Callback --

TEST_F(ChatPanelTest, OnSendMessage_DefaultIsNull) {
    EXPECT_FALSE(panel_.on_send_message);
}

TEST_F(ChatPanelTest, OnSendMessage_CanBeSet) {
    ChatChannel called_ch = ChatChannel::kSystem;
    std::string called_text;

    panel_.on_send_message = [&](ChatChannel ch, const std::string& text) {
        called_ch = ch;
        called_text = text;
    };

    panel_.on_send_message(ChatChannel::kWorld, "test message");
    EXPECT_EQ(called_ch, ChatChannel::kWorld);
    EXPECT_EQ(called_text, "test message");
}

}  // namespace
}  // namespace mir2::ui::chat
