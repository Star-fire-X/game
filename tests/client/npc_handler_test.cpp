#include <gtest/gtest.h>

#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "client/handlers/npc_handler.h"
#include "common/enums.h"

namespace {

using mir2::client::ConnectionState;
using mir2::client::INetworkManager;
using mir2::common::ErrorCode;
using mir2::common::MsgId;
using mir2::common::NetworkPacket;
using mir2::core::EventDispatcher;
using mir2::core::IEventListener;
using mir2::game::handlers::NpcDialogEvent;
using mir2::game::handlers::NpcHandler;
using mir2::game::handlers::NpcInteractEvent;
using mir2::game::handlers::NpcMenuSelectEvent;
using mir2::game::handlers::NpcQuestAcceptEvent;
using mir2::game::handlers::NpcQuestCompleteEvent;
using mir2::game::handlers::NpcShopCloseEvent;
using mir2::game::handlers::NpcShopOpenEvent;
using mir2::game::handlers::NpcUiEventType;

NetworkPacket MakePacket(MsgId msg_id, const std::string& payload) {
    NetworkPacket packet;
    packet.msg_id = static_cast<uint16_t>(msg_id);
    packet.payload.assign(payload.begin(), payload.end());
    return packet;
}

class MockNetworkManager : public INetworkManager {
public:
    bool connect(const std::string& host, uint16_t port) override {
        host_ = host;
        port_ = port;
        return false;
    }

    void disconnect() override {}

    bool is_connected() const override {
        return false;
    }

    void send_message(mir2::common::MsgId msg_id, const std::vector<uint8_t>& payload) override {
        sent_msg_id_ = msg_id;
        sent_payload_ = payload;
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

    ErrorCode get_last_error() const override {
        return ErrorCode::SUCCESS;
    }

    void update() override {}

    bool HasHandler(MsgId msg_id) const {
        return handlers_.find(msg_id) != handlers_.end();
    }

    size_t handler_count() const {
        return handlers_.size();
    }

private:
    std::string host_;
    uint16_t port_ = 0;
    MsgId sent_msg_id_{MsgId::kNone};
    std::vector<uint8_t> sent_payload_;
    std::map<MsgId, HandlerFunc> handlers_;
    HandlerFunc default_handler_;
    EventCallback on_connect_;
    EventCallback on_disconnect_;
};

class CaptureUserEventListener : public IEventListener {
public:
    bool on_user_event(const SDL_UserEvent& event) override {
        ++user_event_calls;
        codes.push_back(event.code);

        const void* payload = event.data1;
        switch (static_cast<NpcUiEventType>(event.code)) {
            case NpcUiEventType::kInteractResponse:
                if (payload) {
                    interact_events.push_back(*static_cast<const NpcInteractEvent*>(payload));
                }
                break;
            case NpcUiEventType::kDialogShow:
                if (payload) {
                    dialog_events.push_back(*static_cast<const NpcDialogEvent*>(payload));
                }
                break;
            case NpcUiEventType::kMenuSelect:
                if (payload) {
                    menu_select_events.push_back(*static_cast<const NpcMenuSelectEvent*>(payload));
                }
                break;
            case NpcUiEventType::kShopOpen:
                if (payload) {
                    shop_open_events.push_back(*static_cast<const NpcShopOpenEvent*>(payload));
                }
                break;
            case NpcUiEventType::kShopClose:
                if (payload) {
                    shop_close_events.push_back(*static_cast<const NpcShopCloseEvent*>(payload));
                }
                break;
            case NpcUiEventType::kQuestAccept:
                if (payload) {
                    quest_accept_events.push_back(*static_cast<const NpcQuestAcceptEvent*>(payload));
                }
                break;
            case NpcUiEventType::kQuestComplete:
                if (payload) {
                    quest_complete_events.push_back(*static_cast<const NpcQuestCompleteEvent*>(payload));
                }
                break;
            default:
                break;
        }

        return false;
    }

    int user_event_calls = 0;
    std::vector<int> codes;
    std::vector<NpcInteractEvent> interact_events;
    std::vector<NpcDialogEvent> dialog_events;
    std::vector<NpcMenuSelectEvent> menu_select_events;
    std::vector<NpcShopOpenEvent> shop_open_events;
    std::vector<NpcShopCloseEvent> shop_close_events;
    std::vector<NpcQuestAcceptEvent> quest_accept_events;
    std::vector<NpcQuestCompleteEvent> quest_complete_events;
};

struct MockEventDispatcher {
    EventDispatcher dispatcher;
    CaptureUserEventListener listener;

    MockEventDispatcher() {
        dispatcher.add_listener(&listener);
    }
};

} // namespace

TEST(NpcHandlerTest, RegisterHandlersWithoutInstanceDoesNothing) {
    MockNetworkManager manager;
    NpcHandler::RegisterHandlers(manager);
    EXPECT_EQ(manager.handler_count(), 0u);
}

TEST(NpcHandlerTest, BindHandlersRegistersExpectedMessageIds) {
    NpcHandler handler(NpcHandler::Callbacks{});
    MockNetworkManager manager;

    handler.BindHandlers(manager);

    EXPECT_EQ(manager.handler_count(), 7u);
    EXPECT_TRUE(manager.HasHandler(MsgId::kNpcInteractRsp));
    EXPECT_TRUE(manager.HasHandler(MsgId::kNpcDialogShow));
    EXPECT_TRUE(manager.HasHandler(MsgId::kNpcMenuSelect));
    EXPECT_TRUE(manager.HasHandler(MsgId::kNpcShopOpen));
    EXPECT_TRUE(manager.HasHandler(MsgId::kNpcShopClose));
    EXPECT_TRUE(manager.HasHandler(MsgId::kNpcQuestAccept));
    EXPECT_TRUE(manager.HasHandler(MsgId::kNpcQuestComplete));
}

TEST(NpcHandlerTest, InitialStateDefaults) {
    NpcHandler handler(NpcHandler::Callbacks{});

    const auto& state = handler.state();
    EXPECT_EQ(state.npc_id, 0u);
    EXPECT_EQ(state.npc_type, 0u);
    EXPECT_EQ(state.last_interact_result, 0u);
    EXPECT_FALSE(state.dialog_open);
    EXPECT_TRUE(state.dialog_text.empty());
    EXPECT_TRUE(state.dialog_options.empty());
    EXPECT_FALSE(state.shop_open);
    EXPECT_EQ(state.store_id, 0u);
    EXPECT_TRUE(state.shop_items.empty());
    EXPECT_FALSE(state.last_menu_selection.has_value());
    EXPECT_FALSE(state.active_quest.has_value());
    EXPECT_FALSE(state.last_quest_complete.has_value());
}

TEST(NpcHandlerTest, DialogShowUpdatesStateAndDispatchesEvent) {
    MockEventDispatcher dispatcher;
    std::vector<std::string> errors;

    NpcHandler::Callbacks callbacks;
    callbacks.event_dispatcher = &dispatcher.dispatcher;
    callbacks.on_parse_error = [&](const std::string& msg) { errors.push_back(msg); };

    NpcHandler handler(callbacks);

    std::string payload =
        R"({"npc_id":101,"text":"Hello","has_menu":true,"options":["Shop","Leave"]})";

    handler.HandleNpcDialogShow(MakePacket(MsgId::kNpcDialogShow, payload));

    const auto& state = handler.state();
    EXPECT_EQ(state.npc_id, 101u);
    EXPECT_TRUE(state.dialog_open);
    EXPECT_EQ(state.dialog_text, "Hello");
    ASSERT_EQ(state.dialog_options.size(), 2u);
    EXPECT_EQ(state.dialog_options[0], "Shop");
    EXPECT_EQ(state.dialog_options[1], "Leave");

    EXPECT_TRUE(errors.empty());
    ASSERT_EQ(dispatcher.listener.dialog_events.size(), 1u);
    EXPECT_EQ(dispatcher.listener.codes.back(), static_cast<int>(NpcUiEventType::kDialogShow));
    const auto& event = dispatcher.listener.dialog_events.back();
    EXPECT_EQ(event.npc_id, 101u);
    EXPECT_EQ(event.text, "Hello");
    EXPECT_TRUE(event.has_menu);
    ASSERT_EQ(event.options.size(), 2u);
    EXPECT_EQ(event.options[0], "Shop");
    EXPECT_EQ(event.options[1], "Leave");
}

TEST(NpcHandlerTest, DialogShowOptionsForceHasMenu) {
    MockEventDispatcher dispatcher;

    NpcHandler::Callbacks callbacks;
    callbacks.event_dispatcher = &dispatcher.dispatcher;

    NpcHandler handler(callbacks);

    std::string payload =
        R"({"npc_id":200,"text":"Choose","has_menu":false,"options":["Yes"]})";

    handler.HandleNpcDialogShow(MakePacket(MsgId::kNpcDialogShow, payload));

    ASSERT_EQ(dispatcher.listener.dialog_events.size(), 1u);
    EXPECT_TRUE(dispatcher.listener.dialog_events.back().has_menu);
}

TEST(NpcHandlerTest, DialogShowMissingTextReportsParseError) {
    MockEventDispatcher dispatcher;
    std::vector<std::string> errors;

    NpcHandler::Callbacks callbacks;
    callbacks.event_dispatcher = &dispatcher.dispatcher;
    callbacks.on_parse_error = [&](const std::string& msg) { errors.push_back(msg); };

    NpcHandler handler(callbacks);

    std::string payload = R"({"npc_id":101})";
    handler.HandleNpcDialogShow(MakePacket(MsgId::kNpcDialogShow, payload));

    ASSERT_EQ(errors.size(), 1u);
    EXPECT_NE(errors[0].find("NpcDialogShow"), std::string::npos);
    EXPECT_NE(errors[0].find("text"), std::string::npos);
    EXPECT_TRUE(dispatcher.listener.dialog_events.empty());
    EXPECT_FALSE(handler.state().dialog_open);
}

TEST(NpcHandlerTest, MenuSelectUpdatesStateAndDispatchesEvent) {
    MockEventDispatcher dispatcher;

    NpcHandler::Callbacks callbacks;
    callbacks.event_dispatcher = &dispatcher.dispatcher;

    NpcHandler handler(callbacks);

    std::string payload = R"({"npc_id":300,"option_index":2})";
    handler.HandleNpcMenuSelect(MakePacket(MsgId::kNpcMenuSelect, payload));

    const auto& state = handler.state();
    EXPECT_EQ(state.npc_id, 300u);
    ASSERT_TRUE(state.last_menu_selection.has_value());
    EXPECT_EQ(state.last_menu_selection.value(), 2u);

    ASSERT_EQ(dispatcher.listener.menu_select_events.size(), 1u);
    const auto& event = dispatcher.listener.menu_select_events.back();
    EXPECT_EQ(event.npc_id, 300u);
    EXPECT_EQ(event.option_index, 2u);
}

TEST(NpcHandlerTest, MenuSelectInvalidOptionReportsError) {
    MockEventDispatcher dispatcher;
    std::vector<std::string> errors;

    NpcHandler::Callbacks callbacks;
    callbacks.event_dispatcher = &dispatcher.dispatcher;
    callbacks.on_parse_error = [&](const std::string& msg) { errors.push_back(msg); };

    NpcHandler handler(callbacks);

    std::string payload = R"({"npc_id":300,"option_index":999})";
    handler.HandleNpcMenuSelect(MakePacket(MsgId::kNpcMenuSelect, payload));

    ASSERT_EQ(errors.size(), 1u);
    EXPECT_NE(errors[0].find("NpcMenuSelect"), std::string::npos);
    EXPECT_FALSE(handler.state().last_menu_selection.has_value());
    EXPECT_TRUE(dispatcher.listener.menu_select_events.empty());
}

TEST(NpcHandlerTest, ShopOpenUpdatesStateAndDispatchesEvent) {
    MockEventDispatcher dispatcher;

    NpcHandler::Callbacks callbacks;
    callbacks.event_dispatcher = &dispatcher.dispatcher;

    NpcHandler handler(callbacks);

    std::string payload =
        R"({"npc_id":400,"store_id":7,"items":[{"item_id":1001,"price":50,"stock":3},{"item_id":1002,"price":75}]})";

    handler.HandleNpcShopOpen(MakePacket(MsgId::kNpcShopOpen, payload));

    const auto& state = handler.state();
    EXPECT_EQ(state.npc_id, 400u);
    EXPECT_TRUE(state.shop_open);
    EXPECT_EQ(state.store_id, 7u);
    ASSERT_EQ(state.shop_items.size(), 2u);
    EXPECT_EQ(state.shop_items[0].item_id, 1001u);
    EXPECT_EQ(state.shop_items[0].price, 50u);
    EXPECT_EQ(state.shop_items[0].stock, 3u);
    EXPECT_EQ(state.shop_items[1].item_id, 1002u);
    EXPECT_EQ(state.shop_items[1].price, 75u);

    ASSERT_EQ(dispatcher.listener.shop_open_events.size(), 1u);
    const auto& event = dispatcher.listener.shop_open_events.back();
    EXPECT_EQ(event.npc_id, 400u);
    EXPECT_EQ(event.store_id, 7u);
    ASSERT_EQ(event.items.size(), 2u);
    EXPECT_EQ(event.items[0].item_id, 1001u);
    EXPECT_EQ(event.items[1].item_id, 1002u);
}

TEST(NpcHandlerTest, ShopOpenClampsStockAndStoresItems) {
    MockEventDispatcher dispatcher;

    NpcHandler::Callbacks callbacks;
    callbacks.event_dispatcher = &dispatcher.dispatcher;

    NpcHandler handler(callbacks);

    std::string payload =
        R"({"npc_id":401,"store_id":9,"items":[{"item_id":2001,"price":10,"stock":70000}]})";

    handler.HandleNpcShopOpen(MakePacket(MsgId::kNpcShopOpen, payload));

    const auto& state = handler.state();
    ASSERT_EQ(state.shop_items.size(), 1u);
    EXPECT_EQ(state.shop_items[0].stock, 65535u);

    ASSERT_EQ(dispatcher.listener.shop_open_events.size(), 1u);
    EXPECT_EQ(dispatcher.listener.shop_open_events.back().items[0].stock, 65535u);
}

TEST(NpcHandlerTest, ShopOpenInvalidItemReportsErrorAndSkips) {
    MockEventDispatcher dispatcher;
    std::vector<std::string> errors;

    NpcHandler::Callbacks callbacks;
    callbacks.event_dispatcher = &dispatcher.dispatcher;
    callbacks.on_parse_error = [&](const std::string& msg) { errors.push_back(msg); };

    NpcHandler handler(callbacks);

    std::string payload =
        R"({"npc_id":402,"store_id":10,"items":[{"item_id":2001},{"item_id":2002,"price":20}]})";

    handler.HandleNpcShopOpen(MakePacket(MsgId::kNpcShopOpen, payload));

    EXPECT_FALSE(errors.empty());
    const auto& state = handler.state();
    ASSERT_EQ(state.shop_items.size(), 1u);
    EXPECT_EQ(state.shop_items[0].item_id, 2002u);
    ASSERT_EQ(dispatcher.listener.shop_open_events.size(), 1u);
    EXPECT_EQ(dispatcher.listener.shop_open_events.back().items.size(), 1u);
}

TEST(NpcHandlerTest, ShopCloseResetsStateAndDispatchesEvent) {
    MockEventDispatcher dispatcher;

    NpcHandler::Callbacks callbacks;
    callbacks.event_dispatcher = &dispatcher.dispatcher;

    NpcHandler handler(callbacks);

    std::string open_payload =
        R"({"npc_id":500,"store_id":3,"items":[{"item_id":3001,"price":99,"stock":1}]})";
    handler.HandleNpcShopOpen(MakePacket(MsgId::kNpcShopOpen, open_payload));

    std::string close_payload = R"({"npc_id":500})";
    handler.HandleNpcShopClose(MakePacket(MsgId::kNpcShopClose, close_payload));

    const auto& state = handler.state();
    EXPECT_FALSE(state.shop_open);
    EXPECT_EQ(state.store_id, 0u);
    EXPECT_TRUE(state.shop_items.empty());

    ASSERT_EQ(dispatcher.listener.shop_close_events.size(), 1u);
    EXPECT_EQ(dispatcher.listener.shop_close_events.back().npc_id, 500u);
}

TEST(NpcHandlerTest, QuestAcceptUpdatesStateAndDispatchesEvent) {
    MockEventDispatcher dispatcher;

    NpcHandler::Callbacks callbacks;
    callbacks.event_dispatcher = &dispatcher.dispatcher;

    NpcHandler handler(callbacks);

    std::string payload =
        R"({"npc_id":600,"quest_id":42,"quest_name":"Trial","quest_desc":"Defeat 5 slimes"})";
    handler.HandleNpcQuestAccept(MakePacket(MsgId::kNpcQuestAccept, payload));

    const auto& state = handler.state();
    ASSERT_TRUE(state.active_quest.has_value());
    EXPECT_EQ(state.active_quest->quest_id, 42u);
    EXPECT_EQ(state.active_quest->quest_name, "Trial");
    EXPECT_EQ(state.active_quest->quest_desc, "Defeat 5 slimes");

    ASSERT_EQ(dispatcher.listener.quest_accept_events.size(), 1u);
    const auto& event = dispatcher.listener.quest_accept_events.back();
    EXPECT_EQ(event.npc_id, 600u);
    EXPECT_EQ(event.quest_id, 42u);
    EXPECT_EQ(event.quest_name, "Trial");
}

TEST(NpcHandlerTest, QuestCompleteClearsActiveQuestAndDispatchesEvent) {
    MockEventDispatcher dispatcher;

    NpcHandler::Callbacks callbacks;
    callbacks.event_dispatcher = &dispatcher.dispatcher;

    NpcHandler handler(callbacks);

    std::string accept_payload =
        R"({"npc_id":601,"quest_id":7,"quest_name":"Hunt","quest_desc":"Hunt wolves"})";
    handler.HandleNpcQuestAccept(MakePacket(MsgId::kNpcQuestAccept, accept_payload));

    std::string complete_payload =
        R"({"npc_id":601,"quest_id":7,"rewards":[{"item_id":9001,"count":2}]})";
    handler.HandleNpcQuestComplete(MakePacket(MsgId::kNpcQuestComplete, complete_payload));

    const auto& state = handler.state();
    EXPECT_FALSE(state.active_quest.has_value());
    ASSERT_TRUE(state.last_quest_complete.has_value());
    EXPECT_EQ(state.last_quest_complete->quest_id, 7u);
    ASSERT_EQ(state.last_quest_complete->rewards.size(), 1u);
    EXPECT_EQ(state.last_quest_complete->rewards[0].item_id, 9001u);

    ASSERT_EQ(dispatcher.listener.quest_complete_events.size(), 1u);
    EXPECT_EQ(dispatcher.listener.quest_complete_events.back().quest_id, 7u);
}

TEST(NpcHandlerTest, QuestCompleteDefaultsRewardCountWhenMissing) {
    MockEventDispatcher dispatcher;

    NpcHandler::Callbacks callbacks;
    callbacks.event_dispatcher = &dispatcher.dispatcher;

    NpcHandler handler(callbacks);

    std::string payload =
        R"({"npc_id":602,"quest_id":8,"rewards":[{"item_id":9002}]})";
    handler.HandleNpcQuestComplete(MakePacket(MsgId::kNpcQuestComplete, payload));

    const auto& state = handler.state();
    ASSERT_TRUE(state.last_quest_complete.has_value());
    ASSERT_EQ(state.last_quest_complete->rewards.size(), 1u);
    EXPECT_EQ(state.last_quest_complete->rewards[0].count, 1u);

    ASSERT_EQ(dispatcher.listener.quest_complete_events.size(), 1u);
    EXPECT_EQ(dispatcher.listener.quest_complete_events.back().rewards[0].count, 1u);
}

TEST(NpcHandlerTest, InteractResponseFailureResetsDialogAndShopState) {
    MockEventDispatcher dispatcher;

    NpcHandler::Callbacks callbacks;
    callbacks.event_dispatcher = &dispatcher.dispatcher;

    NpcHandler handler(callbacks);

    std::string dialog_payload =
        R"({"npc_id":700,"text":"Hi","has_menu":true,"options":["Talk"]})";
    handler.HandleNpcDialogShow(MakePacket(MsgId::kNpcDialogShow, dialog_payload));

    std::string shop_payload =
        R"({"npc_id":700,"store_id":2,"items":[{"item_id":4001,"price":10,"stock":1}]})";
    handler.HandleNpcShopOpen(MakePacket(MsgId::kNpcShopOpen, shop_payload));

    std::string interact_payload =
        R"({"npc_id":700,"result":1,"npc_type":3,"npc_name":"Bob"})";
    handler.HandleNpcInteractResponse(MakePacket(MsgId::kNpcInteractRsp, interact_payload));

    const auto& state = handler.state();
    EXPECT_EQ(state.npc_id, 700u);
    EXPECT_EQ(state.last_interact_result, 1u);
    EXPECT_FALSE(state.dialog_open);
    EXPECT_TRUE(state.dialog_text.empty());
    EXPECT_TRUE(state.dialog_options.empty());
    EXPECT_FALSE(state.shop_open);
    EXPECT_EQ(state.store_id, 0u);
    EXPECT_TRUE(state.shop_items.empty());

    ASSERT_EQ(dispatcher.listener.interact_events.size(), 1u);
    EXPECT_EQ(dispatcher.listener.interact_events.back().npc_name, "Bob");
}

TEST(NpcHandlerTest, EmptyPayloadReportsParseError) {
    MockEventDispatcher dispatcher;
    std::vector<std::string> errors;

    NpcHandler::Callbacks callbacks;
    callbacks.event_dispatcher = &dispatcher.dispatcher;
    callbacks.on_parse_error = [&](const std::string& msg) { errors.push_back(msg); };

    NpcHandler handler(callbacks);

    NetworkPacket packet;
    packet.msg_id = static_cast<uint16_t>(MsgId::kNpcMenuSelect);

    handler.HandleNpcMenuSelect(packet);

    ASSERT_EQ(errors.size(), 1u);
    EXPECT_NE(errors[0].find("Empty payload"), std::string::npos);
    EXPECT_TRUE(dispatcher.listener.menu_select_events.empty());
}

