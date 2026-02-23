#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <asio/io_context.hpp>
#include <entt/entt.hpp>
#include <flatbuffers/flatbuffers.h>

#include "common/enums.h"
#include "ecs/components/character_components.h"
#include "ecs/components/item_component.h"
#include "ecs/components/trade_component.h"
#include "logic/coroutine_executor.h"
#include "logic/handlers/trade/trade_handler.h"
#include "logic/mock_response_sender.h"
#include "logic/services/client_registry.h"
#include "logic/services/session_role_store.h"
#include "trade_generated.h"

namespace mir2::logic::test {
namespace {

std::vector<uint8_t> BuildTradeReqPayload(uint32_t target_character_id) {
  flatbuffers::FlatBufferBuilder builder;
  const auto req = mir2::proto::CreateTradeReq(builder, target_character_id);
  builder.Finish(req);
  return {builder.GetBufferPointer(), builder.GetBufferPointer() + builder.GetSize()};
}

std::vector<uint8_t> BuildTradeAddItemReqPayload(uint64_t trade_id,
                                                 uint16_t slot,
                                                 uint32_t item_id,
                                                 uint32_t count) {
  flatbuffers::FlatBufferBuilder builder;
  const auto req =
      mir2::proto::CreateTradeAddItemReq(builder, trade_id, slot, item_id, count);
  builder.Finish(req);
  return {builder.GetBufferPointer(), builder.GetBufferPointer() + builder.GetSize()};
}

std::vector<uint8_t> BuildTradeConfirmReqPayload(uint64_t trade_id) {
  flatbuffers::FlatBufferBuilder builder;
  const auto req = mir2::proto::CreateTradeConfirmReq(builder, trade_id);
  builder.Finish(req);
  return {builder.GetBufferPointer(), builder.GetBufferPointer() + builder.GetSize()};
}

std::vector<uint8_t> BuildTradeCancelReqPayload(uint64_t trade_id) {
  flatbuffers::FlatBufferBuilder builder;
  const auto req = mir2::proto::CreateTradeCancelReq(builder, trade_id);
  builder.Finish(req);
  return {builder.GetBufferPointer(), builder.GetBufferPointer() + builder.GetSize()};
}

class TradeHandlerTest : public ::testing::Test {
 protected:
  struct Player {
    uint64_t character_id = 0;
    uint64_t client_id = 0;
    entt::entity entity = entt::null;
  };

  void SetUp() override {
    executor_ = std::make_unique<CoroutineExecutor>(io_context_, 1);
    response_sender_ = std::make_unique<MockResponseSender>();
    handler_ = std::make_unique<TradeHandler>(*response_sender_,
                                              client_registry_,
                                              registry_,
                                              &role_store_,
                                              nullptr);
  }

  void RunIoContext() {
    io_context_.run();
    io_context_.restart();
  }

  void Dispatch(const HandlerContext& ctx, const std::vector<uint8_t>& payload) {
    ASSERT_TRUE(executor_->Spawn(handler_->HandleMessage(ctx, payload.data(), payload.size())));
    RunIoContext();
  }

  Player CreatePlayer(uint64_t character_id,
                      uint64_t client_id,
                      const std::string& name,
                      int gold) {
    const entt::entity entity = registry_.create();

    auto& identity = registry_.emplace<ecs::CharacterIdentityComponent>(entity);
    identity.id = static_cast<uint32_t>(character_id);
    identity.account_id = static_cast<ecs::AccountId>(character_id);
    identity.name = name;

    auto& state = registry_.emplace<ecs::CharacterStateComponent>(entity);
    state.is_online = true;

    auto& attributes = registry_.emplace<ecs::CharacterAttributesComponent>(entity);
    attributes.gold = gold;

    client_registry_.Track(client_id);
    role_store_.BindClientRole(client_id, character_id);

    return Player{character_id, client_id, entity};
  }

  void CreateInventoryItem(entt::entity owner,
                           int slot,
                           uint32_t item_id,
                           int count) {
    const entt::entity item = registry_.create();
    auto& item_comp = registry_.emplace<ecs::ItemComponent>(item);
    item_comp.item_id = item_id;
    item_comp.count = count;
    item_comp.instance_id = static_cast<uint32_t>(entt::to_integral(item));

    auto& owner_comp = registry_.emplace<ecs::InventoryOwnerComponent>(item);
    owner_comp.owner = owner;
    owner_comp.slot_index = slot;
  }

  std::optional<CapturedResponse> FindResponse(uint64_t client_id,
                                               uint16_t msg_id) const {
    const auto responses = response_sender_->GetCapturedResponses();
    auto it = std::find_if(
        responses.begin(),
        responses.end(),
        [client_id, msg_id](const CapturedResponse& response) {
          return response.client_id == client_id && response.msg_id == msg_id;
        });
    if (it == responses.end()) {
      return std::nullopt;
    }
    return *it;
  }

  size_t CountResponses(uint16_t msg_id) const {
    const auto responses = response_sender_->GetCapturedResponses();
    return static_cast<size_t>(std::count_if(
        responses.begin(),
        responses.end(),
        [msg_id](const CapturedResponse& response) {
          return response.msg_id == msg_id;
        }));
  }

  asio::io_context io_context_;
  entt::registry registry_;
  std::unique_ptr<CoroutineExecutor> executor_;
  std::unique_ptr<MockResponseSender> response_sender_;
  ClientRegistry client_registry_;
  RoleStore role_store_;
  std::unique_ptr<TradeHandler> handler_;
};

TEST_F(TradeHandlerTest, TradeConfirmFlowSendsCompleteAndSetsCloseCooldown) {
  const auto left = CreatePlayer(1001, 5001, "Left", 1000);
  const auto right = CreatePlayer(1002, 5002, "Right", 1000);
  CreateInventoryItem(left.entity, 1, 2001, 1);

  HandlerContext req_ctx;
  req_ctx.client_id = left.client_id;
  req_ctx.msg_id = static_cast<uint16_t>(mir2::common::MsgId::kTradeReq);
  req_ctx.entity = left.entity;
  Dispatch(req_ctx, BuildTradeReqPayload(static_cast<uint32_t>(right.character_id)));

  const auto trade_rsp = FindResponse(
      left.client_id, static_cast<uint16_t>(mir2::common::MsgId::kTradeRsp));
  ASSERT_TRUE(trade_rsp.has_value());
  flatbuffers::Verifier trade_rsp_verifier(
      trade_rsp->payload.data(), trade_rsp->payload.size());
  ASSERT_TRUE(trade_rsp_verifier.VerifyBuffer<mir2::proto::TradeRsp>(nullptr));
  const auto* trade_rsp_root =
      flatbuffers::GetRoot<mir2::proto::TradeRsp>(trade_rsp->payload.data());
  ASSERT_NE(trade_rsp_root, nullptr);
  ASSERT_TRUE(trade_rsp_root->success());
  const uint64_t trade_id = trade_rsp_root->trade_id();

  const auto invited_rsp = FindResponse(
      right.client_id, static_cast<uint16_t>(mir2::common::MsgId::kTradeRsp));
  ASSERT_TRUE(invited_rsp.has_value());
  flatbuffers::Verifier invited_rsp_verifier(
      invited_rsp->payload.data(), invited_rsp->payload.size());
  ASSERT_TRUE(invited_rsp_verifier.VerifyBuffer<mir2::proto::TradeRsp>(nullptr));
  const auto* invited_rsp_root =
      flatbuffers::GetRoot<mir2::proto::TradeRsp>(invited_rsp->payload.data());
  ASSERT_NE(invited_rsp_root, nullptr);
  ASSERT_TRUE(invited_rsp_root->success());
  ASSERT_EQ(invited_rsp_root->trade_id(), trade_id);

  response_sender_->Clear();

  HandlerContext accept_ctx;
  accept_ctx.client_id = right.client_id;
  accept_ctx.msg_id = static_cast<uint16_t>(mir2::common::MsgId::kTradeReq);
  accept_ctx.entity = right.entity;
  Dispatch(accept_ctx, BuildTradeReqPayload(static_cast<uint32_t>(left.character_id)));

  const auto accept_rsp_left = FindResponse(
      left.client_id, static_cast<uint16_t>(mir2::common::MsgId::kTradeRsp));
  const auto accept_rsp_right = FindResponse(
      right.client_id, static_cast<uint16_t>(mir2::common::MsgId::kTradeRsp));
  ASSERT_TRUE(accept_rsp_left.has_value());
  ASSERT_TRUE(accept_rsp_right.has_value());
  EXPECT_GE(CountResponses(static_cast<uint16_t>(mir2::common::MsgId::kTradeUpdate)), 2u);

  response_sender_->Clear();

  HandlerContext add_ctx;
  add_ctx.client_id = left.client_id;
  add_ctx.msg_id = static_cast<uint16_t>(mir2::common::MsgId::kTradeAddItemReq);
  add_ctx.entity = left.entity;
  Dispatch(add_ctx, BuildTradeAddItemReqPayload(trade_id, 1, 2001, 1));

  const auto add_rsp = FindResponse(
      left.client_id, static_cast<uint16_t>(mir2::common::MsgId::kTradeAddItemRsp));
  ASSERT_TRUE(add_rsp.has_value());
  flatbuffers::Verifier add_verifier(add_rsp->payload.data(), add_rsp->payload.size());
  ASSERT_TRUE(add_verifier.VerifyBuffer<mir2::proto::TradeAddItemRsp>(nullptr));
  const auto* add_root =
      flatbuffers::GetRoot<mir2::proto::TradeAddItemRsp>(add_rsp->payload.data());
  ASSERT_NE(add_root, nullptr);
  ASSERT_TRUE(add_root->success());

  response_sender_->Clear();

  HandlerContext left_confirm_ctx;
  left_confirm_ctx.client_id = left.client_id;
  left_confirm_ctx.msg_id = static_cast<uint16_t>(mir2::common::MsgId::kTradeConfirmReq);
  left_confirm_ctx.entity = left.entity;
  Dispatch(left_confirm_ctx, BuildTradeConfirmReqPayload(trade_id));

  const auto left_confirm_rsp = FindResponse(
      left.client_id, static_cast<uint16_t>(mir2::common::MsgId::kTradeConfirmRsp));
  ASSERT_TRUE(left_confirm_rsp.has_value());

  response_sender_->Clear();

  HandlerContext right_confirm_ctx;
  right_confirm_ctx.client_id = right.client_id;
  right_confirm_ctx.msg_id = static_cast<uint16_t>(mir2::common::MsgId::kTradeConfirmReq);
  right_confirm_ctx.entity = right.entity;
  Dispatch(right_confirm_ctx, BuildTradeConfirmReqPayload(trade_id));

  const auto right_confirm_rsp = FindResponse(
      right.client_id, static_cast<uint16_t>(mir2::common::MsgId::kTradeConfirmRsp));
  ASSERT_TRUE(right_confirm_rsp.has_value());

  const auto left_complete = FindResponse(
      left.client_id, static_cast<uint16_t>(mir2::common::MsgId::kTradeComplete));
  const auto right_complete = FindResponse(
      right.client_id, static_cast<uint16_t>(mir2::common::MsgId::kTradeComplete));
  ASSERT_TRUE(left_complete.has_value());
  ASSERT_TRUE(right_complete.has_value());

  flatbuffers::Verifier complete_verifier(
      left_complete->payload.data(), left_complete->payload.size());
  ASSERT_TRUE(complete_verifier.VerifyBuffer<mir2::proto::TradeComplete>(nullptr));
  const auto* complete_root =
      flatbuffers::GetRoot<mir2::proto::TradeComplete>(left_complete->payload.data());
  ASSERT_NE(complete_root, nullptr);
  EXPECT_EQ(complete_root->trade_id(), trade_id);
  EXPECT_TRUE(complete_root->success());

  const auto& left_state = registry_.get<ecs::CharacterStateComponent>(left.entity);
  const auto& right_state = registry_.get<ecs::CharacterStateComponent>(right.entity);
  EXPECT_GT(left_state.last_trade_close_time_ms, 0);
  EXPECT_GT(right_state.last_trade_close_time_ms, 0);
}

TEST_F(TradeHandlerTest, TradeAddItemRejectsInvalidTradeId) {
  const auto left = CreatePlayer(2001, 6001, "Left", 1000);
  const auto right = CreatePlayer(2002, 6002, "Right", 1000);
  CreateInventoryItem(left.entity, 2, 3001, 1);

  HandlerContext req_ctx;
  req_ctx.client_id = left.client_id;
  req_ctx.msg_id = static_cast<uint16_t>(mir2::common::MsgId::kTradeReq);
  req_ctx.entity = left.entity;
  Dispatch(req_ctx, BuildTradeReqPayload(static_cast<uint32_t>(right.character_id)));

  const auto trade_rsp = FindResponse(
      left.client_id, static_cast<uint16_t>(mir2::common::MsgId::kTradeRsp));
  ASSERT_TRUE(trade_rsp.has_value());
  flatbuffers::Verifier trade_rsp_verifier(
      trade_rsp->payload.data(), trade_rsp->payload.size());
  ASSERT_TRUE(trade_rsp_verifier.VerifyBuffer<mir2::proto::TradeRsp>(nullptr));
  const auto* trade_rsp_root =
      flatbuffers::GetRoot<mir2::proto::TradeRsp>(trade_rsp->payload.data());
  ASSERT_NE(trade_rsp_root, nullptr);
  ASSERT_TRUE(trade_rsp_root->success());
  const uint64_t trade_id = trade_rsp_root->trade_id();

  response_sender_->Clear();

  HandlerContext accept_ctx;
  accept_ctx.client_id = right.client_id;
  accept_ctx.msg_id = static_cast<uint16_t>(mir2::common::MsgId::kTradeReq);
  accept_ctx.entity = right.entity;
  Dispatch(accept_ctx, BuildTradeReqPayload(static_cast<uint32_t>(left.character_id)));

  const auto accept_rsp_left = FindResponse(
      left.client_id, static_cast<uint16_t>(mir2::common::MsgId::kTradeRsp));
  const auto accept_rsp_right = FindResponse(
      right.client_id, static_cast<uint16_t>(mir2::common::MsgId::kTradeRsp));
  ASSERT_TRUE(accept_rsp_left.has_value());
  ASSERT_TRUE(accept_rsp_right.has_value());

  const uint64_t invalid_trade_id = trade_id + 1;

  response_sender_->Clear();

  HandlerContext add_ctx;
  add_ctx.client_id = left.client_id;
  add_ctx.msg_id = static_cast<uint16_t>(mir2::common::MsgId::kTradeAddItemReq);
  add_ctx.entity = left.entity;
  Dispatch(add_ctx, BuildTradeAddItemReqPayload(invalid_trade_id, 2, 3001, 1));

  const auto add_rsp = FindResponse(
      left.client_id, static_cast<uint16_t>(mir2::common::MsgId::kTradeAddItemRsp));
  ASSERT_TRUE(add_rsp.has_value());

  flatbuffers::Verifier add_verifier(add_rsp->payload.data(), add_rsp->payload.size());
  ASSERT_TRUE(add_verifier.VerifyBuffer<mir2::proto::TradeAddItemRsp>(nullptr));
  const auto* add_root =
      flatbuffers::GetRoot<mir2::proto::TradeAddItemRsp>(add_rsp->payload.data());
  ASSERT_NE(add_root, nullptr);
  EXPECT_FALSE(add_root->success());
  EXPECT_EQ(add_root->error_code(),
            static_cast<int>(mir2::proto::ErrorCode::ERR_TRADE_INVALID_STATE));
}

TEST_F(TradeHandlerTest, TradeCancelNotifiesBothSides) {
  const auto left = CreatePlayer(3001, 7001, "Left", 1000);
  const auto right = CreatePlayer(3002, 7002, "Right", 1000);

  HandlerContext req_ctx;
  req_ctx.client_id = left.client_id;
  req_ctx.msg_id = static_cast<uint16_t>(mir2::common::MsgId::kTradeReq);
  req_ctx.entity = left.entity;
  Dispatch(req_ctx, BuildTradeReqPayload(static_cast<uint32_t>(right.character_id)));

  const auto trade_rsp = FindResponse(
      left.client_id, static_cast<uint16_t>(mir2::common::MsgId::kTradeRsp));
  ASSERT_TRUE(trade_rsp.has_value());
  flatbuffers::Verifier trade_rsp_verifier(
      trade_rsp->payload.data(), trade_rsp->payload.size());
  ASSERT_TRUE(trade_rsp_verifier.VerifyBuffer<mir2::proto::TradeRsp>(nullptr));
  const auto* trade_rsp_root =
      flatbuffers::GetRoot<mir2::proto::TradeRsp>(trade_rsp->payload.data());
  ASSERT_NE(trade_rsp_root, nullptr);
  const uint64_t trade_id = trade_rsp_root->trade_id();

  response_sender_->Clear();

  HandlerContext cancel_ctx;
  cancel_ctx.client_id = left.client_id;
  cancel_ctx.msg_id = static_cast<uint16_t>(mir2::common::MsgId::kTradeCancelReq);
  cancel_ctx.entity = left.entity;
  Dispatch(cancel_ctx, BuildTradeCancelReqPayload(trade_id));

  const auto left_cancel_rsp = FindResponse(
      left.client_id, static_cast<uint16_t>(mir2::common::MsgId::kTradeCancelRsp));
  const auto right_cancel_rsp = FindResponse(
      right.client_id, static_cast<uint16_t>(mir2::common::MsgId::kTradeCancelRsp));
  ASSERT_TRUE(left_cancel_rsp.has_value());
  ASSERT_TRUE(right_cancel_rsp.has_value());

  EXPECT_GE(CountResponses(static_cast<uint16_t>(mir2::common::MsgId::kTradeComplete)), 2u);

  const auto* left_trade = registry_.try_get<ecs::TradeComponent>(left.entity);
  const auto* right_trade = registry_.try_get<ecs::TradeComponent>(right.entity);
  ASSERT_NE(left_trade, nullptr);
  ASSERT_NE(right_trade, nullptr);
  EXPECT_EQ(left_trade->state, ecs::TradeState::kNone);
  EXPECT_EQ(right_trade->state, ecs::TradeState::kNone);
}

}  // namespace
}  // namespace mir2::logic::test
