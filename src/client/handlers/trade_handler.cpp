#include "client/handlers/trade_handler.h"

#include "common/enums.h"
#include "trade_generated.h"

#include <flatbuffers/flatbuffers.h>

#include <utility>

namespace mir2::game::handlers {

namespace {

bool TryLockCallbackOwner(const TradeHandler::Callbacks& callbacks,
                          std::shared_ptr<void>* owner_guard) {
    if (!callbacks.owner.has_value()) {
        return true;
    }
    *owner_guard = callbacks.owner->lock();
    return static_cast<bool>(*owner_guard);
}

mir2::proto::ErrorCode ToProtoError(int error_code) {
    return static_cast<mir2::proto::ErrorCode>(static_cast<uint16_t>(error_code));
}

std::vector<uint8_t> BuildPayload(flatbuffers::FlatBufferBuilder& builder) {
    const uint8_t* data = builder.GetBufferPointer();
    return std::vector<uint8_t>(data, data + builder.GetSize());
}

} // namespace

TradeHandler::TradeHandler(Callbacks callbacks)
    : callbacks_(std::move(callbacks)) {}

void TradeHandler::RegisterHandlers(mir2::client::INetworkManager& /*manager*/) {
    // Trade handlers are bound per-instance to capture callbacks.
}

void TradeHandler::BindHandlers(mir2::client::INetworkManager& manager) {
    const auto weak_self = weak_from_this();

    auto bind = [&manager, &weak_self](mir2::common::MsgId msg_id,
                                       auto fn) {
        manager.register_handler(msg_id, [weak_self, fn](const NetworkPacket& packet) {
            if (auto self = weak_self.lock()) {
                (self.get()->*fn)(packet);
            }
        });
    };

    bind(mir2::common::MsgId::kTradeRsp, &TradeHandler::HandleTradeResponse);
    bind(mir2::common::MsgId::kTradeAddItemRsp, &TradeHandler::HandleTradeAddItemResponse);
    bind(mir2::common::MsgId::kTradeSetGoldRsp, &TradeHandler::HandleTradeSetGoldResponse);
    bind(mir2::common::MsgId::kTradeConfirmRsp, &TradeHandler::HandleTradeConfirmResponse);
    bind(mir2::common::MsgId::kTradeCancelRsp, &TradeHandler::HandleTradeCancelResponse);
    bind(mir2::common::MsgId::kTradeUpdate, &TradeHandler::HandleTradeUpdate);
    bind(mir2::common::MsgId::kTradeComplete, &TradeHandler::HandleTradeComplete);
}

void TradeHandler::HandleTradeResponse(const NetworkPacket& packet) {
    std::shared_ptr<void> owner_guard;
    if (!TryLockCallbackOwner(callbacks_, &owner_guard)) {
        return;
    }

    flatbuffers::Verifier verifier(packet.payload.data(), packet.payload.size());
    if (!verifier.VerifyBuffer<mir2::proto::TradeRsp>(nullptr)) {
        if (callbacks_.on_parse_error) {
            callbacks_.on_parse_error("TradeRsp verification failed");
        }
        return;
    }

    const auto* rsp = flatbuffers::GetRoot<mir2::proto::TradeRsp>(packet.payload.data());
    if (!rsp) {
        if (callbacks_.on_parse_error) {
            callbacks_.on_parse_error("TradeRsp parse failed");
        }
        return;
    }

    if (callbacks_.on_trade_response) {
        callbacks_.on_trade_response(rsp->success(),
                                     ToProtoError(rsp->error_code()),
                                     rsp->trade_id());
    }
}

void TradeHandler::HandleTradeAddItemResponse(const NetworkPacket& packet) {
    std::shared_ptr<void> owner_guard;
    if (!TryLockCallbackOwner(callbacks_, &owner_guard)) {
        return;
    }

    flatbuffers::Verifier verifier(packet.payload.data(), packet.payload.size());
    if (!verifier.VerifyBuffer<mir2::proto::TradeAddItemRsp>(nullptr)) {
        if (callbacks_.on_parse_error) {
            callbacks_.on_parse_error("TradeAddItemRsp verification failed");
        }
        return;
    }

    const auto* rsp = flatbuffers::GetRoot<mir2::proto::TradeAddItemRsp>(packet.payload.data());
    if (rsp && callbacks_.on_add_item_response) {
        callbacks_.on_add_item_response(rsp->success(), ToProtoError(rsp->error_code()));
    }
}

void TradeHandler::HandleTradeSetGoldResponse(const NetworkPacket& packet) {
    std::shared_ptr<void> owner_guard;
    if (!TryLockCallbackOwner(callbacks_, &owner_guard)) {
        return;
    }

    flatbuffers::Verifier verifier(packet.payload.data(), packet.payload.size());
    if (!verifier.VerifyBuffer<mir2::proto::TradeSetGoldRsp>(nullptr)) {
        if (callbacks_.on_parse_error) {
            callbacks_.on_parse_error("TradeSetGoldRsp verification failed");
        }
        return;
    }

    const auto* rsp = flatbuffers::GetRoot<mir2::proto::TradeSetGoldRsp>(packet.payload.data());
    if (rsp && callbacks_.on_set_gold_response) {
        callbacks_.on_set_gold_response(rsp->success(), ToProtoError(rsp->error_code()));
    }
}

void TradeHandler::HandleTradeConfirmResponse(const NetworkPacket& packet) {
    std::shared_ptr<void> owner_guard;
    if (!TryLockCallbackOwner(callbacks_, &owner_guard)) {
        return;
    }

    flatbuffers::Verifier verifier(packet.payload.data(), packet.payload.size());
    if (!verifier.VerifyBuffer<mir2::proto::TradeConfirmRsp>(nullptr)) {
        if (callbacks_.on_parse_error) {
            callbacks_.on_parse_error("TradeConfirmRsp verification failed");
        }
        return;
    }

    const auto* rsp = flatbuffers::GetRoot<mir2::proto::TradeConfirmRsp>(packet.payload.data());
    if (rsp && callbacks_.on_confirm_response) {
        callbacks_.on_confirm_response(rsp->success(), ToProtoError(rsp->error_code()));
    }
}

void TradeHandler::HandleTradeCancelResponse(const NetworkPacket& packet) {
    std::shared_ptr<void> owner_guard;
    if (!TryLockCallbackOwner(callbacks_, &owner_guard)) {
        return;
    }

    flatbuffers::Verifier verifier(packet.payload.data(), packet.payload.size());
    if (!verifier.VerifyBuffer<mir2::proto::TradeCancelRsp>(nullptr)) {
        if (callbacks_.on_parse_error) {
            callbacks_.on_parse_error("TradeCancelRsp verification failed");
        }
        return;
    }

    const auto* rsp = flatbuffers::GetRoot<mir2::proto::TradeCancelRsp>(packet.payload.data());
    if (rsp && callbacks_.on_cancel_response) {
        callbacks_.on_cancel_response(rsp->success(), ToProtoError(rsp->error_code()));
    }
}

void TradeHandler::HandleTradeUpdate(const NetworkPacket& packet) {
    std::shared_ptr<void> owner_guard;
    if (!TryLockCallbackOwner(callbacks_, &owner_guard)) {
        return;
    }

    flatbuffers::Verifier verifier(packet.payload.data(), packet.payload.size());
    if (!verifier.VerifyBuffer<mir2::proto::TradeUpdate>(nullptr)) {
        if (callbacks_.on_parse_error) {
            callbacks_.on_parse_error("TradeUpdate verification failed");
        }
        return;
    }

    const auto* update = flatbuffers::GetRoot<mir2::proto::TradeUpdate>(packet.payload.data());
    if (!update) {
        if (callbacks_.on_parse_error) {
            callbacks_.on_parse_error("TradeUpdate parse failed");
        }
        return;
    }

    if (callbacks_.on_trade_update) {
        TradeUpdateData data;
        data.trade_id = update->trade_id();
        data.left_character_id = update->left_character_id();
        data.right_character_id = update->right_character_id();
        data.left_gold = update->left_gold();
        data.right_gold = update->right_gold();
        data.left_confirmed = update->left_confirmed();
        data.right_confirmed = update->right_confirmed();

        if (const auto* items = update->left_items()) {
            data.left_items.reserve(items->size());
            for (const auto* item : *items) {
                if (!item) {
                    continue;
                }
                TradeItemData view;
                view.inventory_slot = item->inventory_slot();
                view.item_id = item->item_id();
                view.count = item->count();
                data.left_items.push_back(view);
            }
        }

        if (const auto* items = update->right_items()) {
            data.right_items.reserve(items->size());
            for (const auto* item : *items) {
                if (!item) {
                    continue;
                }
                TradeItemData view;
                view.inventory_slot = item->inventory_slot();
                view.item_id = item->item_id();
                view.count = item->count();
                data.right_items.push_back(view);
            }
        }

        callbacks_.on_trade_update(data);
    }
}

void TradeHandler::HandleTradeComplete(const NetworkPacket& packet) {
    std::shared_ptr<void> owner_guard;
    if (!TryLockCallbackOwner(callbacks_, &owner_guard)) {
        return;
    }

    flatbuffers::Verifier verifier(packet.payload.data(), packet.payload.size());
    if (!verifier.VerifyBuffer<mir2::proto::TradeComplete>(nullptr)) {
        if (callbacks_.on_parse_error) {
            callbacks_.on_parse_error("TradeComplete verification failed");
        }
        return;
    }

    const auto* complete = flatbuffers::GetRoot<mir2::proto::TradeComplete>(packet.payload.data());
    if (complete && callbacks_.on_trade_complete) {
        callbacks_.on_trade_complete(complete->trade_id(),
                                     complete->success(),
                                     ToProtoError(complete->error_code()));
    }
}

void TradeHandler::SendTradeRequest(mir2::client::INetworkManager& manager,
                                    uint32_t target_character_id) {
    flatbuffers::FlatBufferBuilder builder;
    const auto req = mir2::proto::CreateTradeReq(builder, target_character_id);
    builder.Finish(req);
    manager.send_message(mir2::common::MsgId::kTradeReq, BuildPayload(builder));
}

void TradeHandler::SendTradeAddItemRequest(mir2::client::INetworkManager& manager,
                                           uint64_t trade_id,
                                           uint16_t inventory_slot,
                                           uint32_t item_id,
                                           uint32_t count) {
    flatbuffers::FlatBufferBuilder builder;
    const auto req = mir2::proto::CreateTradeAddItemReq(
        builder, trade_id, inventory_slot, item_id, count);
    builder.Finish(req);
    manager.send_message(mir2::common::MsgId::kTradeAddItemReq, BuildPayload(builder));
}

void TradeHandler::SendTradeSetGoldRequest(mir2::client::INetworkManager& manager,
                                           uint64_t trade_id,
                                           uint32_t gold) {
    flatbuffers::FlatBufferBuilder builder;
    const auto req = mir2::proto::CreateTradeSetGoldReq(builder, trade_id, gold);
    builder.Finish(req);
    manager.send_message(mir2::common::MsgId::kTradeSetGoldReq, BuildPayload(builder));
}

void TradeHandler::SendTradeConfirmRequest(mir2::client::INetworkManager& manager,
                                           uint64_t trade_id) {
    flatbuffers::FlatBufferBuilder builder;
    const auto req = mir2::proto::CreateTradeConfirmReq(builder, trade_id);
    builder.Finish(req);
    manager.send_message(mir2::common::MsgId::kTradeConfirmReq, BuildPayload(builder));
}

void TradeHandler::SendTradeCancelRequest(mir2::client::INetworkManager& manager,
                                          uint64_t trade_id) {
    flatbuffers::FlatBufferBuilder builder;
    const auto req = mir2::proto::CreateTradeCancelReq(builder, trade_id);
    builder.Finish(req);
    manager.send_message(mir2::common::MsgId::kTradeCancelReq, BuildPayload(builder));
}

} // namespace mir2::game::handlers
