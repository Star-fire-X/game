/**
 * @file npc_handler.h
 * @brief Client NPC message handlers.
 */

#ifndef LEGEND2_CLIENT_HANDLERS_NPC_HANDLER_H
#define LEGEND2_CLIENT_HANDLERS_NPC_HANDLER_H

#include "client/network/i_network_manager.h"
#include "core/event_dispatcher.h"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace mir2::game::handlers {

/**
 * @brief Client message handler interface (NPC uses this for uniformity).
 */
class IMessageHandler {
public:
    virtual ~IMessageHandler() = default;
    virtual void BindHandlers(mir2::client::INetworkManager& manager) = 0;
};

/**
 * @brief NPC UI event type mapping (SDL_UserEvent::code).
 *
 * Note: SDL_UserEvent::data1 points to a stack-allocated payload that is only
 * valid during EventDispatcher::dispatch(). Listeners should copy data if
 * they need to persist it.
 */
enum class NpcUiEventType : int {
    kInteractResponse = 1,
    kDialogShow = 2,
    kMenuSelect = 3,
    kShopOpen = 4,
    kShopClose = 5,
    kQuestAccept = 6,
    kQuestComplete = 7
};

/// NPC交互响应事件
struct NpcInteractEvent {
    uint64_t npc_id = 0;
    uint8_t result = 0;
    uint8_t npc_type = 0;
    std::string npc_name;
};

/// NPC对话显示事件
struct NpcDialogEvent {
    uint64_t npc_id = 0;
    std::string text;
    bool has_menu = false;
    std::vector<std::string> options;
};

/// NPC菜单选择事件
struct NpcMenuSelectEvent {
    uint64_t npc_id = 0;
    uint8_t option_index = 0;
};

/// NPC商店物品
struct NpcShopItem {
    uint32_t item_id = 0;
    uint32_t price = 0;
    uint16_t stock = 0;
};

/// NPC商店打开事件
struct NpcShopOpenEvent {
    uint64_t npc_id = 0;
    uint32_t store_id = 0;
    std::vector<NpcShopItem> items;
};

/// NPC商店关闭事件
struct NpcShopCloseEvent {
    uint64_t npc_id = 0;
};

/// NPC任务接受事件
struct NpcQuestAcceptEvent {
    uint64_t npc_id = 0;
    uint32_t quest_id = 0;
    std::string quest_name;
    std::string quest_desc;
};

/// NPC任务奖励
struct NpcQuestReward {
    uint32_t item_id = 0;
    uint32_t count = 0;
};

/// NPC任务完成事件
struct NpcQuestCompleteEvent {
    uint64_t npc_id = 0;
    uint32_t quest_id = 0;
    std::vector<NpcQuestReward> rewards;
};

/// NPC本地状态
struct NpcState {
    uint64_t npc_id = 0;
    uint8_t npc_type = 0;
    std::string npc_name;
    uint8_t last_interact_result = 0;

    bool dialog_open = false;
    std::string dialog_text;
    std::vector<std::string> dialog_options;

    bool shop_open = false;
    uint32_t store_id = 0;
    std::vector<NpcShopItem> shop_items;

    std::optional<uint8_t> last_menu_selection;

    struct QuestState {
        uint32_t quest_id = 0;
        std::string quest_name;
        std::string quest_desc;
    };

    struct QuestCompletion {
        uint32_t quest_id = 0;
        std::vector<NpcQuestReward> rewards;
    };

    std::optional<QuestState> active_quest;
    std::optional<QuestCompletion> last_quest_complete;
};

/**
 * @brief Handles NPC interaction messages.
 *
 * Responsibilities:
 * - Register NPC handlers with NetworkManager.
 * - Parse JSON payloads and forward results to UI events.
 * - Track local NPC interaction state.
 */
class NpcHandler : public IMessageHandler {
public:
    using NetworkPacket = mir2::common::NetworkPacket;

    /**
     * @brief Callback hooks for NPC flow.
     */
    struct Callbacks {
        mir2::core::EventDispatcher* event_dispatcher = nullptr;
        std::function<void(const std::string& error)> on_parse_error;
    };

    explicit NpcHandler(Callbacks callbacks);
    ~NpcHandler();

    void BindHandlers(mir2::client::INetworkManager& manager) override;
    static void RegisterHandlers(mir2::client::INetworkManager& manager);

    const NpcState& state() const { return state_; }

    void HandleNpcInteractResponse(const NetworkPacket& packet);
    void HandleNpcDialogShow(const NetworkPacket& packet);
    void HandleNpcMenuSelect(const NetworkPacket& packet);
    void HandleNpcShopOpen(const NetworkPacket& packet);
    void HandleNpcShopClose(const NetworkPacket& packet);
    void HandleNpcQuestAccept(const NetworkPacket& packet);
    void HandleNpcQuestComplete(const NetworkPacket& packet);

private:
    void DispatchUiEvent(NpcUiEventType type, const void* payload) const;
    void LogParseError(const std::string& message) const;

    Callbacks callbacks_;
    NpcState state_;
    static NpcHandler* instance_;
};

} // namespace mir2::game::handlers

#endif // LEGEND2_CLIENT_HANDLERS_NPC_HANDLER_H
