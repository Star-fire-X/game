/**
 * @file item_handler.cpp
 * @brief Implementation of client item/inventory message handlers.
 *
 * Each Handle* method follows the same defensive pattern:
 * 1. Lock the callback owner (if present) to guard captured object lifetimes.
 * 2. Reject empty payloads early.
 * 3. Run the FlatBuffers verifier before accessing any field.
 * 4. Null-check the root pointer after deserialization.
 * 5. Forward extracted fields to the appropriate callback.
 */

#include "client/handlers/item_handler.h"

#include "item_generated.h"

#include <flatbuffers/flatbuffers.h>

namespace mir2::game::handlers {

namespace {

/// Attempt to lock the optional callback owner guard.  Returns true if there is
/// no owner or if the owner is still alive (in which case *owner_guard holds
/// the shared_ptr keeping it alive for the duration of the callback).
bool TryLockCallbackOwner(const ItemHandler::Callbacks& callbacks,
                          std::shared_ptr<void>* owner_guard) {
    if (!callbacks.owner.has_value()) {
        return true;
    }
    *owner_guard = callbacks.owner->lock();
    return static_cast<bool>(*owner_guard);
}

} // namespace

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

ItemHandler::ItemHandler(Callbacks callbacks)
    : callbacks_(std::move(callbacks)) {}

// ---------------------------------------------------------------------------
// Handler registration
// ---------------------------------------------------------------------------

void ItemHandler::RegisterHandlers(mir2::client::INetworkManager& /*manager*/) {
    // Item handlers are bound per-instance to capture callbacks.
    // Use BindHandlers() on a shared_ptr-managed instance instead.
}

void ItemHandler::BindHandlers(mir2::client::INetworkManager& manager) {
    const auto weak_self = weak_from_this();

    manager.register_handler(mir2::common::MsgId::kInventoryUpdate,
                             [weak_self](const NetworkPacket& packet) {
                                 if (auto self = weak_self.lock()) {
                                     self->HandleInventoryUpdate(packet);
                                 }
                             });

    manager.register_handler(mir2::common::MsgId::kUseItemRsp,
                             [weak_self](const NetworkPacket& packet) {
                                 if (auto self = weak_self.lock()) {
                                     self->HandleUseItemResponse(packet);
                                 }
                             });

    manager.register_handler(mir2::common::MsgId::kDropItemRsp,
                             [weak_self](const NetworkPacket& packet) {
                                 if (auto self = weak_self.lock()) {
                                     self->HandleDropItemResponse(packet);
                                 }
                             });

    manager.register_handler(mir2::common::MsgId::kEquipRsp,
                             [weak_self](const NetworkPacket& packet) {
                                 if (auto self = weak_self.lock()) {
                                     self->HandleEquipResponse(packet);
                                 }
                             });

    manager.register_handler(mir2::common::MsgId::kUnequipRsp,
                             [weak_self](const NetworkPacket& packet) {
                                 if (auto self = weak_self.lock()) {
                                     self->HandleUnequipResponse(packet);
                                 }
                             });
}

// ---------------------------------------------------------------------------
// HandleInventoryUpdate  (MsgId::kInventoryUpdate = 4001)
// ---------------------------------------------------------------------------

void ItemHandler::HandleInventoryUpdate(const NetworkPacket& packet) {
    std::shared_ptr<void> owner_guard;
    if (!TryLockCallbackOwner(callbacks_, &owner_guard)) {
        return;
    }

    if (packet.payload.empty()) {
        if (callbacks_.on_parse_error) {
            callbacks_.on_parse_error("Empty inventory update payload");
        }
        return;
    }

    flatbuffers::Verifier verifier(packet.payload.data(), packet.payload.size());
    if (!verifier.VerifyBuffer<mir2::proto::InventoryUpdate>(nullptr)) {
        if (callbacks_.on_parse_error) {
            callbacks_.on_parse_error("Inventory update verification failed");
        }
        return;
    }

    const auto* update =
        flatbuffers::GetRoot<mir2::proto::InventoryUpdate>(packet.payload.data());
    if (!update) {
        if (callbacks_.on_parse_error) {
            callbacks_.on_parse_error("Inventory update parse failed");
        }
        return;
    }

    if (callbacks_.on_inventory_update) {
        std::vector<ClientInventoryItem> items;
        const auto* fb_items = update->items();
        if (fb_items) {
            items.reserve(fb_items->size());
            for (const auto* entry : *fb_items) {
                if (!entry) {
                    continue;
                }
                ClientInventoryItem item;
                item.slot = entry->slot();
                const auto* info = entry->item();
                if (info) {
                    item.item_id = info->item_id();
                    item.count = info->count();
                    item.durability = info->durability();
                }
                items.push_back(item);
            }
        }
        callbacks_.on_inventory_update(items);
    }
}

// ---------------------------------------------------------------------------
// HandleUseItemResponse  (MsgId::kUseItemRsp = 4011)
// ---------------------------------------------------------------------------

void ItemHandler::HandleUseItemResponse(const NetworkPacket& packet) {
    std::shared_ptr<void> owner_guard;
    if (!TryLockCallbackOwner(callbacks_, &owner_guard)) {
        return;
    }

    if (packet.payload.empty()) {
        if (callbacks_.on_parse_error) {
            callbacks_.on_parse_error("Empty use-item response payload");
        }
        return;
    }

    flatbuffers::Verifier verifier(packet.payload.data(), packet.payload.size());
    if (!verifier.VerifyBuffer<mir2::proto::UseItemRsp>(nullptr)) {
        if (callbacks_.on_parse_error) {
            callbacks_.on_parse_error("Use-item response verification failed");
        }
        return;
    }

    const auto* rsp =
        flatbuffers::GetRoot<mir2::proto::UseItemRsp>(packet.payload.data());
    if (!rsp) {
        if (callbacks_.on_parse_error) {
            callbacks_.on_parse_error("Use-item response parse failed");
        }
        return;
    }

    if (callbacks_.on_use_item_response) {
        callbacks_.on_use_item_response(rsp->code(),
                                        rsp->slot(),
                                        rsp->item_id(),
                                        rsp->remaining());
    }
}

// ---------------------------------------------------------------------------
// HandleDropItemResponse  (MsgId::kDropItemRsp = 4021)
// ---------------------------------------------------------------------------

void ItemHandler::HandleDropItemResponse(const NetworkPacket& packet) {
    std::shared_ptr<void> owner_guard;
    if (!TryLockCallbackOwner(callbacks_, &owner_guard)) {
        return;
    }

    if (packet.payload.empty()) {
        if (callbacks_.on_parse_error) {
            callbacks_.on_parse_error("Empty drop-item response payload");
        }
        return;
    }

    flatbuffers::Verifier verifier(packet.payload.data(), packet.payload.size());
    if (!verifier.VerifyBuffer<mir2::proto::DropItemRsp>(nullptr)) {
        if (callbacks_.on_parse_error) {
            callbacks_.on_parse_error("Drop-item response verification failed");
        }
        return;
    }

    const auto* rsp =
        flatbuffers::GetRoot<mir2::proto::DropItemRsp>(packet.payload.data());
    if (!rsp) {
        if (callbacks_.on_parse_error) {
            callbacks_.on_parse_error("Drop-item response parse failed");
        }
        return;
    }

    if (callbacks_.on_drop_item_response) {
        callbacks_.on_drop_item_response(rsp->code(),
                                         rsp->item_id(),
                                         rsp->count());
    }
}

// ---------------------------------------------------------------------------
// HandleEquipResponse  (MsgId::kEquipRsp = 4042)
// ---------------------------------------------------------------------------

void ItemHandler::HandleEquipResponse(const NetworkPacket& packet) {
    std::shared_ptr<void> owner_guard;
    if (!TryLockCallbackOwner(callbacks_, &owner_guard)) {
        return;
    }

    if (packet.payload.empty()) {
        if (callbacks_.on_parse_error) {
            callbacks_.on_parse_error("Empty equip response payload");
        }
        return;
    }

    flatbuffers::Verifier verifier(packet.payload.data(), packet.payload.size());
    if (!verifier.VerifyBuffer<mir2::proto::EquipRsp>(nullptr)) {
        if (callbacks_.on_parse_error) {
            callbacks_.on_parse_error("Equip response verification failed");
        }
        return;
    }

    const auto* rsp =
        flatbuffers::GetRoot<mir2::proto::EquipRsp>(packet.payload.data());
    if (!rsp) {
        if (callbacks_.on_parse_error) {
            callbacks_.on_parse_error("Equip response parse failed");
        }
        return;
    }

    if (callbacks_.on_equip_response) {
        callbacks_.on_equip_response(rsp->code(),
                                     rsp->slot(),
                                     rsp->item_id());
    }
}

// ---------------------------------------------------------------------------
// HandleUnequipResponse  (MsgId::kUnequipRsp = 4043)
// ---------------------------------------------------------------------------

void ItemHandler::HandleUnequipResponse(const NetworkPacket& packet) {
    std::shared_ptr<void> owner_guard;
    if (!TryLockCallbackOwner(callbacks_, &owner_guard)) {
        return;
    }

    if (packet.payload.empty()) {
        if (callbacks_.on_parse_error) {
            callbacks_.on_parse_error("Empty unequip response payload");
        }
        return;
    }

    flatbuffers::Verifier verifier(packet.payload.data(), packet.payload.size());
    if (!verifier.VerifyBuffer<mir2::proto::UnequipRsp>(nullptr)) {
        if (callbacks_.on_parse_error) {
            callbacks_.on_parse_error("Unequip response verification failed");
        }
        return;
    }

    const auto* rsp =
        flatbuffers::GetRoot<mir2::proto::UnequipRsp>(packet.payload.data());
    if (!rsp) {
        if (callbacks_.on_parse_error) {
            callbacks_.on_parse_error("Unequip response parse failed");
        }
        return;
    }

    if (callbacks_.on_unequip_response) {
        callbacks_.on_unequip_response(rsp->code(),
                                       rsp->slot(),
                                       rsp->item_id());
    }
}

} // namespace mir2::game::handlers
