#include "client/handlers/npc_handler.h"

#include "common/enums.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <iostream>
#include <limits>
#include <string>

namespace mir2::game::handlers {

namespace {
using json = nlohmann::json;

std::string format_context_error(const char* context, const std::string& message) {
    std::string result("[");
    result += context;
    result += "] ";
    result += message;
    return result;
}

bool parse_json_payload(const mir2::common::NetworkPacket& packet,
                        json* out,
                        const char* context,
                        const std::function<void(const std::string&)>& log_error) {
    if (!out) {
        return false;
    }

    if (packet.payload.empty()) {
        log_error(format_context_error(context, "Empty payload"));
        return false;
    }

    try {
        *out = json::parse(packet.payload.begin(), packet.payload.end());
    } catch (const json::exception& ex) {
        log_error(format_context_error(context, std::string("JSON parse failed: ") + ex.what()));
        return false;
    }

    if (!out->is_object()) {
        log_error(format_context_error(context, "JSON payload is not an object"));
        return false;
    }

    return true;
}

bool read_uint64(const json& j,
                 const char* key,
                 uint64_t* out,
                 const char* context,
                 const std::function<void(const std::string&)>& log_error) {
    auto it = j.find(key);
    if (it == j.end() || !it->is_number()) {
        log_error(format_context_error(context, std::string("Missing or invalid field '") + key + "'"));
        return false;
    }

    if (it->is_number_unsigned()) {
        *out = it->get<uint64_t>();
        return true;
    }

    if (it->is_number_integer()) {
        const auto value = it->get<int64_t>();
        if (value < 0) {
            log_error(format_context_error(context, std::string("Field '") + key + "' is negative"));
            return false;
        }
        *out = static_cast<uint64_t>(value);
        return true;
    }

    log_error(format_context_error(context, std::string("Field '") + key + "' has invalid type"));
    return false;
}

bool read_uint32(const json& j,
                 const char* key,
                 uint32_t* out,
                 const char* context,
                 const std::function<void(const std::string&)>& log_error) {
    uint64_t value = 0;
    if (!read_uint64(j, key, &value, context, log_error)) {
        return false;
    }
    if (value > std::numeric_limits<uint32_t>::max()) {
        log_error(format_context_error(context, std::string("Field '") + key + "' out of range"));
        return false;
    }
    *out = static_cast<uint32_t>(value);
    return true;
}

bool read_uint8(const json& j,
                const char* key,
                uint8_t* out,
                const char* context,
                const std::function<void(const std::string&)>& log_error) {
    uint64_t value = 0;
    if (!read_uint64(j, key, &value, context, log_error)) {
        return false;
    }
    if (value > std::numeric_limits<uint8_t>::max()) {
        log_error(format_context_error(context, std::string("Field '") + key + "' out of range"));
        return false;
    }
    *out = static_cast<uint8_t>(value);
    return true;
}

bool read_string(const json& j,
                 const char* key,
                 std::string* out,
                 const char* context,
                 const std::function<void(const std::string&)>& log_error) {
    auto it = j.find(key);
    if (it == j.end() || !it->is_string()) {
        log_error(format_context_error(context, std::string("Missing or invalid field '") + key + "'"));
        return false;
    }
    *out = it->get<std::string>();
    return true;
}

bool read_bool(const json& j,
               const char* key,
               bool* out,
               const char* context,
               const std::function<void(const std::string&)>& log_error) {
    auto it = j.find(key);
    if (it == j.end()) {
        log_error(format_context_error(context, std::string("Missing field '") + key + "'"));
        return false;
    }

    if (it->is_boolean()) {
        *out = it->get<bool>();
        return true;
    }

    if (it->is_number_integer()) {
        const auto value = it->get<int>();
        *out = (value != 0);
        return true;
    }

    log_error(format_context_error(context, std::string("Invalid field '") + key + "'"));
    return false;
}

std::vector<std::string> read_string_array(const json& j,
                                           const char* key,
                                           const char* context,
                                           const std::function<void(const std::string&)>& log_error) {
    std::vector<std::string> result;
    auto it = j.find(key);
    if (it == j.end()) {
        return result;
    }

    if (!it->is_array()) {
        log_error(format_context_error(context, std::string("Field '") + key + "' is not an array"));
        return result;
    }

    for (const auto& entry : *it) {
        if (entry.is_string()) {
            result.push_back(entry.get<std::string>());
        } else {
            log_error(format_context_error(context, "Menu option is not a string"));
        }
    }

    return result;
}

std::vector<NpcShopItem> read_shop_items(const json& j,
                                         const char* key,
                                         const char* context,
                                         const std::function<void(const std::string&)>& log_error) {
    std::vector<NpcShopItem> items;
    auto it = j.find(key);
    if (it == j.end()) {
        return items;
    }

    if (!it->is_array()) {
        log_error(format_context_error(context, std::string("Field '") + key + "' is not an array"));
        return items;
    }

    for (const auto& entry : *it) {
        if (!entry.is_object()) {
            log_error(format_context_error(context, "Shop item is not an object"));
            continue;
        }

        NpcShopItem item;
        if (!read_uint32(entry, "item_id", &item.item_id, context, log_error)) {
            continue;
        }
        if (!read_uint32(entry, "price", &item.price, context, log_error)) {
            continue;
        }
        if (entry.contains("stock")) {
            uint32_t stock_value = 0;
            if (read_uint32(entry, "stock", &stock_value, context, log_error)) {
                item.stock = static_cast<uint16_t>(std::min<uint32_t>(stock_value, std::numeric_limits<uint16_t>::max()));
            }
        }
        items.push_back(std::move(item));
    }

    return items;
}

std::vector<NpcQuestReward> read_rewards(const json& j,
                                        const char* key,
                                        const char* context,
                                        const std::function<void(const std::string&)>& log_error) {
    std::vector<NpcQuestReward> rewards;
    auto it = j.find(key);
    if (it == j.end()) {
        return rewards;
    }

    if (!it->is_array()) {
        log_error(format_context_error(context, std::string("Field '") + key + "' is not an array"));
        return rewards;
    }

    for (const auto& entry : *it) {
        if (!entry.is_object()) {
            log_error(format_context_error(context, "Reward entry is not an object"));
            continue;
        }

        NpcQuestReward reward;
        if (!read_uint32(entry, "item_id", &reward.item_id, context, log_error)) {
            continue;
        }

        if (entry.contains("count")) {
            read_uint32(entry, "count", &reward.count, context, log_error);
        } else if (entry.contains("amount")) {
            read_uint32(entry, "amount", &reward.count, context, log_error);
        } else if (entry.contains("quantity")) {
            read_uint32(entry, "quantity", &reward.count, context, log_error);
        }

        if (reward.count == 0) {
            reward.count = 1;
        }

        rewards.push_back(std::move(reward));
    }

    return rewards;
}
} // namespace

NpcHandler* NpcHandler::instance_ = nullptr;

NpcHandler::NpcHandler(Callbacks callbacks)
    : callbacks_(std::move(callbacks)) {
    instance_ = this;
}

NpcHandler::~NpcHandler() {
    if (instance_ == this) {
        instance_ = nullptr;
    }
}

void NpcHandler::BindHandlers(mir2::client::INetworkManager& manager) {
    manager.register_handler(mir2::common::MsgId::kNpcInteractRsp,
                             [this](const NetworkPacket& packet) {
                                 HandleNpcInteractResponse(packet);
                             });
    manager.register_handler(mir2::common::MsgId::kNpcDialogShow,
                             [this](const NetworkPacket& packet) {
                                 HandleNpcDialogShow(packet);
                             });
    manager.register_handler(mir2::common::MsgId::kNpcMenuSelect,
                             [this](const NetworkPacket& packet) {
                                 HandleNpcMenuSelect(packet);
                             });
    manager.register_handler(mir2::common::MsgId::kNpcShopOpen,
                             [this](const NetworkPacket& packet) {
                                 HandleNpcShopOpen(packet);
                             });
    manager.register_handler(mir2::common::MsgId::kNpcShopClose,
                             [this](const NetworkPacket& packet) {
                                 HandleNpcShopClose(packet);
                             });
    manager.register_handler(mir2::common::MsgId::kNpcQuestAccept,
                             [this](const NetworkPacket& packet) {
                                 HandleNpcQuestAccept(packet);
                             });
    manager.register_handler(mir2::common::MsgId::kNpcQuestComplete,
                             [this](const NetworkPacket& packet) {
                                 HandleNpcQuestComplete(packet);
                             });
}

void NpcHandler::RegisterHandlers(mir2::client::INetworkManager& manager) {
    if (!instance_) {
        return;
    }
    instance_->BindHandlers(manager);
}

void NpcHandler::HandleNpcInteractResponse(const NetworkPacket& packet) {
    const auto log_error = [this](const std::string& msg) { LogParseError(msg); };

    json j;
    if (!parse_json_payload(packet, &j, "NpcInteractRsp", log_error)) {
        return;
    }

    uint64_t npc_id = 0;
    uint8_t result = 0;
    if (!read_uint64(j, "npc_id", &npc_id, "NpcInteractRsp", log_error)) {
        return;
    }
    if (!read_uint8(j, "result", &result, "NpcInteractRsp", log_error)) {
        return;
    }

    uint8_t npc_type = 0;
    if (j.contains("npc_type")) {
        read_uint8(j, "npc_type", &npc_type, "NpcInteractRsp", log_error);
    }
    std::string npc_name = j.value("npc_name", "");

    state_.npc_id = npc_id;
    state_.npc_type = npc_type;
    state_.npc_name = npc_name;
    state_.last_interact_result = result;

    if (result != 0) {
        state_.dialog_open = false;
        state_.dialog_text.clear();
        state_.dialog_options.clear();
        state_.shop_open = false;
        state_.store_id = 0;
        state_.shop_items.clear();
    }

    NpcInteractEvent event{npc_id, result, npc_type, npc_name};
    DispatchUiEvent(NpcUiEventType::kInteractResponse, &event);
}

void NpcHandler::HandleNpcDialogShow(const NetworkPacket& packet) {
    const auto log_error = [this](const std::string& msg) { LogParseError(msg); };

    json j;
    if (!parse_json_payload(packet, &j, "NpcDialogShow", log_error)) {
        return;
    }

    uint64_t npc_id = 0;
    if (!read_uint64(j, "npc_id", &npc_id, "NpcDialogShow", log_error)) {
        return;
    }

    std::string text;
    if (!read_string(j, "text", &text, "NpcDialogShow", log_error)) {
        return;
    }

    bool has_menu = false;
    if (j.contains("has_menu")) {
        read_bool(j, "has_menu", &has_menu, "NpcDialogShow", log_error);
    }
    std::vector<std::string> options = read_string_array(j, "options", "NpcDialogShow", log_error);
    if (!options.empty()) {
        has_menu = true;
    }

    state_.npc_id = npc_id;
    state_.dialog_open = true;
    state_.dialog_text = text;
    state_.dialog_options = options;

    NpcDialogEvent event;
    event.npc_id = npc_id;
    event.text = text;
    event.has_menu = has_menu;
    event.options = std::move(options);

    DispatchUiEvent(NpcUiEventType::kDialogShow, &event);
}

void NpcHandler::HandleNpcMenuSelect(const NetworkPacket& packet) {
    const auto log_error = [this](const std::string& msg) { LogParseError(msg); };

    json j;
    if (!parse_json_payload(packet, &j, "NpcMenuSelect", log_error)) {
        return;
    }

    uint64_t npc_id = 0;
    uint8_t option_index = 0;
    if (!read_uint64(j, "npc_id", &npc_id, "NpcMenuSelect", log_error)) {
        return;
    }
    if (!read_uint8(j, "option_index", &option_index, "NpcMenuSelect", log_error)) {
        return;
    }

    state_.npc_id = npc_id;
    state_.last_menu_selection = option_index;

    NpcMenuSelectEvent event{npc_id, option_index};
    DispatchUiEvent(NpcUiEventType::kMenuSelect, &event);
}

void NpcHandler::HandleNpcShopOpen(const NetworkPacket& packet) {
    const auto log_error = [this](const std::string& msg) { LogParseError(msg); };

    json j;
    if (!parse_json_payload(packet, &j, "NpcShopOpen", log_error)) {
        return;
    }

    uint64_t npc_id = 0;
    uint32_t store_id = 0;
    if (!read_uint64(j, "npc_id", &npc_id, "NpcShopOpen", log_error)) {
        return;
    }
    if (!read_uint32(j, "store_id", &store_id, "NpcShopOpen", log_error)) {
        return;
    }

    auto items = read_shop_items(j, "items", "NpcShopOpen", log_error);

    state_.npc_id = npc_id;
    state_.shop_open = true;
    state_.store_id = store_id;
    state_.shop_items = items;

    NpcShopOpenEvent event;
    event.npc_id = npc_id;
    event.store_id = store_id;
    event.items = std::move(items);

    DispatchUiEvent(NpcUiEventType::kShopOpen, &event);
}

void NpcHandler::HandleNpcShopClose(const NetworkPacket& packet) {
    const auto log_error = [this](const std::string& msg) { LogParseError(msg); };

    json j;
    if (!parse_json_payload(packet, &j, "NpcShopClose", log_error)) {
        return;
    }

    uint64_t npc_id = 0;
    if (!read_uint64(j, "npc_id", &npc_id, "NpcShopClose", log_error)) {
        return;
    }

    state_.npc_id = npc_id;
    state_.shop_open = false;
    state_.store_id = 0;
    state_.shop_items.clear();

    NpcShopCloseEvent event{npc_id};
    DispatchUiEvent(NpcUiEventType::kShopClose, &event);
}

void NpcHandler::HandleNpcQuestAccept(const NetworkPacket& packet) {
    const auto log_error = [this](const std::string& msg) { LogParseError(msg); };

    json j;
    if (!parse_json_payload(packet, &j, "NpcQuestAccept", log_error)) {
        return;
    }

    uint64_t npc_id = 0;
    uint32_t quest_id = 0;
    if (!read_uint64(j, "npc_id", &npc_id, "NpcQuestAccept", log_error)) {
        return;
    }
    if (!read_uint32(j, "quest_id", &quest_id, "NpcQuestAccept", log_error)) {
        return;
    }

    std::string quest_name = j.value("quest_name", "");
    std::string quest_desc = j.value("quest_desc", "");

    state_.npc_id = npc_id;
    state_.active_quest = NpcState::QuestState{quest_id, quest_name, quest_desc};

    NpcQuestAcceptEvent event;
    event.npc_id = npc_id;
    event.quest_id = quest_id;
    event.quest_name = quest_name;
    event.quest_desc = quest_desc;

    DispatchUiEvent(NpcUiEventType::kQuestAccept, &event);
}

void NpcHandler::HandleNpcQuestComplete(const NetworkPacket& packet) {
    const auto log_error = [this](const std::string& msg) { LogParseError(msg); };

    json j;
    if (!parse_json_payload(packet, &j, "NpcQuestComplete", log_error)) {
        return;
    }

    uint64_t npc_id = 0;
    uint32_t quest_id = 0;
    if (!read_uint64(j, "npc_id", &npc_id, "NpcQuestComplete", log_error)) {
        return;
    }
    if (!read_uint32(j, "quest_id", &quest_id, "NpcQuestComplete", log_error)) {
        return;
    }

    auto rewards = read_rewards(j, "rewards", "NpcQuestComplete", log_error);

    state_.npc_id = npc_id;
    state_.last_quest_complete = NpcState::QuestCompletion{quest_id, rewards};
    if (state_.active_quest && state_.active_quest->quest_id == quest_id) {
        state_.active_quest.reset();
    }

    NpcQuestCompleteEvent event;
    event.npc_id = npc_id;
    event.quest_id = quest_id;
    event.rewards = std::move(rewards);

    DispatchUiEvent(NpcUiEventType::kQuestComplete, &event);
}

void NpcHandler::DispatchUiEvent(NpcUiEventType type, const void* payload) const {
    if (!callbacks_.event_dispatcher) {
        return;
    }

    SDL_Event event{};
    event.type = SDL_USEREVENT;
    event.user.code = static_cast<int>(type);
    event.user.data1 = const_cast<void*>(payload);
    event.user.data2 = nullptr;

    callbacks_.event_dispatcher->dispatch(event);
}

void NpcHandler::LogParseError(const std::string& message) const {
    std::cerr << "[NpcHandler] " << message << std::endl;
    if (callbacks_.on_parse_error) {
        callbacks_.on_parse_error(message);
    }
}

} // namespace mir2::game::handlers
