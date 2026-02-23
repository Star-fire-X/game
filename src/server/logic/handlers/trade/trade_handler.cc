#include "logic/handlers/trade/trade_handler.h"

#include <array>
#include <exception>
#include <limits>
#include <utility>
#include <vector>

#include <flatbuffers/flatbuffers.h>

#include "common/enums.h"
#include "core/utils.h"
#include "ecs/components/character_components.h"
#include "ecs/components/item_component.h"
#include "ecs/components/trade_component.h"
#include "ecs/systems/trade_system.h"
#include "log/logger.h"
#include "logic/handlers/handler_error_utils.h"
#include "logic/response_sender.h"
#include "logic/services/client_registry.h"
#include "logic/services/ecs_inventory_service.h"
#include "logic/services/session_role_store.h"
#include "trade_generated.h"

namespace mir2::logic {

namespace {

std::vector<uint8_t> BuildTradeRspPayload(bool success,
                                          mir2::common::ErrorCode code,
                                          uint64_t trade_id) {
  flatbuffers::FlatBufferBuilder builder;
  const auto rsp = mir2::proto::CreateTradeRsp(
      builder,
      success,
      static_cast<int>(ToProtoError(code)),
      trade_id);
  builder.Finish(rsp);
  const uint8_t* data = builder.GetBufferPointer();
  return {data, data + builder.GetSize()};
}

std::vector<uint8_t> BuildTradeAddItemRspPayload(bool success,
                                                 mir2::common::ErrorCode code) {
  flatbuffers::FlatBufferBuilder builder;
  const auto rsp = mir2::proto::CreateTradeAddItemRsp(
      builder,
      success,
      static_cast<int>(ToProtoError(code)));
  builder.Finish(rsp);
  const uint8_t* data = builder.GetBufferPointer();
  return {data, data + builder.GetSize()};
}

std::vector<uint8_t> BuildTradeSetGoldRspPayload(bool success,
                                                 mir2::common::ErrorCode code) {
  flatbuffers::FlatBufferBuilder builder;
  const auto rsp = mir2::proto::CreateTradeSetGoldRsp(
      builder,
      success,
      static_cast<int>(ToProtoError(code)));
  builder.Finish(rsp);
  const uint8_t* data = builder.GetBufferPointer();
  return {data, data + builder.GetSize()};
}

std::vector<uint8_t> BuildTradeConfirmRspPayload(bool success,
                                                 mir2::common::ErrorCode code) {
  flatbuffers::FlatBufferBuilder builder;
  const auto rsp = mir2::proto::CreateTradeConfirmRsp(
      builder,
      success,
      static_cast<int>(ToProtoError(code)));
  builder.Finish(rsp);
  const uint8_t* data = builder.GetBufferPointer();
  return {data, data + builder.GetSize()};
}

std::vector<uint8_t> BuildTradeCancelRspPayload(bool success,
                                                mir2::common::ErrorCode code) {
  flatbuffers::FlatBufferBuilder builder;
  const auto rsp = mir2::proto::CreateTradeCancelRsp(
      builder,
      success,
      static_cast<int>(ToProtoError(code)));
  builder.Finish(rsp);
  const uint8_t* data = builder.GetBufferPointer();
  return {data, data + builder.GetSize()};
}

std::vector<uint8_t> BuildTradeCompletePayload(uint64_t trade_id,
                                               bool success,
                                               mir2::common::ErrorCode code) {
  flatbuffers::FlatBufferBuilder builder;
  const auto complete = mir2::proto::CreateTradeComplete(
      builder,
      trade_id,
      success,
      static_cast<int>(ToProtoError(code)));
  builder.Finish(complete);
  const uint8_t* data = builder.GetBufferPointer();
  return {data, data + builder.GetSize()};
}

uint32_t SafeToUint32(uint64_t value) {
  return value > static_cast<uint64_t>(std::numeric_limits<uint32_t>::max())
             ? std::numeric_limits<uint32_t>::max()
             : static_cast<uint32_t>(value);
}

}  // namespace

TradeHandler::TradeHandler(ResponseSender& response_sender,
                           ClientRegistry& client_registry,
                           entt::registry& ecs_registry,
                           RoleStore* role_store,
                           EcsInventoryService* inventory_service)
    : response_sender_(response_sender),
      client_registry_(client_registry),
      ecs_registry_(ecs_registry),
      role_store_(role_store),
      inventory_service_(inventory_service) {}

Task<void> TradeHandler::HandleMessage(HandlerContext ctx,
                                       const uint8_t* payload,
                                       size_t payload_size) {
  try {
    if (!payload || payload_size == 0) {
      SYSLOG_WARN("TradeHandler ignored empty payload (client_id={}, msg_id={})",
                  ctx.client_id,
                  ctx.msg_id);
      co_return;
    }

    switch (static_cast<mir2::common::MsgId>(ctx.msg_id)) {
      case mir2::common::MsgId::kTradeReq: {
        flatbuffers::Verifier verifier(payload, payload_size);
        if (!verifier.VerifyBuffer<mir2::proto::TradeReq>(nullptr)) {
          co_await SendTradeRsp(
              ctx.client_id, false, mir2::common::ErrorCode::kInvalidAction, 0);
          co_return;
        }
        const auto* req = flatbuffers::GetRoot<mir2::proto::TradeReq>(payload);
        co_await HandleTradeRequest(std::move(ctx), req);
        co_return;
      }
      case mir2::common::MsgId::kTradeAddItemReq: {
        flatbuffers::Verifier verifier(payload, payload_size);
        if (!verifier.VerifyBuffer<mir2::proto::TradeAddItemReq>(nullptr)) {
          co_await SendTradeAddItemRsp(
              ctx.client_id, false, mir2::common::ErrorCode::kInvalidAction);
          co_return;
        }
        const auto* req = flatbuffers::GetRoot<mir2::proto::TradeAddItemReq>(payload);
        co_await HandleAddItem(std::move(ctx), req);
        co_return;
      }
      case mir2::common::MsgId::kTradeSetGoldReq: {
        flatbuffers::Verifier verifier(payload, payload_size);
        if (!verifier.VerifyBuffer<mir2::proto::TradeSetGoldReq>(nullptr)) {
          co_await SendTradeSetGoldRsp(
              ctx.client_id, false, mir2::common::ErrorCode::kInvalidAction);
          co_return;
        }
        const auto* req = flatbuffers::GetRoot<mir2::proto::TradeSetGoldReq>(payload);
        co_await HandleSetGold(std::move(ctx), req);
        co_return;
      }
      case mir2::common::MsgId::kTradeConfirmReq: {
        flatbuffers::Verifier verifier(payload, payload_size);
        if (!verifier.VerifyBuffer<mir2::proto::TradeConfirmReq>(nullptr)) {
          co_await SendTradeConfirmRsp(
              ctx.client_id, false, mir2::common::ErrorCode::kInvalidAction);
          co_return;
        }
        const auto* req = flatbuffers::GetRoot<mir2::proto::TradeConfirmReq>(payload);
        co_await HandleConfirm(std::move(ctx), req);
        co_return;
      }
      case mir2::common::MsgId::kTradeCancelReq: {
        flatbuffers::Verifier verifier(payload, payload_size);
        if (!verifier.VerifyBuffer<mir2::proto::TradeCancelReq>(nullptr)) {
          co_await SendTradeCancelRsp(
              ctx.client_id, false, mir2::common::ErrorCode::kInvalidAction);
          co_return;
        }
        const auto* req = flatbuffers::GetRoot<mir2::proto::TradeCancelReq>(payload);
        co_await HandleCancel(std::move(ctx), req);
        co_return;
      }
      default:
        SYSLOG_WARN("TradeHandler unsupported msg_id={} client_id={}",
                    ctx.msg_id,
                    ctx.client_id);
        co_return;
    }
  } catch (const std::exception& ex) {
    SYSLOG_ERROR("TradeHandler exception msg_id={} client_id={} error={}",
                 ctx.msg_id,
                 ctx.client_id,
                 ex.what());
    co_return;
  } catch (...) {
    SYSLOG_ERROR("TradeHandler exception msg_id={} client_id={} error=unknown",
                 ctx.msg_id,
                 ctx.client_id);
    co_return;
  }
}

Task<void> TradeHandler::HandleTradeRequest(HandlerContext ctx,
                                            const mir2::proto::TradeReq* req) {
  if (!req || ctx.entity == entt::null || !ecs_registry_.valid(ctx.entity)) {
    co_await SendTradeRsp(ctx.client_id,
                          false,
                          mir2::common::ErrorCode::kInvalidAction,
                          0);
    co_return;
  }

  const uint64_t target_character_id = req->target_character_id();
  const auto self_character_id = GetCharacterId(ctx.entity);
  if (!self_character_id.has_value() || target_character_id == 0 ||
      *self_character_id == target_character_id) {
    co_await SendTradeRsp(ctx.client_id,
                          false,
                          mir2::common::ErrorCode::kTargetNotFound,
                          0);
    co_return;
  }

  auto* self_trade = ecs_registry_.try_get<mir2::ecs::TradeComponent>(ctx.entity);
  if (self_trade && self_trade->state == mir2::ecs::TradeState::kPending) {
    const entt::entity pending_partner = self_trade->partner;
    const auto pending_partner_character_id = GetCharacterId(pending_partner);
    if (pending_partner == entt::null || !ecs_registry_.valid(pending_partner) ||
        !pending_partner_character_id.has_value() ||
        *pending_partner_character_id != target_character_id) {
      co_await SendTradeRsp(ctx.client_id,
                            false,
                            mir2::common::ErrorCode::kTradeInvalidState,
                            0);
      co_return;
    }

    if (!mir2::ecs::TradeSystem::AcceptTrade(ecs_registry_, ctx.entity, nullptr)) {
      co_await SendTradeRsp(ctx.client_id,
                            false,
                            mir2::common::ErrorCode::kTradeInvalidState,
                            0);
      co_return;
    }

    const auto runtime = ResolveRuntime(ctx.entity);
    if (!runtime.has_value()) {
      co_await SendTradeRsp(ctx.client_id,
                            false,
                            mir2::common::ErrorCode::kTradeInvalidState,
                            0);
      co_return;
    }

    co_await SendTradeRsp(
        ctx.client_id, true, mir2::common::ErrorCode::kOk, runtime->trade_id);

    const uint64_t partner_client_id =
        runtime->self == ctx.entity ? runtime->partner_client_id : runtime->self_client_id;
    if (partner_client_id != 0 && partner_client_id != ctx.client_id) {
      co_await SendTradeRsp(
          partner_client_id, true, mir2::common::ErrorCode::kOk, runtime->trade_id);
    }

    co_await SendTradeUpdate(*runtime);
    co_return;
  }

  if (self_trade && self_trade->state != mir2::ecs::TradeState::kNone) {
    co_await SendTradeRsp(ctx.client_id,
                          false,
                          mir2::common::ErrorCode::kTradeTargetBusy,
                          0);
    co_return;
  }

  const auto target_entity = FindOnlineEntityByCharacterId(target_character_id);
  if (!target_entity.has_value()) {
    co_await SendTradeRsp(ctx.client_id,
                          false,
                          mir2::common::ErrorCode::kTargetNotFound,
                          0);
    co_return;
  }

  if (!mir2::ecs::TradeSystem::RequestTrade(
          ecs_registry_, ctx.entity, *target_entity, nullptr)) {
    co_await SendTradeRsp(ctx.client_id,
                          false,
                          mir2::common::ErrorCode::kTradeTargetBusy,
                          0);
    co_return;
  }

  const auto runtime = ResolveRuntime(ctx.entity);
  if (!runtime.has_value()) {
    co_await SendTradeRsp(ctx.client_id,
                          false,
                          mir2::common::ErrorCode::kTradeInvalidState,
                          0);
    co_return;
  }

  co_await SendTradeRsp(
      ctx.client_id, true, mir2::common::ErrorCode::kOk, runtime->trade_id);

  const uint64_t target_client_id =
      GetClientIdByCharacterId(target_character_id).value_or(target_character_id);
  if (target_client_id != 0 && target_client_id != ctx.client_id) {
    co_await SendTradeRsp(target_client_id,
                          true,
                          mir2::common::ErrorCode::kOk,
                          runtime->trade_id);
  }
}

Task<void> TradeHandler::HandleAddItem(HandlerContext ctx,
                                       const mir2::proto::TradeAddItemReq* req) {
  if (!req || ctx.entity == entt::null || !ecs_registry_.valid(ctx.entity)) {
    co_await SendTradeAddItemRsp(
        ctx.client_id, false, mir2::common::ErrorCode::kInvalidAction);
    co_return;
  }

  const auto runtime = ResolveRuntime(ctx.entity);
  if (!runtime.has_value() || req->trade_id() != runtime->trade_id) {
    co_await SendTradeAddItemRsp(
        ctx.client_id, false, mir2::common::ErrorCode::kTradeInvalidState);
    co_return;
  }

  const entt::entity item_entity = FindInventoryItem(
      ctx.entity, req->inventory_slot(), req->item_id(), req->count());
  if (item_entity == entt::null) {
    co_await SendTradeAddItemRsp(
        ctx.client_id, false, mir2::common::ErrorCode::kTargetNotFound);
    co_return;
  }

  if (!mir2::ecs::TradeSystem::AddTradeItem(
          ecs_registry_, ctx.entity, item_entity, nullptr)) {
    co_await SendTradeAddItemRsp(
        ctx.client_id, false, mir2::common::ErrorCode::kTradeInvalidState);
    co_return;
  }

  co_await SendTradeAddItemRsp(ctx.client_id, true, mir2::common::ErrorCode::kOk);

  const auto updated_runtime = ResolveRuntime(ctx.entity);
  if (updated_runtime.has_value()) {
    co_await SendTradeUpdate(*updated_runtime);
  }
}

Task<void> TradeHandler::HandleSetGold(HandlerContext ctx,
                                       const mir2::proto::TradeSetGoldReq* req) {
  if (!req || ctx.entity == entt::null || !ecs_registry_.valid(ctx.entity)) {
    co_await SendTradeSetGoldRsp(
        ctx.client_id, false, mir2::common::ErrorCode::kInvalidAction);
    co_return;
  }

  const auto runtime = ResolveRuntime(ctx.entity);
  if (!runtime.has_value() || req->trade_id() != runtime->trade_id) {
    co_await SendTradeSetGoldRsp(
        ctx.client_id, false, mir2::common::ErrorCode::kTradeInvalidState);
    co_return;
  }

  if (!mir2::ecs::TradeSystem::SetTradeGold(
          ecs_registry_, ctx.entity, static_cast<int>(req->gold()))) {
    co_await SendTradeSetGoldRsp(
        ctx.client_id, false, mir2::common::ErrorCode::kTradeInvalidState);
    co_return;
  }

  co_await SendTradeSetGoldRsp(ctx.client_id, true, mir2::common::ErrorCode::kOk);

  const auto updated_runtime = ResolveRuntime(ctx.entity);
  if (updated_runtime.has_value()) {
    co_await SendTradeUpdate(*updated_runtime);
  }
}

Task<void> TradeHandler::HandleConfirm(HandlerContext ctx,
                                       const mir2::proto::TradeConfirmReq* req) {
  if (!req || ctx.entity == entt::null || !ecs_registry_.valid(ctx.entity)) {
    co_await SendTradeConfirmRsp(
        ctx.client_id, false, mir2::common::ErrorCode::kInvalidAction);
    co_return;
  }

  const auto runtime = ResolveRuntime(ctx.entity);
  if (!runtime.has_value() || req->trade_id() != runtime->trade_id) {
    co_await SendTradeConfirmRsp(
        ctx.client_id, false, mir2::common::ErrorCode::kTradeInvalidState);
    co_return;
  }

  const bool partner_already_confirmed = runtime->partner_confirmed;

  if (!mir2::ecs::TradeSystem::ConfirmTrade(
          ecs_registry_, ctx.entity, nullptr, /*auto_execute=*/false)) {
    co_await SendTradeConfirmRsp(
        ctx.client_id, false, mir2::common::ErrorCode::kTradeInvalidState);
    co_return;
  }

  co_await SendTradeConfirmRsp(ctx.client_id, true, mir2::common::ErrorCode::kOk);

  if (partner_already_confirmed) {
    bool executed = false;
    if (inventory_service_) {
      executed = inventory_service_->ExecuteTradeAtomic(
          ecs_registry_, runtime->self, runtime->partner, nullptr);
    } else {
      executed = mir2::ecs::TradeSystem::ExecuteTrade(
          ecs_registry_, runtime->self, runtime->partner, nullptr);
    }

    if (!executed) {
      mir2::ecs::TradeSystem::CancelTrade(ecs_registry_, ctx.entity, nullptr);
      co_await SendTradeComplete(
          *runtime, false, mir2::common::ErrorCode::kTradeInvalidState);
      co_return;
    }

    co_await SendTradeComplete(*runtime, true, mir2::common::ErrorCode::kOk);
    co_return;
  }

  const auto updated_runtime = ResolveRuntime(ctx.entity);
  if (updated_runtime.has_value()) {
    co_await SendTradeUpdate(*updated_runtime);
  }
}

Task<void> TradeHandler::HandleCancel(HandlerContext ctx,
                                      const mir2::proto::TradeCancelReq* req) {
  if (!req || ctx.entity == entt::null || !ecs_registry_.valid(ctx.entity)) {
    co_await SendTradeCancelRsp(
        ctx.client_id, false, mir2::common::ErrorCode::kInvalidAction);
    co_return;
  }

  const auto runtime = ResolveRuntime(ctx.entity);
  if (!runtime.has_value() || req->trade_id() != runtime->trade_id) {
    co_await SendTradeCancelRsp(
        ctx.client_id, false, mir2::common::ErrorCode::kTradeInvalidState);
    co_return;
  }

  if (!mir2::ecs::TradeSystem::CancelTrade(ecs_registry_, ctx.entity, nullptr)) {
    co_await SendTradeCancelRsp(
        ctx.client_id, false, mir2::common::ErrorCode::kTradeInvalidState);
    co_return;
  }

  MarkTradeClosed(runtime->self);
  MarkTradeClosed(runtime->partner);

  co_await SendTradeCancelRsp(ctx.client_id, true, mir2::common::ErrorCode::kOk);

  const uint64_t partner_client_id =
      runtime->self == ctx.entity ? runtime->partner_client_id : runtime->self_client_id;
  if (partner_client_id != 0 && partner_client_id != ctx.client_id) {
    co_await SendTradeCancelRsp(partner_client_id,
                                true,
                                mir2::common::ErrorCode::kOk);
  }

  co_await SendTradeComplete(*runtime,
                             false,
                             mir2::common::ErrorCode::kOk);
}

Task<void> TradeHandler::SendTradeRsp(uint64_t client_id,
                                      bool success,
                                      mir2::common::ErrorCode code,
                                      uint64_t trade_id) {
  if (client_id == 0) {
    co_return;
  }
  auto payload = BuildTradeRspPayload(success, code, trade_id);
  co_await response_sender_.SendAsync(
      client_id,
      static_cast<uint16_t>(mir2::common::MsgId::kTradeRsp),
      std::move(payload));
}

Task<void> TradeHandler::SendTradeAddItemRsp(uint64_t client_id,
                                             bool success,
                                             mir2::common::ErrorCode code) {
  if (client_id == 0) {
    co_return;
  }
  auto payload = BuildTradeAddItemRspPayload(success, code);
  co_await response_sender_.SendAsync(
      client_id,
      static_cast<uint16_t>(mir2::common::MsgId::kTradeAddItemRsp),
      std::move(payload));
}

Task<void> TradeHandler::SendTradeSetGoldRsp(uint64_t client_id,
                                             bool success,
                                             mir2::common::ErrorCode code) {
  if (client_id == 0) {
    co_return;
  }
  auto payload = BuildTradeSetGoldRspPayload(success, code);
  co_await response_sender_.SendAsync(
      client_id,
      static_cast<uint16_t>(mir2::common::MsgId::kTradeSetGoldRsp),
      std::move(payload));
}

Task<void> TradeHandler::SendTradeConfirmRsp(uint64_t client_id,
                                             bool success,
                                             mir2::common::ErrorCode code) {
  if (client_id == 0) {
    co_return;
  }
  auto payload = BuildTradeConfirmRspPayload(success, code);
  co_await response_sender_.SendAsync(
      client_id,
      static_cast<uint16_t>(mir2::common::MsgId::kTradeConfirmRsp),
      std::move(payload));
}

Task<void> TradeHandler::SendTradeCancelRsp(uint64_t client_id,
                                            bool success,
                                            mir2::common::ErrorCode code) {
  if (client_id == 0) {
    co_return;
  }
  auto payload = BuildTradeCancelRspPayload(success, code);
  co_await response_sender_.SendAsync(
      client_id,
      static_cast<uint16_t>(mir2::common::MsgId::kTradeCancelRsp),
      std::move(payload));
}

Task<void> TradeHandler::SendTradeUpdate(const TradeRuntime& runtime) {
  flatbuffers::FlatBufferBuilder builder;

  std::vector<flatbuffers::Offset<mir2::proto::TradeItemInfo>> self_items;
  self_items.reserve(runtime.self_items.size());
  for (const auto& item : runtime.self_items) {
    self_items.emplace_back(mir2::proto::CreateTradeItemInfo(
        builder, item.inventory_slot, item.item_id, item.count));
  }

  std::vector<flatbuffers::Offset<mir2::proto::TradeItemInfo>> partner_items;
  partner_items.reserve(runtime.partner_items.size());
  for (const auto& item : runtime.partner_items) {
    partner_items.emplace_back(mir2::proto::CreateTradeItemInfo(
        builder, item.inventory_slot, item.item_id, item.count));
  }

  const auto self_items_vec = builder.CreateVector(self_items);
  const auto partner_items_vec = builder.CreateVector(partner_items);

  const auto update = mir2::proto::CreateTradeUpdate(
      builder,
      runtime.trade_id,
      SafeToUint32(runtime.self_character_id),
      SafeToUint32(runtime.partner_character_id),
      self_items_vec,
      partner_items_vec,
      runtime.self_gold,
      runtime.partner_gold,
      runtime.self_confirmed,
      runtime.partner_confirmed);
  builder.Finish(update);

  const uint8_t* data = builder.GetBufferPointer();
  std::vector<uint8_t> payload(data, data + builder.GetSize());

  std::array<uint64_t, 2> targets = {runtime.self_client_id, runtime.partner_client_id};
  for (const uint64_t client_id : targets) {
    if (client_id == 0) {
      continue;
    }
    co_await response_sender_.SendAsync(
        client_id,
        static_cast<uint16_t>(mir2::common::MsgId::kTradeUpdate),
        payload);
  }
}

Task<void> TradeHandler::SendTradeComplete(const TradeRuntime& runtime,
                                           bool success,
                                           mir2::common::ErrorCode code) {
  auto payload = BuildTradeCompletePayload(runtime.trade_id, success, code);
  std::array<uint64_t, 2> targets = {runtime.self_client_id, runtime.partner_client_id};
  for (const uint64_t client_id : targets) {
    if (client_id == 0) {
      continue;
    }
    co_await response_sender_.SendAsync(
        client_id,
        static_cast<uint16_t>(mir2::common::MsgId::kTradeComplete),
        payload);
  }
}

std::optional<TradeHandler::TradeRuntime> TradeHandler::ResolveRuntime(
    entt::entity self) const {
  if (self == entt::null || !ecs_registry_.valid(self)) {
    return std::nullopt;
  }

  auto* self_trade = ecs_registry_.try_get<mir2::ecs::TradeComponent>(self);
  if (!self_trade || self_trade->state == mir2::ecs::TradeState::kNone) {
    return std::nullopt;
  }
  if (self_trade->partner == entt::null || !ecs_registry_.valid(self_trade->partner)) {
    return std::nullopt;
  }

  auto* partner_trade =
      ecs_registry_.try_get<mir2::ecs::TradeComponent>(self_trade->partner);
  if (!partner_trade || partner_trade->state == mir2::ecs::TradeState::kNone ||
      partner_trade->partner != self) {
    return std::nullopt;
  }
  if (self_trade->trade_id == 0 || self_trade->trade_id != partner_trade->trade_id) {
    return std::nullopt;
  }

  const auto self_character_id = GetCharacterId(self);
  const auto partner_character_id = GetCharacterId(self_trade->partner);
  if (!self_character_id.has_value() || !partner_character_id.has_value()) {
    return std::nullopt;
  }

  TradeRuntime runtime;
  runtime.self = self;
  runtime.partner = self_trade->partner;
  runtime.self_character_id = *self_character_id;
  runtime.partner_character_id = *partner_character_id;
  runtime.self_client_id =
      GetClientIdByCharacterId(*self_character_id).value_or(*self_character_id);
  runtime.partner_client_id =
      GetClientIdByCharacterId(*partner_character_id).value_or(*partner_character_id);
  runtime.trade_id = self_trade->trade_id;
  runtime.self_confirmed = self_trade->confirmed;
  runtime.partner_confirmed = partner_trade->confirmed;
  runtime.self_gold = self_trade->offered_gold < 0
                          ? 0
                          : static_cast<uint32_t>(self_trade->offered_gold);
  runtime.partner_gold = partner_trade->offered_gold < 0
                             ? 0
                             : static_cast<uint32_t>(partner_trade->offered_gold);
  runtime.self_items = CollectTradeItems(self, *self_trade);
  runtime.partner_items = CollectTradeItems(runtime.partner, *partner_trade);
  return runtime;
}

std::optional<entt::entity> TradeHandler::FindOnlineEntityByCharacterId(
    uint64_t character_id) const {
  if (character_id == 0) {
    return std::nullopt;
  }

  auto view = ecs_registry_.view<mir2::ecs::CharacterIdentityComponent,
                                 mir2::ecs::CharacterStateComponent>();
  for (const auto entity : view) {
    const auto& identity = view.get<mir2::ecs::CharacterIdentityComponent>(entity);
    const auto& state = view.get<mir2::ecs::CharacterStateComponent>(entity);
    if (identity.id != character_id || !state.is_online) {
      continue;
    }
    return entity;
  }

  return std::nullopt;
}

std::optional<uint64_t> TradeHandler::GetCharacterId(entt::entity entity) const {
  if (entity == entt::null || !ecs_registry_.valid(entity)) {
    return std::nullopt;
  }

  const auto* identity = ecs_registry_.try_get<mir2::ecs::CharacterIdentityComponent>(entity);
  if (!identity || identity->id == 0) {
    return std::nullopt;
  }
  return identity->id;
}

std::optional<uint64_t> TradeHandler::GetClientIdByCharacterId(
    uint64_t character_id) const {
  if (character_id == 0) {
    return std::nullopt;
  }

  if (role_store_) {
    auto mapped = role_store_->GetClientIdByRoleId(character_id);
    if (mapped.has_value()) {
      return mapped;
    }
  }

  if (client_registry_.Contains(character_id)) {
    return character_id;
  }

  return character_id;
}

std::vector<TradeHandler::TradeItemView> TradeHandler::CollectTradeItems(
    entt::entity owner,
    const mir2::ecs::TradeComponent& trade) const {
  std::vector<TradeItemView> items;

  for (const entt::entity item_entity : trade.offered_items) {
    if (item_entity == entt::null || !ecs_registry_.valid(item_entity)) {
      continue;
    }

    const auto* owner_comp =
        ecs_registry_.try_get<mir2::ecs::InventoryOwnerComponent>(item_entity);
    const auto* item_comp =
        ecs_registry_.try_get<mir2::ecs::ItemComponent>(item_entity);
    if (!owner_comp || !item_comp || owner_comp->owner != owner ||
        owner_comp->slot_index < 0) {
      continue;
    }

    TradeItemView view;
    view.inventory_slot = owner_comp->slot_index >
                                  static_cast<int>(std::numeric_limits<uint16_t>::max())
                              ? std::numeric_limits<uint16_t>::max()
                              : static_cast<uint16_t>(owner_comp->slot_index);
    view.item_id = item_comp->item_id;
    view.count = item_comp->count < 0
                     ? 0
                     : static_cast<uint32_t>(item_comp->count);
    items.push_back(view);
  }

  return items;
}

entt::entity TradeHandler::FindInventoryItem(entt::entity owner,
                                             uint16_t slot,
                                             uint32_t item_id,
                                             uint32_t count) const {
  if (owner == entt::null || !ecs_registry_.valid(owner) || item_id == 0 || count == 0) {
    return entt::null;
  }

  auto view =
      ecs_registry_.view<mir2::ecs::InventoryOwnerComponent, mir2::ecs::ItemComponent>();
  for (const auto entity : view) {
    const auto& owner_comp = view.get<mir2::ecs::InventoryOwnerComponent>(entity);
    if (owner_comp.owner != owner || owner_comp.slot_index != static_cast<int>(slot)) {
      continue;
    }

    const auto& item_comp = view.get<mir2::ecs::ItemComponent>(entity);
    if (item_comp.item_id != item_id) {
      continue;
    }

    if (item_comp.count < 0 || static_cast<uint32_t>(item_comp.count) != count) {
      return entt::null;
    }

    return entity;
  }

  return entt::null;
}

void TradeHandler::MarkTradeClosed(entt::entity entity) const {
  if (entity == entt::null || !ecs_registry_.valid(entity)) {
    return;
  }

  auto* state = ecs_registry_.try_get<mir2::ecs::CharacterStateComponent>(entity);
  if (!state) {
    return;
  }

  state->last_trade_close_time_ms = mir2::core::GetCurrentTimestampMs();
}

}  // namespace mir2::logic
