/**
 * @file item_handler.h
 * @brief Client item/inventory message handlers.
 *
 * Decodes FlatBuffers payloads for inventory updates, use/drop/equip/unequip
 * responses and forwards the results to caller-provided callbacks.
 */

#ifndef LEGEND2_CLIENT_HANDLERS_ITEM_HANDLER_H
#define LEGEND2_CLIENT_HANDLERS_ITEM_HANDLER_H

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

/// Client-side inventory item representation matching the wire format.
struct ClientInventoryItem {
    uint16_t slot = 0;
    uint32_t item_id = 0;
    uint32_t count = 0;
    int32_t durability = 0;
};

/**
 * @brief Handles item and inventory messages from the server.
 *
 * Responsibilities:
 * - Register item/inventory handlers with NetworkManager.
 * - Parse FlatBuffers payloads and forward results to callbacks.
 *
 * Threading: expected to be used on the main thread; handlers are invoked from
 * NetworkManager::update() context.
 *
 * Ownership: stores callbacks by value and does not own NetworkManager.
 * Caller manages handler lifetime and should keep it alive while handlers are
 * bound.
 */
class ItemHandler : public std::enable_shared_from_this<ItemHandler> {
public:
    using NetworkPacket = mir2::common::NetworkPacket;

    /**
     * @brief Callback hooks for item/inventory flow.
     *
     * Ownership: stored by value. If owner is provided, callbacks are invoked
     * only while the owner is still alive; otherwise the caller must ensure
     * captured objects outlive the handler.
     */
    struct Callbacks {
        // Optional lifetime guard for objects captured by callbacks.
        std::optional<std::weak_ptr<void>> owner;

        std::function<void(const std::vector<ClientInventoryItem>& items)>
            on_inventory_update;

        std::function<void(mir2::proto::ErrorCode code,
                           uint16_t slot,
                           uint32_t item_id,
                           uint32_t remaining)>
            on_use_item_response;

        std::function<void(mir2::proto::ErrorCode code,
                           uint32_t item_id,
                           uint32_t count)>
            on_drop_item_response;

        std::function<void(mir2::proto::ErrorCode code,
                           uint16_t slot,
                           uint32_t item_id)>
            on_equip_response;

        std::function<void(mir2::proto::ErrorCode code,
                           uint16_t slot,
                           uint32_t item_id)>
            on_unequip_response;

        std::function<void(const std::string& error)> on_parse_error;
    };

    explicit ItemHandler(Callbacks callbacks);
    ~ItemHandler() = default;

    // Non-copyable; prevent accidental duplication of handler registrations.
    ItemHandler(const ItemHandler&) = delete;
    ItemHandler& operator=(const ItemHandler&) = delete;

    /// Bind handlers with lifetime safety. Requires shared_ptr ownership.
    void BindHandlers(mir2::client::INetworkManager& manager);

    /// Static placeholder for consistency with the handler registration API.
    static void RegisterHandlers(mir2::client::INetworkManager& manager);

    void HandleInventoryUpdate(const NetworkPacket& packet);
    void HandleUseItemResponse(const NetworkPacket& packet);
    void HandleDropItemResponse(const NetworkPacket& packet);
    void HandleEquipResponse(const NetworkPacket& packet);
    void HandleUnequipResponse(const NetworkPacket& packet);

private:
    Callbacks callbacks_;
};

} // namespace mir2::game::handlers

#endif // LEGEND2_CLIENT_HANDLERS_ITEM_HANDLER_H
