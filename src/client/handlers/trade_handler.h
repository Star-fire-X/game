/**
 * @file trade_handler.h
 * @brief Client trade message handlers.
 */

#ifndef LEGEND2_CLIENT_HANDLERS_TRADE_HANDLER_H
#define LEGEND2_CLIENT_HANDLERS_TRADE_HANDLER_H

#include "client/network/i_network_manager.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace mir2::proto {
enum class ErrorCode : uint16_t;
}

namespace mir2::game::handlers {

struct TradeItemData {
    uint16_t inventory_slot = 0;
    uint32_t item_id = 0;
    uint32_t count = 0;
};

struct TradeUpdateData {
    uint64_t trade_id = 0;
    uint64_t left_character_id = 0;
    uint64_t right_character_id = 0;
    std::vector<TradeItemData> left_items;
    std::vector<TradeItemData> right_items;
    uint32_t left_gold = 0;
    uint32_t right_gold = 0;
    bool left_confirmed = false;
    bool right_confirmed = false;
};

class TradeHandler : public std::enable_shared_from_this<TradeHandler> {
public:
    using NetworkPacket = mir2::common::NetworkPacket;

    struct Callbacks {
        std::optional<std::weak_ptr<void>> owner;

        std::function<void(bool success, mir2::proto::ErrorCode code, uint64_t trade_id)>
            on_trade_response;
        std::function<void(bool success, mir2::proto::ErrorCode code)>
            on_add_item_response;
        std::function<void(bool success, mir2::proto::ErrorCode code)>
            on_set_gold_response;
        std::function<void(bool success, mir2::proto::ErrorCode code)>
            on_confirm_response;
        std::function<void(bool success, mir2::proto::ErrorCode code)>
            on_cancel_response;
        std::function<void(const TradeUpdateData& update)> on_trade_update;
        std::function<void(uint64_t trade_id, bool success, mir2::proto::ErrorCode code)>
            on_trade_complete;
        std::function<void(const std::string& error)> on_parse_error;
    };

    explicit TradeHandler(Callbacks callbacks);
    ~TradeHandler() = default;

    TradeHandler(const TradeHandler&) = delete;
    TradeHandler& operator=(const TradeHandler&) = delete;

    void BindHandlers(mir2::client::INetworkManager& manager);
    static void RegisterHandlers(mir2::client::INetworkManager& manager);

    void HandleTradeResponse(const NetworkPacket& packet);
    void HandleTradeAddItemResponse(const NetworkPacket& packet);
    void HandleTradeSetGoldResponse(const NetworkPacket& packet);
    void HandleTradeConfirmResponse(const NetworkPacket& packet);
    void HandleTradeCancelResponse(const NetworkPacket& packet);
    void HandleTradeUpdate(const NetworkPacket& packet);
    void HandleTradeComplete(const NetworkPacket& packet);

    static void SendTradeRequest(mir2::client::INetworkManager& manager,
                                 uint32_t target_character_id);
    static void SendTradeAddItemRequest(mir2::client::INetworkManager& manager,
                                        uint64_t trade_id,
                                        uint16_t inventory_slot,
                                        uint32_t item_id,
                                        uint32_t count);
    static void SendTradeSetGoldRequest(mir2::client::INetworkManager& manager,
                                        uint64_t trade_id,
                                        uint32_t gold);
    static void SendTradeConfirmRequest(mir2::client::INetworkManager& manager,
                                        uint64_t trade_id);
    static void SendTradeCancelRequest(mir2::client::INetworkManager& manager,
                                       uint64_t trade_id);

private:
    Callbacks callbacks_;
};

} // namespace mir2::game::handlers

#endif // LEGEND2_CLIENT_HANDLERS_TRADE_HANDLER_H
