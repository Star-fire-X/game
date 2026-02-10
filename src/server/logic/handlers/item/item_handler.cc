#include "logic/handlers/item/item_handler.h"

#include <utility>
#include <vector>

#include <flatbuffers/flatbuffers.h>

#include "common/enums.h"
#include "item_generated.h"
#include "log/logger.h"
#include "logic/handlers/handler_error_utils.h"
#include "logic/response_sender.h"

namespace mir2::logic {

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

ItemHandler::ItemHandler(CoroutineExecutor& executor,
                         ResponseSender& response_sender,
                         InventoryService& service)
    : executor_(executor),
      response_sender_(response_sender),
      service_(service) {}

Task<void> ItemHandler::HandleMessage(HandlerContext ctx,
                                      const uint8_t* payload,
                                      size_t payload_size) {
  if (!payload || payload_size == 0) {
    SYSLOG_WARN("ItemHandler ignored empty payload (client_id={})", ctx.client_id);
    co_return;
  }

  flatbuffers::Verifier pickup_verifier(payload, payload_size);
  if (pickup_verifier.VerifyBuffer<mir2::proto::PickupItemReq>(nullptr)) {
    const auto* req = flatbuffers::GetRoot<mir2::proto::PickupItemReq>(payload);
    co_await HandlePickup(std::move(ctx), req);
    co_return;
  }

  flatbuffers::Verifier use_verifier(payload, payload_size);
  if (use_verifier.VerifyBuffer<mir2::proto::UseItemReq>(nullptr)) {
    const auto* req = flatbuffers::GetRoot<mir2::proto::UseItemReq>(payload);
    co_await HandleUse(std::move(ctx), req);
    co_return;
  }

  flatbuffers::Verifier drop_verifier(payload, payload_size);
  if (drop_verifier.VerifyBuffer<mir2::proto::DropItemReq>(nullptr)) {
    const auto* req = flatbuffers::GetRoot<mir2::proto::DropItemReq>(payload);
    co_await HandleDrop(std::move(ctx), req);
    co_return;
  }

  SYSLOG_WARN("ItemHandler payload verify failed (client_id={})", ctx.client_id);
}

Task<void> ItemHandler::HandlePickup(HandlerContext ctx, const mir2::proto::PickupItemReq* req) {
  const uint32_t item_id = req ? req->item_id() : 0;
  if (item_id == 0) {
    SYSLOG_WARN("ItemHandler pickup invalid item_id (client_id={})", ctx.client_id);
    co_await SendPickupError(std::move(ctx), mir2::common::ErrorCode::kInvalidAction);
    co_return;
  }

  ItemPickupResult result = service_.PickupItem(ctx.client_id, item_id);
  auto payload = BuildPickupRsp(result);
  co_await response_sender_.SendAsync(
      ctx.client_id,
      static_cast<uint16_t>(mir2::common::MsgId::kPickupItemRsp),
      std::move(payload));

  SYSLOG_DEBUG("ItemHandler pickup client_id={} item_id={} code={}",
               ctx.client_id, item_id, static_cast<int>(result.code));
}

Task<void> ItemHandler::HandleUse(HandlerContext ctx, const mir2::proto::UseItemReq* req) {
  const uint16_t slot = req ? req->slot() : 0;
  const uint32_t item_id = req ? req->item_id() : 0;
  if (item_id == 0) {
    SYSLOG_WARN("ItemHandler use invalid item_id (client_id={})", ctx.client_id);
    co_await SendUseError(std::move(ctx), mir2::common::ErrorCode::kInvalidAction);
    co_return;
  }

  ItemUseResult result = service_.UseItem(ctx.client_id, slot, item_id);
  auto payload = BuildUseRsp(result);
  co_await response_sender_.SendAsync(
      ctx.client_id,
      static_cast<uint16_t>(mir2::common::MsgId::kUseItemRsp),
      std::move(payload));

  SYSLOG_DEBUG("ItemHandler use client_id={} item_id={} slot={} code={}",
               ctx.client_id, item_id, slot, static_cast<int>(result.code));
}

Task<void> ItemHandler::HandleDrop(HandlerContext ctx, const mir2::proto::DropItemReq* req) {
  const uint16_t slot = req ? req->slot() : 0;
  const uint32_t item_id = req ? req->item_id() : 0;
  const uint32_t count = req ? req->count() : 0;
  if (item_id == 0 || count == 0) {
    SYSLOG_WARN("ItemHandler drop invalid payload (client_id={})", ctx.client_id);
    co_await SendDropError(std::move(ctx), mir2::common::ErrorCode::kInvalidAction);
    co_return;
  }

  ItemDropResult result = service_.DropItem(ctx.client_id, slot, item_id, count);
  auto payload = BuildDropRsp(result);
  co_await response_sender_.SendAsync(
      ctx.client_id,
      static_cast<uint16_t>(mir2::common::MsgId::kDropItemRsp),
      std::move(payload));

  SYSLOG_DEBUG("ItemHandler drop client_id={} item_id={} slot={} count={} code={}",
               ctx.client_id, item_id, slot, count, static_cast<int>(result.code));
}

Task<void> ItemHandler::SendPickupError(HandlerContext ctx, mir2::common::ErrorCode code) {
  ItemPickupResult result;
  result.code = code;
  auto payload = BuildPickupRsp(result);
  co_await response_sender_.SendAsync(
      ctx.client_id,
      static_cast<uint16_t>(mir2::common::MsgId::kPickupItemRsp),
      std::move(payload));
}

Task<void> ItemHandler::SendUseError(HandlerContext ctx, mir2::common::ErrorCode code) {
  ItemUseResult result;
  result.code = code;
  auto payload = BuildUseRsp(result);
  co_await response_sender_.SendAsync(
      ctx.client_id,
      static_cast<uint16_t>(mir2::common::MsgId::kUseItemRsp),
      std::move(payload));
}

Task<void> ItemHandler::SendDropError(HandlerContext ctx, mir2::common::ErrorCode code) {
  ItemDropResult result;
  result.code = code;
  auto payload = BuildDropRsp(result);
  co_await response_sender_.SendAsync(
      ctx.client_id,
      static_cast<uint16_t>(mir2::common::MsgId::kDropItemRsp),
      std::move(payload));
}

}  // namespace mir2::logic
