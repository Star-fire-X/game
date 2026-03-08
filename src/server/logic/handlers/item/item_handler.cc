#include "logic/handlers/item/item_handler.h"

#include <exception>
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

std::vector<uint8_t> BuildEquipRsp(const ItemEquipResult& result) {
  flatbuffers::FlatBufferBuilder builder;
  const auto rsp = mir2::proto::CreateEquipRsp(
      builder, ToProtoError(result.code), result.slot, result.item_id);
  builder.Finish(rsp);
  const uint8_t* data = builder.GetBufferPointer();
  return std::vector<uint8_t>(data, data + builder.GetSize());
}

std::vector<uint8_t> BuildUnequipRsp(const ItemUnequipResult& result) {
  flatbuffers::FlatBufferBuilder builder;
  const auto rsp = mir2::proto::CreateUnequipRsp(
      builder, ToProtoError(result.code), result.slot, result.item_id);
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
  try {
    if (!payload || payload_size == 0) {
      SYSLOG_WARN("ItemHandler ignored empty payload (client_id={})", ctx.client_id);
      co_return;
    }
    switch (static_cast<mir2::common::MsgId>(ctx.msg_id)) {
      case mir2::common::MsgId::kPickupItemReq: {
        flatbuffers::Verifier verifier(payload, payload_size);
        if (!verifier.VerifyBuffer<mir2::proto::PickupItemReq>(nullptr)) {
          SYSLOG_WARN("ItemHandler pickup verify failed (client_id={})", ctx.client_id);
          co_return;
        }
        const auto* req = flatbuffers::GetRoot<mir2::proto::PickupItemReq>(payload);
        co_await HandlePickup(std::move(ctx), req);
        co_return;
      }
      case mir2::common::MsgId::kUseItemReq: {
        flatbuffers::Verifier verifier(payload, payload_size);
        if (!verifier.VerifyBuffer<mir2::proto::UseItemReq>(nullptr)) {
          SYSLOG_WARN("ItemHandler use verify failed (client_id={})", ctx.client_id);
          co_return;
        }
        const auto* req = flatbuffers::GetRoot<mir2::proto::UseItemReq>(payload);
        co_await HandleUse(std::move(ctx), req);
        co_return;
      }
      case mir2::common::MsgId::kDropItemReq: {
        flatbuffers::Verifier verifier(payload, payload_size);
        if (!verifier.VerifyBuffer<mir2::proto::DropItemReq>(nullptr)) {
          SYSLOG_WARN("ItemHandler drop verify failed (client_id={})", ctx.client_id);
          co_return;
        }
        const auto* req = flatbuffers::GetRoot<mir2::proto::DropItemReq>(payload);
        co_await HandleDrop(std::move(ctx), req);
        co_return;
      }
      case mir2::common::MsgId::kEquipReq: {
        flatbuffers::Verifier verifier(payload, payload_size);
        if (!verifier.VerifyBuffer<mir2::proto::EquipReq>(nullptr)) {
          SYSLOG_WARN("ItemHandler equip verify failed (client_id={})", ctx.client_id);
          co_return;
        }
        const auto* req = flatbuffers::GetRoot<mir2::proto::EquipReq>(payload);
        co_await HandleEquip(std::move(ctx), req);
        co_return;
      }
      case mir2::common::MsgId::kUnequipReq: {
        flatbuffers::Verifier verifier(payload, payload_size);
        if (!verifier.VerifyBuffer<mir2::proto::UnequipReq>(nullptr)) {
          SYSLOG_WARN("ItemHandler unequip verify failed (client_id={})", ctx.client_id);
          co_return;
        }
        const auto* req = flatbuffers::GetRoot<mir2::proto::UnequipReq>(payload);
        co_await HandleUnequip(std::move(ctx), req);
        co_return;
      }
      default:
        SYSLOG_WARN("ItemHandler unsupported msg_id={} client_id={}",
                    ctx.msg_id,
                    ctx.client_id);
        co_return;
    }
  } catch (const std::exception& ex) {
    SYSLOG_ERROR("ItemHandler HandleMessage exception client_id={} error={}",
                 ctx.client_id, ex.what());
    co_return;
  } catch (...) {
    SYSLOG_ERROR("ItemHandler HandleMessage exception client_id={} error=unknown",
                 ctx.client_id);
    co_return;
  }
}

Task<void> ItemHandler::HandlePickup(HandlerContext ctx, const mir2::proto::PickupItemReq* req) {
  const uint32_t item_id = req ? req->item_id() : 0;
  bool send_invalid_action = false;
  try {
    if (item_id == 0) {
      SYSLOG_WARN("ItemHandler pickup invalid item_id (client_id={})", ctx.client_id);
      send_invalid_action = true;
    } else {
      ItemPickupResult result = service_.PickupItem(ctx.client_id, item_id);
      auto payload = BuildPickupRsp(result);
      co_await response_sender_.SendAsync(
          ctx.client_id,
          static_cast<uint16_t>(mir2::common::MsgId::kPickupItemRsp),
          std::move(payload));

      SYSLOG_DEBUG("ItemHandler pickup client_id={} item_id={} code={}",
                   ctx.client_id, item_id, static_cast<int>(result.code));
      co_return;
    }
  } catch (const std::exception& ex) {
    SYSLOG_ERROR("ItemHandler pickup exception client_id={} item_id={} error={}",
                 ctx.client_id, item_id, ex.what());
    send_invalid_action = true;
  } catch (...) {
    SYSLOG_ERROR("ItemHandler pickup exception client_id={} item_id={} error=unknown",
                 ctx.client_id, item_id);
    send_invalid_action = true;
  }

  if (send_invalid_action) {
    try {
      co_await SendPickupError(ctx, mir2::common::ErrorCode::kInvalidAction);
    } catch (const std::exception& send_ex) {
      SYSLOG_ERROR("ItemHandler pickup fallback send failed client_id={} item_id={} error={}",
                   ctx.client_id, item_id, send_ex.what());
    } catch (...) {
      SYSLOG_ERROR("ItemHandler pickup fallback send failed client_id={} item_id={} error=unknown",
                   ctx.client_id, item_id);
    }
  }
  co_return;
}

Task<void> ItemHandler::HandleEquip(HandlerContext ctx, const mir2::proto::EquipReq* req) {
  const uint16_t slot = req ? req->slot() : 0;
  const uint32_t item_id = req ? req->item_id() : 0;
  bool send_invalid_action = false;
  try {
    if (item_id == 0) {
      SYSLOG_WARN("ItemHandler equip invalid item_id (client_id={})", ctx.client_id);
      send_invalid_action = true;
    } else {
      ItemEquipResult result = service_.EquipItem(ctx.client_id, slot, item_id);
      auto payload = BuildEquipRsp(result);
      co_await response_sender_.SendAsync(
          ctx.client_id,
          static_cast<uint16_t>(mir2::common::MsgId::kEquipRsp),
          std::move(payload));

      SYSLOG_DEBUG("ItemHandler equip client_id={} item_id={} slot={} code={}",
                   ctx.client_id, item_id, slot, static_cast<int>(result.code));
      co_return;
    }
  } catch (const std::exception& ex) {
    SYSLOG_ERROR("ItemHandler equip exception client_id={} item_id={} slot={} error={}",
                 ctx.client_id, item_id, slot, ex.what());
    send_invalid_action = true;
  } catch (...) {
    SYSLOG_ERROR("ItemHandler equip exception client_id={} item_id={} slot={} error=unknown",
                 ctx.client_id, item_id, slot);
    send_invalid_action = true;
  }

  if (send_invalid_action) {
    try {
      co_await SendEquipError(ctx, mir2::common::ErrorCode::kInvalidAction);
    } catch (const std::exception& send_ex) {
      SYSLOG_ERROR("ItemHandler equip fallback send failed client_id={} item_id={} slot={} error={}",
                   ctx.client_id, item_id, slot, send_ex.what());
    } catch (...) {
      SYSLOG_ERROR(
          "ItemHandler equip fallback send failed client_id={} item_id={} slot={} error=unknown",
          ctx.client_id, item_id, slot);
    }
  }
  co_return;
}

Task<void> ItemHandler::HandleUnequip(HandlerContext ctx, const mir2::proto::UnequipReq* req) {
  const uint16_t slot = req ? req->slot() : 0;
  bool send_invalid_action = false;
  try {
    ItemUnequipResult result = service_.UnequipItem(ctx.client_id, slot);
    auto payload = BuildUnequipRsp(result);
    co_await response_sender_.SendAsync(
        ctx.client_id,
        static_cast<uint16_t>(mir2::common::MsgId::kUnequipRsp),
        std::move(payload));

    SYSLOG_DEBUG("ItemHandler unequip client_id={} slot={} code={}",
                 ctx.client_id, slot, static_cast<int>(result.code));
    co_return;
  } catch (const std::exception& ex) {
    SYSLOG_ERROR("ItemHandler unequip exception client_id={} slot={} error={}",
                 ctx.client_id, slot, ex.what());
    send_invalid_action = true;
  } catch (...) {
    SYSLOG_ERROR("ItemHandler unequip exception client_id={} slot={} error=unknown",
                 ctx.client_id, slot);
    send_invalid_action = true;
  }

  if (send_invalid_action) {
    try {
      co_await SendUnequipError(ctx, mir2::common::ErrorCode::kInvalidAction);
    } catch (const std::exception& send_ex) {
      SYSLOG_ERROR("ItemHandler unequip fallback send failed client_id={} slot={} error={}",
                   ctx.client_id, slot, send_ex.what());
    } catch (...) {
      SYSLOG_ERROR("ItemHandler unequip fallback send failed client_id={} slot={} error=unknown",
                   ctx.client_id, slot);
    }
  }
  co_return;
}

Task<void> ItemHandler::HandleUse(HandlerContext ctx, const mir2::proto::UseItemReq* req) {
  const uint16_t slot = req ? req->slot() : 0;
  const uint32_t item_id = req ? req->item_id() : 0;
  bool send_invalid_action = false;
  try {
    if (item_id == 0) {
      SYSLOG_WARN("ItemHandler use invalid item_id (client_id={})", ctx.client_id);
      send_invalid_action = true;
    } else {
      ItemUseResult result = service_.UseItem(ctx.client_id, slot, item_id);
      auto payload = BuildUseRsp(result);
      co_await response_sender_.SendAsync(
          ctx.client_id,
          static_cast<uint16_t>(mir2::common::MsgId::kUseItemRsp),
          std::move(payload));

      SYSLOG_DEBUG("ItemHandler use client_id={} item_id={} slot={} code={}",
                   ctx.client_id, item_id, slot, static_cast<int>(result.code));
      co_return;
    }
  } catch (const std::exception& ex) {
    SYSLOG_ERROR("ItemHandler use exception client_id={} item_id={} slot={} error={}",
                 ctx.client_id, item_id, slot, ex.what());
    send_invalid_action = true;
  } catch (...) {
    SYSLOG_ERROR("ItemHandler use exception client_id={} item_id={} slot={} error=unknown",
                 ctx.client_id, item_id, slot);
    send_invalid_action = true;
  }

  if (send_invalid_action) {
    try {
      co_await SendUseError(ctx, mir2::common::ErrorCode::kInvalidAction);
    } catch (const std::exception& send_ex) {
      SYSLOG_ERROR("ItemHandler use fallback send failed client_id={} item_id={} slot={} error={}",
                   ctx.client_id, item_id, slot, send_ex.what());
    } catch (...) {
      SYSLOG_ERROR(
          "ItemHandler use fallback send failed client_id={} item_id={} slot={} error=unknown",
          ctx.client_id, item_id, slot);
    }
  }
  co_return;
}

Task<void> ItemHandler::HandleDrop(HandlerContext ctx, const mir2::proto::DropItemReq* req) {
  const uint16_t slot = req ? req->slot() : 0;
  const uint32_t item_id = req ? req->item_id() : 0;
  const uint32_t count = req ? req->count() : 0;
  bool send_invalid_action = false;
  try {
    if (item_id == 0 || count == 0) {
      SYSLOG_WARN("ItemHandler drop invalid payload (client_id={})", ctx.client_id);
      send_invalid_action = true;
    } else {
      ItemDropResult result = service_.DropItem(ctx.client_id, slot, item_id, count);
      auto payload = BuildDropRsp(result);
      co_await response_sender_.SendAsync(
          ctx.client_id,
          static_cast<uint16_t>(mir2::common::MsgId::kDropItemRsp),
          std::move(payload));

      SYSLOG_DEBUG("ItemHandler drop client_id={} item_id={} slot={} count={} code={}",
                   ctx.client_id, item_id, slot, count, static_cast<int>(result.code));
      co_return;
    }
  } catch (const std::exception& ex) {
    SYSLOG_ERROR("ItemHandler drop exception client_id={} item_id={} slot={} count={} error={}",
                 ctx.client_id, item_id, slot, count, ex.what());
    send_invalid_action = true;
  } catch (...) {
    SYSLOG_ERROR("ItemHandler drop exception client_id={} item_id={} slot={} count={} error=unknown",
                 ctx.client_id, item_id, slot, count);
    send_invalid_action = true;
  }

  if (send_invalid_action) {
    try {
      co_await SendDropError(ctx, mir2::common::ErrorCode::kInvalidAction);
    } catch (const std::exception& send_ex) {
      SYSLOG_ERROR(
          "ItemHandler drop fallback send failed client_id={} item_id={} slot={} count={} error={}",
          ctx.client_id, item_id, slot, count, send_ex.what());
    } catch (...) {
      SYSLOG_ERROR(
          "ItemHandler drop fallback send failed client_id={} item_id={} slot={} count={} error=unknown",
          ctx.client_id, item_id, slot, count);
    }
  }
  co_return;
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

Task<void> ItemHandler::SendEquipError(HandlerContext ctx, mir2::common::ErrorCode code) {
  ItemEquipResult result;
  result.code = code;
  auto payload = BuildEquipRsp(result);
  co_await response_sender_.SendAsync(
      ctx.client_id,
      static_cast<uint16_t>(mir2::common::MsgId::kEquipRsp),
      std::move(payload));
}

Task<void> ItemHandler::SendUnequipError(HandlerContext ctx, mir2::common::ErrorCode code) {
  ItemUnequipResult result;
  result.code = code;
  auto payload = BuildUnequipRsp(result);
  co_await response_sender_.SendAsync(
      ctx.client_id,
      static_cast<uint16_t>(mir2::common::MsgId::kUnequipRsp),
      std::move(payload));
}

}  // namespace mir2::logic
