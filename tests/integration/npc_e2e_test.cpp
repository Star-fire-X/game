#include <gtest/gtest.h>

#include <entt/entt.hpp>
#include <nlohmann/json.hpp>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "client/handlers/npc_handler.h"
#include "client/network/i_network_manager.h"
#include "common/enums.h"
#include "common/protocol/npc_message_codec.h"
#include "core/event_dispatcher.h"
#include "ecs/components/character_components.h"
#include "ecs/components/npc_component.h"
#include "ecs/components/transform_component.h"
#include "ecs/events/npc_events.h"
#include "ecs/world.h"
#include "game/npc/npc_types.h"

#define private public
#include "ecs/event_bus.h"
#include "game/npc/lua_script_engine.h"
#undef private

namespace {

using json = nlohmann::json;

using mir2::client::ConnectionState;
using mir2::client::INetworkManager;
using mir2::common::ErrorCode;
using mir2::common::MsgId;
using mir2::common::NetworkPacket;
using mir2::core::EventDispatcher;
using mir2::core::IEventListener;
using mir2::ecs::CharacterAttributesComponent;
using mir2::ecs::CharacterIdentityComponent;
using mir2::ecs::CharacterStateComponent;
using mir2::ecs::EventBus;
using mir2::ecs::NpcAIComponent;
using mir2::ecs::NpcScriptComponent;
using mir2::ecs::NpcStateComponent;
using mir2::ecs::TransformComponent;
using mir2::ecs::events::NpcDialogEvent;
using mir2::ecs::events::NpcMenuEvent;
using mir2::ecs::events::NpcOpenMerchantEvent;
using mir2::ecs::events::NpcStartQuestEvent;
using mir2::ecs::events::NpcCompleteQuestEvent;
using mir2::ecs::events::NpcTeleportEvent;
using mir2::game::handlers::NpcHandler;
using mir2::game::handlers::NpcInteractEvent;
using mir2::game::handlers::NpcMenuSelectEvent;
using mir2::game::handlers::NpcQuestAcceptEvent;
using mir2::game::handlers::NpcQuestCompleteEvent;
using mir2::game::handlers::NpcQuestReward;
using mir2::game::handlers::NpcShopCloseEvent;
using mir2::game::handlers::NpcShopOpenEvent;
using ClientNpcDialogEvent = mir2::game::handlers::NpcDialogEvent;
using ClientNpcState = mir2::game::handlers::NpcState;
using mir2::game::handlers::NpcUiEventType;
using mir2::game::npc::LuaScriptContext;
using mir2::game::npc::LuaScriptEngine;
using mir2::game::npc::NpcScriptContext;
using mir2::game::npc::NpcState;
using mir2::game::npc::NpcType;

constexpr uint32_t kDefaultMapId = 1;
constexpr int32_t kNpcX = 10;
constexpr int32_t kNpcY = 12;
constexpr uint32_t kStoreId = 701;
constexpr uint32_t kQuestId = 9001;
constexpr int32_t kTeleportMapId = 2;
constexpr int32_t kTeleportX = 50;
constexpr int32_t kTeleportY = 60;

constexpr uint8_t kMenuBuy = 0;
constexpr uint8_t kMenuQuestAccept = 1;
constexpr uint8_t kMenuQuestComplete = 2;
constexpr uint8_t kMenuTeleport = 3;

const char* kDialogScript = R"lua(
local payload = ...
if payload == nil then
  return
end
npc.say(payload.ctx, "Welcome")
)lua";

const char* kMenuScript = R"lua(
local payload = ...
if payload == nil then
  return
end
npc.say(payload.ctx, "Choose a service")
npc.showMenu(payload.ctx, {"Buy Items", "Accept Quest", "Complete Quest", "Teleport"})
)lua";

struct TempLuaScriptFile {
    explicit TempLuaScriptFile(const std::string& contents) {
        path = MakeTempScriptPath();
        std::ofstream output(path, std::ios::trunc);
        output << contents;
        output.flush();
    }

    ~TempLuaScriptFile() {
        std::error_code ec;
        std::filesystem::remove(path, ec);
    }

    static std::filesystem::path MakeTempScriptPath() {
        static std::atomic<int> counter{0};
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        return std::filesystem::temp_directory_path() /
               ("npc_e2e_test_" + std::to_string(stamp) + "_" +
                std::to_string(counter.fetch_add(1)) + ".lua");
    }

    std::filesystem::path path;
};

class TestNetworkManager : public INetworkManager {
public:
    using SendCallback = std::function<void(MsgId, const std::vector<uint8_t>&)>;

    bool connect(const std::string& host, uint16_t port) override {
        host_ = host;
        port_ = port;
        return false;
    }

    void disconnect() override {}

    bool is_connected() const override {
        return true;
    }

    void send_message(mir2::common::MsgId msg_id,
                      const std::vector<uint8_t>& payload) override {
        last_sent_id_ = msg_id;
        last_sent_payload_ = payload;
        if (on_send) {
            on_send(msg_id, payload);
        }
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
        return ConnectionState::CONNECTED;
    }

    ErrorCode get_last_error() const override {
        return ErrorCode::SUCCESS;
    }

    void update() override {}

    void DispatchIncoming(MsgId msg_id, const std::vector<uint8_t>& payload) {
        NetworkPacket packet;
        packet.msg_id = static_cast<uint16_t>(msg_id);
        packet.payload = payload;
        auto it = handlers_.find(msg_id);
        if (it != handlers_.end()) {
            it->second(packet);
        } else if (default_handler_) {
            default_handler_(packet);
        }
    }

    SendCallback on_send;

    MsgId last_sent_id_{MsgId::kNone};
    std::vector<uint8_t> last_sent_payload_;

private:
    std::string host_;
    uint16_t port_ = 0;
    std::map<MsgId, HandlerFunc> handlers_;
    HandlerFunc default_handler_;
    EventCallback on_connect_;
    EventCallback on_disconnect_;
};

class CaptureNpcUiListener : public IEventListener {
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
                    dialog_events.push_back(*static_cast<const ClientNpcDialogEvent*>(payload));
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
    std::vector<ClientNpcDialogEvent> dialog_events;
    std::vector<NpcMenuSelectEvent> menu_select_events;
    std::vector<NpcShopOpenEvent> shop_open_events;
    std::vector<NpcShopCloseEvent> shop_close_events;
    std::vector<NpcQuestAcceptEvent> quest_accept_events;
    std::vector<NpcQuestCompleteEvent> quest_complete_events;
};

struct ShopItemData {
    uint32_t item_id = 0;
    uint32_t price = 0;
    uint16_t stock = 0;
};

struct QuestData {
    uint32_t quest_id = 0;
    std::string quest_name;
    std::string quest_desc;
    std::vector<NpcQuestReward> rewards;
};

struct MenuAction {
    enum class Type {
        kNone,
        kShop,
        kQuestAccept,
        kQuestComplete,
        kTeleport
    };

    static MenuAction Shop(uint32_t store_id) {
        MenuAction action;
        action.type = Type::kShop;
        action.store_id = store_id;
        return action;
    }

    static MenuAction QuestAccept(uint32_t quest_id) {
        MenuAction action;
        action.type = Type::kQuestAccept;
        action.quest_id = quest_id;
        return action;
    }

    static MenuAction QuestComplete(uint32_t quest_id) {
        MenuAction action;
        action.type = Type::kQuestComplete;
        action.quest_id = quest_id;
        return action;
    }

    static MenuAction Teleport(int32_t map_id, int32_t x, int32_t y) {
        MenuAction action;
        action.type = Type::kTeleport;
        action.map_id = map_id;
        action.x = x;
        action.y = y;
        return action;
    }

    Type type = Type::kNone;
    uint32_t store_id = 0;
    uint32_t quest_id = 0;
    int32_t map_id = 0;
    int32_t x = 0;
    int32_t y = 0;
};

class ServerToClientBridge {
public:
    ServerToClientBridge(EventBus& event_bus, TestNetworkManager& network)
        : event_bus_(event_bus), network_(network) {
        dialog_connection_ = event_bus_.dispatcher_.sink<NpcDialogEvent>()
            .connect<&ServerToClientBridge::OnDialog>(this);
        menu_connection_ = event_bus_.dispatcher_.sink<NpcMenuEvent>()
            .connect<&ServerToClientBridge::OnMenu>(this);
        merchant_connection_ = event_bus_.dispatcher_.sink<NpcOpenMerchantEvent>()
            .connect<&ServerToClientBridge::OnOpenMerchant>(this);
        quest_accept_connection_ = event_bus_.dispatcher_.sink<NpcStartQuestEvent>()
            .connect<&ServerToClientBridge::OnQuestAccept>(this);
        quest_complete_connection_ = event_bus_.dispatcher_.sink<NpcCompleteQuestEvent>()
            .connect<&ServerToClientBridge::OnQuestComplete>(this);
    }

    void SetShopItems(uint32_t store_id, std::vector<ShopItemData> items) {
        shop_items_[store_id] = std::move(items);
    }

    void SetQuestData(QuestData quest) {
        quest_data_[quest.quest_id] = std::move(quest);
    }

    void SendInteractResponse(uint64_t npc_id,
                              uint8_t result,
                              uint8_t npc_type,
                              const std::string& npc_name) {
        json j;
        j["npc_id"] = npc_id;
        j["result"] = result;
        j["npc_type"] = npc_type;
        j["npc_name"] = npc_name;
        Dispatch(MsgId::kNpcInteractRsp, j);
    }

    void SendMenuSelectAck(uint64_t npc_id, uint8_t option_index) {
        json j;
        j["npc_id"] = npc_id;
        j["option_index"] = option_index;
        Dispatch(MsgId::kNpcMenuSelect, j);
    }

    void SendShopClose(uint64_t npc_id) {
        json j;
        j["npc_id"] = npc_id;
        Dispatch(MsgId::kNpcShopClose, j);
    }

private:
    void Dispatch(MsgId msg_id, const json& payload) {
        const auto dumped = payload.dump();
        std::vector<uint8_t> bytes(dumped.begin(), dumped.end());
        network_.DispatchIncoming(msg_id, bytes);
    }

    void SendDialogShow(uint64_t npc_id,
                        const std::string& text,
                        const std::vector<std::string>& options) {
        json j;
        j["npc_id"] = npc_id;
        j["text"] = text;
        if (!options.empty()) {
            j["has_menu"] = true;
            j["options"] = options;
        }
        Dispatch(MsgId::kNpcDialogShow, j);
    }

    void OnDialog(NpcDialogEvent& event) {
        last_dialog_text_ = event.text;
        if (!pending_menu_options_.empty()) {
            SendDialogShow(event.npc_id, event.text, pending_menu_options_);
            pending_menu_options_.clear();
            return;
        }
        SendDialogShow(event.npc_id, event.text, {});
    }

    void OnMenu(NpcMenuEvent& event) {
        pending_menu_options_ = event.options;
        if (!last_dialog_text_.empty()) {
            SendDialogShow(event.npc_id, last_dialog_text_, pending_menu_options_);
            pending_menu_options_.clear();
        }
    }

    void OnOpenMerchant(NpcOpenMerchantEvent& event) {
        json j;
        j["npc_id"] = event.npc_id;
        j["store_id"] = event.store_id;
        json items_json = json::array();

        auto it = shop_items_.find(event.store_id);
        if (it != shop_items_.end()) {
            for (const auto& item : it->second) {
                json entry;
                entry["item_id"] = item.item_id;
                entry["price"] = item.price;
                if (item.stock > 0) {
                    entry["stock"] = item.stock;
                }
                items_json.push_back(entry);
            }
        }

        j["items"] = items_json;
        Dispatch(MsgId::kNpcShopOpen, j);
    }

    void OnQuestAccept(NpcStartQuestEvent& event) {
        json j;
        j["npc_id"] = event.npc_id;
        j["quest_id"] = event.quest_id;

        auto it = quest_data_.find(event.quest_id);
        if (it != quest_data_.end()) {
            j["quest_name"] = it->second.quest_name;
            j["quest_desc"] = it->second.quest_desc;
        }

        Dispatch(MsgId::kNpcQuestAccept, j);
    }

    void OnQuestComplete(NpcCompleteQuestEvent& event) {
        json j;
        j["npc_id"] = event.npc_id;
        j["quest_id"] = event.quest_id;

        json rewards = json::array();
        auto it = quest_data_.find(event.quest_id);
        if (it != quest_data_.end()) {
            for (const auto& reward : it->second.rewards) {
                json entry;
                entry["item_id"] = reward.item_id;
                entry["count"] = reward.count;
                rewards.push_back(entry);
            }
        }

        j["rewards"] = rewards;
        Dispatch(MsgId::kNpcQuestComplete, j);
    }

    EventBus& event_bus_;
    TestNetworkManager& network_;
    std::unordered_map<uint32_t, std::vector<ShopItemData>> shop_items_;
    std::unordered_map<uint32_t, QuestData> quest_data_;
    std::string last_dialog_text_;
    std::vector<std::string> pending_menu_options_;
    entt::scoped_connection dialog_connection_;
    entt::scoped_connection menu_connection_;
    entt::scoped_connection merchant_connection_;
    entt::scoped_connection quest_accept_connection_;
    entt::scoped_connection quest_complete_connection_;
};

class NpcE2EContext {
public:
    NpcE2EContext()
        : world(1000),
          registry(world.Registry()),
          event_bus(world.GetEventBus()),
          lua_engine(&event_bus) {
        lua_engine.Initialize();

        dispatcher.add_listener(&ui_listener);
        NpcHandler::Callbacks callbacks;
        callbacks.event_dispatcher = &dispatcher;
        callbacks.on_parse_error = [&](const std::string& msg) { parse_errors.push_back(msg); };
        npc_handler = std::make_unique<NpcHandler>(std::move(callbacks));
        npc_handler->BindHandlers(network);

        player = CreatePlayer();
        npc_entity = CreateNpc();
        npc_id = static_cast<uint64_t>(entt::to_integral(npc_entity));

        bridge = std::make_unique<ServerToClientBridge>(event_bus, network);

        network.on_send = [&](MsgId msg_id, const std::vector<uint8_t>& payload) {
            HandleClientMessage(msg_id, payload);
        };

        teleport_connection_ = event_bus.dispatcher_.sink<NpcTeleportEvent>()
            .connect<&NpcE2EContext::OnTeleport>(this);
    }

    void ConfigureNpc(const std::string& name, NpcType type) {
        npc_name = name;
        npc_type = type;
    }

    void LoadScript(const std::string& contents) {
        script_file = std::make_unique<TempLuaScriptFile>(contents);
        script_path = script_file->path.string();
        ASSERT_TRUE(lua_engine.LoadScript(script_path));
    }

    void SetMenuAction(uint8_t option_index, MenuAction action) {
        menu_actions_[option_index] = action;
    }

    void SetShopItems(uint32_t store_id, std::vector<ShopItemData> items) {
        bridge->SetShopItems(store_id, std::move(items));
    }

    void SetQuestData(QuestData quest) {
        bridge->SetQuestData(std::move(quest));
    }

    void ClientInteractNpc() {
        mir2::common::NpcInteractReq request;
        request.npc_id = npc_id;
        request.player_id = registry.get<CharacterIdentityComponent>(player).id;
        mir2::common::MessageCodecStatus status = mir2::common::MessageCodecStatus::kOk;
        const auto payload = mir2::common::EncodeNpcInteractReq(request, &status);
        ASSERT_EQ(status, mir2::common::MessageCodecStatus::kOk);
        network.send_message(MsgId::kNpcInteractReq, payload);
    }

    void ClientSelectMenu(uint8_t option_index) {
        mir2::common::NpcMenuSelectReq request;
        request.npc_id = npc_id;
        request.option_index = option_index;
        mir2::common::MessageCodecStatus status = mir2::common::MessageCodecStatus::kOk;
        const auto payload = mir2::common::EncodeNpcMenuSelect(request, &status);
        ASSERT_EQ(status, mir2::common::MessageCodecStatus::kOk);
        network.send_message(MsgId::kNpcMenuSelect, payload);
    }

    const ClientNpcState& client_state() const {
        return npc_handler->state();
    }

    const NpcStateComponent& npc_state_component() const {
        return registry.get<NpcStateComponent>(npc_entity);
    }

    void SendShopClose() {
        bridge->SendShopClose(npc_id);
    }

    std::optional<uint8_t> last_menu_selection;
    std::optional<NpcTeleportEvent> last_teleport;
    std::vector<std::string> parse_errors;

    mir2::ecs::World world;
    entt::registry& registry;
    EventBus& event_bus;
    entt::entity player{entt::null};
    entt::entity npc_entity{entt::null};
    uint64_t npc_id = 0;
    std::string npc_name = "Guide";
    NpcType npc_type = NpcType::kQuest;
    LuaScriptEngine lua_engine;
    TestNetworkManager network;
    EventDispatcher dispatcher;
    CaptureNpcUiListener ui_listener;
    std::unique_ptr<NpcHandler> npc_handler;
    std::unique_ptr<ServerToClientBridge> bridge;

private:
    entt::entity CreatePlayer() {
        const auto entity = registry.create();

        auto& identity = registry.emplace<CharacterIdentityComponent>(entity);
        identity.id = 5001;
        identity.name = "Hero";
        identity.char_class = mir2::common::CharacterClass::WARRIOR;
        identity.gender = mir2::common::Gender::MALE;

        auto& attributes = registry.emplace<CharacterAttributesComponent>(entity);
        attributes.level = 1;
        attributes.experience = 0;
        attributes.max_hp = 100;
        attributes.hp = 100;
        attributes.gold = 500;

        auto& state = registry.emplace<CharacterStateComponent>(entity);
        state.map_id = kDefaultMapId;
        state.position.x = 5;
        state.position.y = 6;

        return entity;
    }

    entt::entity CreateNpc() {
        const auto entity = registry.create();

        auto& transform = registry.emplace<TransformComponent>(entity);
        transform.map_id = kDefaultMapId;
        transform.position.x = kNpcX;
        transform.position.y = kNpcY;

        auto& state = registry.emplace<NpcStateComponent>(entity);
        state.current_state = NpcState::Idle;
        state.previous_state = NpcState::Idle;

        registry.emplace<NpcAIComponent>(entity);

        auto& script = registry.emplace<NpcScriptComponent>(entity);
        script.script_id = "npc_e2e";

        return entity;
    }

    void HandleClientMessage(MsgId msg_id, const std::vector<uint8_t>& payload) {
        json j;
        if (!ParsePayload(payload, &j)) {
            return;
        }

        if (msg_id == MsgId::kNpcInteractReq) {
            HandleInteractRequest(j);
        } else if (msg_id == MsgId::kNpcMenuSelect) {
            HandleMenuSelectRequest(j);
        }
    }

    void HandleInteractRequest(const json& j) {
        const uint64_t requested_npc = j.value("npc_id", 0ull);
        if (requested_npc == 0 || requested_npc != npc_id) {
            return;
        }

        auto* npc_ai = world.GetNpcAISystem();
        ASSERT_NE(npc_ai, nullptr);
        npc_ai->OnPlayerInteract(npc_entity, player);
        world.Update(0.1f);

        bridge->SendInteractResponse(npc_id, 0u, static_cast<uint8_t>(npc_type), npc_name);

        ExecuteLuaScript();
    }

    void HandleMenuSelectRequest(const json& j) {
        const uint64_t requested_npc = j.value("npc_id", 0ull);
        if (requested_npc == 0 || requested_npc != npc_id) {
            return;
        }
        const uint8_t option_index = static_cast<uint8_t>(j.value("option_index", 0));

        last_menu_selection = option_index;
        bridge->SendMenuSelectAck(npc_id, option_index);

        auto it = menu_actions_.find(option_index);
        if (it == menu_actions_.end()) {
            return;
        }

        const auto& action = it->second;
        switch (action.type) {
            case MenuAction::Type::kShop: {
                NpcOpenMerchantEvent event;
                event.player = player;
                event.npc_id = npc_id;
                event.store_id = action.store_id;
                event_bus.Publish(event);
                break;
            }
            case MenuAction::Type::kQuestAccept: {
                NpcStartQuestEvent event;
                event.player = player;
                event.npc_id = npc_id;
                event.quest_id = action.quest_id;
                event_bus.Publish(event);
                break;
            }
            case MenuAction::Type::kQuestComplete: {
                NpcCompleteQuestEvent event;
                event.player = player;
                event.npc_id = npc_id;
                event.quest_id = action.quest_id;
                event_bus.Publish(event);
                break;
            }
            case MenuAction::Type::kTeleport: {
                NpcTeleportEvent event;
                event.player = player;
                event.npc_id = npc_id;
                event.map_id = action.map_id;
                event.x = action.x;
                event.y = action.y;
                event_bus.Publish(event);
                break;
            }
            default:
                break;
        }
    }

    void ExecuteLuaScript() {
        if (script_path.empty()) {
            return;
        }

        NpcScriptContext npc_context;
        npc_context.player = player;
        npc_context.npc_id = npc_id;
        npc_context.npc_type = npc_type;
        npc_context.npc_name = npc_name;
        npc_context.event_bus = &event_bus;

        LuaScriptContext lua_context;
        lua_context.data = lua_engine.lua_->create_table();
        lua_context.data["ctx"] = &npc_context;

        ASSERT_TRUE(lua_engine.ExecuteScript(script_path, lua_context));
    }

    bool ParsePayload(const std::vector<uint8_t>& payload, json* out) {
        if (!out || payload.empty()) {
            return false;
        }
        try {
            *out = json::parse(payload.begin(), payload.end());
        } catch (const json::exception&) {
            return false;
        }
        return out->is_object();
    }

    void OnTeleport(NpcTeleportEvent& event) {
        auto* state = registry.try_get<CharacterStateComponent>(event.player);
        if (!state) {
            return;
        }
        state->map_id = static_cast<uint32_t>(event.map_id);
        state->position.x = event.x;
        state->position.y = event.y;
        last_teleport = event;
    }

    std::unique_ptr<TempLuaScriptFile> script_file;
    std::string script_path;
    std::unordered_map<uint8_t, MenuAction> menu_actions_;
    entt::scoped_connection teleport_connection_;
};

TEST(NpcE2EIntegrationTest, DialogFlowShowsDialogOnClient) {
    NpcE2EContext context;
    context.ConfigureNpc("Innkeeper", NpcType::kQuest);
    context.LoadScript(kDialogScript);

    context.ClientInteractNpc();

    const auto& npc_state = context.npc_state_component();
    EXPECT_EQ(npc_state.current_state, NpcState::Interact);
    EXPECT_EQ(npc_state.interacting_player, context.player);

    const auto& state = context.client_state();
    EXPECT_TRUE(state.dialog_open);
    EXPECT_EQ(state.dialog_text, "Welcome");
    EXPECT_TRUE(state.dialog_options.empty());
    ASSERT_FALSE(context.ui_listener.dialog_events.empty());
    EXPECT_EQ(context.ui_listener.dialog_events.back().text, "Welcome");
}

TEST(NpcE2EIntegrationTest, DialogFlowIncludesMenuOptions) {
    NpcE2EContext context;
    context.ConfigureNpc("Guide", NpcType::kQuest);
    context.LoadScript(kMenuScript);

    context.ClientInteractNpc();

    const auto& state = context.client_state();
    EXPECT_TRUE(state.dialog_open);
    ASSERT_EQ(state.dialog_options.size(), 4u);
    EXPECT_EQ(state.dialog_options[0], "Buy Items");
    EXPECT_EQ(state.dialog_options[1], "Accept Quest");

    ASSERT_FALSE(context.ui_listener.dialog_events.empty());
    const auto& dialog_event = context.ui_listener.dialog_events.back();
    EXPECT_TRUE(dialog_event.has_menu);
    EXPECT_EQ(dialog_event.options.size(), 4u);
}

TEST(NpcE2EIntegrationTest, MenuSelectionRoundTripUpdatesClientAndServer) {
    NpcE2EContext context;
    context.ConfigureNpc("Guide", NpcType::kQuest);
    context.LoadScript(kMenuScript);

    context.ClientInteractNpc();
    context.ClientSelectMenu(kMenuQuestAccept);

    ASSERT_TRUE(context.last_menu_selection.has_value());
    EXPECT_EQ(*context.last_menu_selection, kMenuQuestAccept);

    const auto& state = context.client_state();
    ASSERT_TRUE(state.last_menu_selection.has_value());
    EXPECT_EQ(state.last_menu_selection.value(), kMenuQuestAccept);
    ASSERT_FALSE(context.ui_listener.menu_select_events.empty());
    EXPECT_EQ(context.ui_listener.menu_select_events.back().option_index, kMenuQuestAccept);
}

TEST(NpcE2EIntegrationTest, ShopOpenFromMenuSelection) {
    NpcE2EContext context;
    context.ConfigureNpc("Merchant", NpcType::kMerchant);
    context.LoadScript(kMenuScript);
    context.SetMenuAction(kMenuBuy, MenuAction::Shop(kStoreId));

    std::vector<ShopItemData> items = {
        {101, 50, 10},
        {102, 75, 0}
    };
    context.SetShopItems(kStoreId, items);

    context.ClientInteractNpc();
    context.ClientSelectMenu(kMenuBuy);

    const auto& state = context.client_state();
    EXPECT_TRUE(state.shop_open);
    EXPECT_EQ(state.store_id, kStoreId);
    ASSERT_EQ(state.shop_items.size(), items.size());
}

TEST(NpcE2EIntegrationTest, ShopItemListMatchesServer) {
    NpcE2EContext context;
    context.ConfigureNpc("Merchant", NpcType::kMerchant);
    context.LoadScript(kMenuScript);
    context.SetMenuAction(kMenuBuy, MenuAction::Shop(kStoreId));

    std::vector<ShopItemData> items = {
        {201, 120, 5},
        {202, 15, 99}
    };
    context.SetShopItems(kStoreId, items);

    context.ClientInteractNpc();
    context.ClientSelectMenu(kMenuBuy);

    const auto& state = context.client_state();
    ASSERT_EQ(state.shop_items.size(), items.size());
    EXPECT_EQ(state.shop_items[0].item_id, items[0].item_id);
    EXPECT_EQ(state.shop_items[0].price, items[0].price);
    EXPECT_EQ(state.shop_items[0].stock, items[0].stock);
    EXPECT_EQ(state.shop_items[1].item_id, items[1].item_id);
    EXPECT_EQ(state.shop_items[1].price, items[1].price);
    EXPECT_EQ(state.shop_items[1].stock, items[1].stock);
}

TEST(NpcE2EIntegrationTest, ShopCloseClearsClientState) {
    NpcE2EContext context;
    context.ConfigureNpc("Merchant", NpcType::kMerchant);
    context.LoadScript(kMenuScript);
    context.SetMenuAction(kMenuBuy, MenuAction::Shop(kStoreId));

    std::vector<ShopItemData> items = {
        {301, 55, 3}
    };
    context.SetShopItems(kStoreId, items);

    context.ClientInteractNpc();
    context.ClientSelectMenu(kMenuBuy);

    context.SendShopClose();

    const auto& state = context.client_state();
    EXPECT_FALSE(state.shop_open);
    EXPECT_EQ(state.store_id, 0u);
    EXPECT_TRUE(state.shop_items.empty());
    ASSERT_FALSE(context.ui_listener.shop_close_events.empty());
    EXPECT_EQ(context.ui_listener.shop_close_events.back().npc_id, context.npc_id);
}

TEST(NpcE2EIntegrationTest, QuestAcceptFlowSetsClientState) {
    NpcE2EContext context;
    context.ConfigureNpc("Quest Giver", NpcType::kQuest);
    context.LoadScript(kMenuScript);
    context.SetMenuAction(kMenuQuestAccept, MenuAction::QuestAccept(kQuestId));

    QuestData quest;
    quest.quest_id = kQuestId;
    quest.quest_name = "Hero Trial";
    quest.quest_desc = "Defeat 10 monsters";
    context.SetQuestData(quest);

    context.ClientInteractNpc();
    context.ClientSelectMenu(kMenuQuestAccept);

    const auto& state = context.client_state();
    ASSERT_TRUE(state.active_quest.has_value());
    EXPECT_EQ(state.active_quest->quest_id, kQuestId);
    EXPECT_EQ(state.active_quest->quest_name, "Hero Trial");
    EXPECT_EQ(state.active_quest->quest_desc, "Defeat 10 monsters");
    ASSERT_FALSE(context.ui_listener.quest_accept_events.empty());
}

TEST(NpcE2EIntegrationTest, QuestCompleteFlowGivesRewardsAndClearsQuest) {
    NpcE2EContext context;
    context.ConfigureNpc("Quest Giver", NpcType::kQuest);
    context.LoadScript(kMenuScript);
    context.SetMenuAction(kMenuQuestAccept, MenuAction::QuestAccept(kQuestId));
    context.SetMenuAction(kMenuQuestComplete, MenuAction::QuestComplete(kQuestId));

    QuestData quest;
    quest.quest_id = kQuestId;
    quest.quest_name = "Hero Trial";
    quest.quest_desc = "Defeat 10 monsters";
    quest.rewards = {NpcQuestReward{501, 2}, NpcQuestReward{502, 1}};
    context.SetQuestData(quest);

    context.ClientInteractNpc();
    context.ClientSelectMenu(kMenuQuestAccept);
    context.ClientSelectMenu(kMenuQuestComplete);

    const auto& state = context.client_state();
    ASSERT_TRUE(state.last_quest_complete.has_value());
    EXPECT_EQ(state.last_quest_complete->quest_id, kQuestId);
    ASSERT_EQ(state.last_quest_complete->rewards.size(), 2u);
    EXPECT_EQ(state.last_quest_complete->rewards[0].item_id, 501u);
    EXPECT_EQ(state.last_quest_complete->rewards[0].count, 2u);
    EXPECT_FALSE(state.active_quest.has_value());
    ASSERT_FALSE(context.ui_listener.quest_complete_events.empty());
}

TEST(NpcE2EIntegrationTest, TeleportFlowUpdatesPlayerPosition) {
    NpcE2EContext context;
    context.ConfigureNpc("Teleporter", NpcType::kTeleport);
    context.LoadScript(kMenuScript);
    context.SetMenuAction(kMenuTeleport, MenuAction::Teleport(kTeleportMapId, kTeleportX, kTeleportY));

    context.ClientInteractNpc();
    context.ClientSelectMenu(kMenuTeleport);

    const auto& state = context.registry.get<CharacterStateComponent>(context.player);
    EXPECT_EQ(state.map_id, static_cast<uint32_t>(kTeleportMapId));
    EXPECT_EQ(state.position.x, kTeleportX);
    EXPECT_EQ(state.position.y, kTeleportY);
    ASSERT_TRUE(context.last_teleport.has_value());
    EXPECT_EQ(context.last_teleport->map_id, kTeleportMapId);
}

TEST(NpcE2EIntegrationTest, FullEndToEndSequenceCoversAllFlows) {
    NpcE2EContext context;
    context.ConfigureNpc("Multi NPC", NpcType::kMerchant);
    context.LoadScript(kMenuScript);

    context.SetMenuAction(kMenuBuy, MenuAction::Shop(kStoreId));
    context.SetMenuAction(kMenuQuestAccept, MenuAction::QuestAccept(kQuestId));
    context.SetMenuAction(kMenuQuestComplete, MenuAction::QuestComplete(kQuestId));
    context.SetMenuAction(kMenuTeleport, MenuAction::Teleport(kTeleportMapId, kTeleportX, kTeleportY));

    std::vector<ShopItemData> items = {
        {401, 20, 2},
        {402, 80, 1}
    };
    context.SetShopItems(kStoreId, items);

    QuestData quest;
    quest.quest_id = kQuestId;
    quest.quest_name = "Escort Mission";
    quest.quest_desc = "Escort villager to safe zone";
    quest.rewards = {NpcQuestReward{601, 3}};
    context.SetQuestData(quest);

    context.ClientInteractNpc();
    EXPECT_TRUE(context.client_state().dialog_open);

    context.ClientSelectMenu(kMenuBuy);
    EXPECT_TRUE(context.client_state().shop_open);
    context.SendShopClose();
    EXPECT_FALSE(context.client_state().shop_open);

    context.ClientSelectMenu(kMenuQuestAccept);
    ASSERT_TRUE(context.client_state().active_quest.has_value());
    context.ClientSelectMenu(kMenuQuestComplete);
    EXPECT_FALSE(context.client_state().active_quest.has_value());
    ASSERT_TRUE(context.client_state().last_quest_complete.has_value());

    context.ClientSelectMenu(kMenuTeleport);
    const auto& state = context.registry.get<CharacterStateComponent>(context.player);
    EXPECT_EQ(state.map_id, static_cast<uint32_t>(kTeleportMapId));
    EXPECT_EQ(state.position.x, kTeleportX);
    EXPECT_EQ(state.position.y, kTeleportY);
}

}  // namespace
