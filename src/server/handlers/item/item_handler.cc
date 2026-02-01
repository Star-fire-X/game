#include "handlers/item/item_handler.h"

#include <flatbuffers/flatbuffers.h>

#include "common/enums.h"
#include "handlers/handler_utils.h"
#include "item_generated.h"

namespace legend2::handlers {

namespace {

std::vector<uint8_t> BuildPickupRsp(const ItemPickupResult& result) {
    flatbuffers::FlatBufferBuilder builder;
    const auto rsp = mir2::proto::CreatePickupItemRsp(
        builder, ToProtoError(result.code), result.item_id);
    builder.Finish(rsp);
    const uint8_t* data = builder.GetBufferPointer();
    return std::vector<uint8_t>(data, data + builder.GetSize());
}

std::vector<uint8_t> BuildUseRsp(const ItemUseResult& result) {
    flatbuffers::FlatBufferBuilder builder;
    const auto rsp = mir2::proto::CreateUseItemRsp(
        builder, ToProtoError(result.code), result.slot, result.item_id, result.remaining);
    builder.Finish(rsp);
    const uint8_t* data = builder.GetBufferPointer();
    return std::vector<uint8_t>(data, data + builder.GetSize());
}

std::vector<uint8_t> BuildDropRsp(const ItemDropResult& result) {
    flatbuffers::FlatBufferBuilder builder;
    const auto rsp = mir2::proto::CreateDropItemRsp(
        builder, ToProtoError(result.code), result.item_id, result.count);
    builder.Finish(rsp);
    const uint8_t* data = builder.GetBufferPointer();
    return std::vector<uint8_t>(data, data + builder.GetSize());
}

}  // namespace

ItemHandler::ItemHandler(InventoryService& service)
    : BaseHandler(mir2::log::LogCategory::kGame),
      service_(service) {}

void ItemHandler::DoHandle(const HandlerContext& context,
                           uint16_t msg_id,
                           const std::vector<uint8_t>& payload,
                           ResponseCallback callback) {
    if (msg_id == static_cast<uint16_t>(mir2::common::MsgId::kPickupItemReq)) {
        HandlePickup(context, payload, std::move(callback));
        return;
    }
    if (msg_id == static_cast<uint16_t>(mir2::common::MsgId::kUseItemReq)) {
        HandleUse(context, payload, std::move(callback));
        return;
    }
    if (msg_id == static_cast<uint16_t>(mir2::common::MsgId::kDropItemReq)) {
        HandleDrop(context, payload, std::move(callback));
        return;
    }

    OnError(context, msg_id, mir2::common::ErrorCode::kInvalidAction, std::move(callback));
}

void ItemHandler::OnError(const HandlerContext& context,
                          uint16_t msg_id,
                          mir2::common::ErrorCode error_code,
                          ResponseCallback callback) {
    BaseHandler::OnError(context, msg_id, error_code, callback);

    ResponseList responses;
    if (msg_id == static_cast<uint16_t>(mir2::common::MsgId::kPickupItemReq)) {
        ItemPickupResult result;
        result.code = error_code;
        responses.push_back({context.client_id,
                             static_cast<uint16_t>(mir2::common::MsgId::kPickupItemRsp),
                             BuildPickupRsp(result)});
    } else if (msg_id == static_cast<uint16_t>(mir2::common::MsgId::kUseItemReq)) {
        ItemUseResult result;
        result.code = error_code;
        responses.push_back({context.client_id,
                             static_cast<uint16_t>(mir2::common::MsgId::kUseItemRsp),
                             BuildUseRsp(result)});
    } else if (msg_id == static_cast<uint16_t>(mir2::common::MsgId::kDropItemReq)) {
        ItemDropResult result;
        result.code = error_code;
        responses.push_back({context.client_id,
                             static_cast<uint16_t>(mir2::common::MsgId::kDropItemRsp),
                             BuildDropRsp(result)});
    }

    if (callback) {
        callback(responses);
    }
}

void ItemHandler::HandlePickup(const HandlerContext& context,
                               const std::vector<uint8_t>& payload,
                               ResponseCallback callback) {
    flatbuffers::Verifier verifier(payload.data(), payload.size());
    if (!verifier.VerifyBuffer<mir2::proto::PickupItemReq>(nullptr)) {
        OnError(context, static_cast<uint16_t>(mir2::common::MsgId::kPickupItemReq),
                mir2::common::ErrorCode::kInvalidAction, std::move(callback));
        return;
    }

    const auto* req = flatbuffers::GetRoot<mir2::proto::PickupItemReq>(payload.data());
    const uint32_t item_id = req ? req->item_id() : 0;
    if (item_id == 0) {
        OnError(context, static_cast<uint16_t>(mir2::common::MsgId::kPickupItemReq),
                mir2::common::ErrorCode::kInvalidAction, std::move(callback));
        return;
    }

    ItemPickupResult result = service_.PickupItem(context.client_id, item_id);
    ResponseList responses;
    responses.push_back({context.client_id,
                         static_cast<uint16_t>(mir2::common::MsgId::kPickupItemRsp),
                         BuildPickupRsp(result)});
    if (callback) {
        callback(responses);
    }
}

void ItemHandler::HandleUse(const HandlerContext& context,
                            const std::vector<uint8_t>& payload,
                            ResponseCallback callback) {
    flatbuffers::Verifier verifier(payload.data(), payload.size());
    if (!verifier.VerifyBuffer<mir2::proto::UseItemReq>(nullptr)) {
        OnError(context, static_cast<uint16_t>(mir2::common::MsgId::kUseItemReq),
                mir2::common::ErrorCode::kInvalidAction, std::move(callback));
        return;
    }

    const auto* req = flatbuffers::GetRoot<mir2::proto::UseItemReq>(payload.data());
    const uint16_t slot = req ? req->slot() : 0;
    const uint32_t item_id = req ? req->item_id() : 0;
    if (item_id == 0) {
        OnError(context, static_cast<uint16_t>(mir2::common::MsgId::kUseItemReq),
                mir2::common::ErrorCode::kInvalidAction, std::move(callback));
        return;
    }

    ItemUseResult result = service_.UseItem(context.client_id, slot, item_id);
    ResponseList responses;
    responses.push_back({context.client_id,
                         static_cast<uint16_t>(mir2::common::MsgId::kUseItemRsp),
                         BuildUseRsp(result)});
    if (callback) {
        callback(responses);
    }
}

void ItemHandler::HandleDrop(const HandlerContext& context,
                             const std::vector<uint8_t>& payload,
                             ResponseCallback callback) {
    flatbuffers::Verifier verifier(payload.data(), payload.size());
    if (!verifier.VerifyBuffer<mir2::proto::DropItemReq>(nullptr)) {
        OnError(context, static_cast<uint16_t>(mir2::common::MsgId::kDropItemReq),
                mir2::common::ErrorCode::kInvalidAction, std::move(callback));
        return;
    }

    const auto* req = flatbuffers::GetRoot<mir2::proto::DropItemReq>(payload.data());
    const uint16_t slot = req ? req->slot() : 0;
    const uint32_t item_id = req ? req->item_id() : 0;
    const uint32_t count = req ? req->count() : 0;
    if (item_id == 0 || count == 0) {
        OnError(context, static_cast<uint16_t>(mir2::common::MsgId::kDropItemReq),
                mir2::common::ErrorCode::kInvalidAction, std::move(callback));
        return;
    }

    ItemDropResult result = service_.DropItem(context.client_id, slot, item_id, count);
    ResponseList responses;
    responses.push_back({context.client_id,
                         static_cast<uint16_t>(mir2::common::MsgId::kDropItemRsp),
                         BuildDropRsp(result)});
    if (callback) {
        callback(responses);
    }
}

}  // namespace legend2::handlers
