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

#include "auction_generated.h"
#include "common/enums.h"
#include "ecs/components/character_components.h"
#include "ecs/components/item_component.h"
#include "logic/coroutine_executor.h"
#include "logic/handlers/auction/auction_handler.h"
#include "logic/mock_response_sender.h"
#include "logic/services/client_registry.h"
#include "logic/services/session_role_store.h"

namespace mir2::logic::test {
namespace {

std::vector<uint8_t> BuildAuctionListReqPayload(uint32_t page,
                                                uint32_t page_size,
                                                bool seller_only) {
  flatbuffers::FlatBufferBuilder builder;
  const auto req = mir2::proto::CreateAuctionListReq(builder, page, page_size, seller_only);
  builder.Finish(req);
  return {builder.GetBufferPointer(), builder.GetBufferPointer() + builder.GetSize()};
}

std::vector<uint8_t> BuildAuctionSellReqPayload(uint16_t slot,
                                                uint32_t item_id,
                                                uint32_t count,
                                                uint32_t unit_price,
                                                uint32_t duration_sec) {
  flatbuffers::FlatBufferBuilder builder;
  const auto req =
      mir2::proto::CreateAuctionSellReq(builder, slot, item_id, count, unit_price, duration_sec);
  builder.Finish(req);
  return {builder.GetBufferPointer(), builder.GetBufferPointer() + builder.GetSize()};
}

std::vector<uint8_t> BuildAuctionBuyReqPayload(uint64_t listing_id) {
  flatbuffers::FlatBufferBuilder builder;
  const auto req = mir2::proto::CreateAuctionBuyReq(builder, listing_id);
  builder.Finish(req);
  return {builder.GetBufferPointer(), builder.GetBufferPointer() + builder.GetSize()};
}

std::vector<uint8_t> BuildAuctionCancelReqPayload(uint64_t listing_id) {
  flatbuffers::FlatBufferBuilder builder;
  const auto req = mir2::proto::CreateAuctionCancelReq(builder, listing_id);
  builder.Finish(req);
  return {builder.GetBufferPointer(), builder.GetBufferPointer() + builder.GetSize()};
}

class AuctionHandlerTest : public ::testing::Test {
 protected:
  struct Player {
    uint64_t character_id = 0;
    uint64_t client_id = 0;
    entt::entity entity = entt::null;
  };

  void SetUp() override {
    executor_ = std::make_unique<CoroutineExecutor>(io_context_, 1);
    response_sender_ = std::make_unique<MockResponseSender>();
    handler_ = std::make_unique<AuctionHandler>(
        *response_sender_, client_registry_, registry_, &role_store_);
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
    item_comp.instance_id = static_cast<uint64_t>(entt::to_integral(item));

    auto& owner_comp = registry_.emplace<ecs::InventoryOwnerComponent>(item);
    owner_comp.owner = owner;
    owner_comp.slot_index = slot;
  }

  int CountOwnedItem(entt::entity owner, uint32_t item_id) const {
    int total = 0;
    auto view = registry_.view<ecs::ItemComponent, ecs::InventoryOwnerComponent>();
    for (const entt::entity entity : view) {
      const auto& item = view.get<ecs::ItemComponent>(entity);
      const auto& owner_comp = view.get<ecs::InventoryOwnerComponent>(entity);
      if (owner_comp.owner == owner && item.item_id == item_id && owner_comp.slot_index >= 0) {
        total += item.count;
      }
    }
    return total;
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

  asio::io_context io_context_;
  entt::registry registry_;
  std::unique_ptr<CoroutineExecutor> executor_;
  std::unique_ptr<MockResponseSender> response_sender_;
  ClientRegistry client_registry_;
  RoleStore role_store_;
  std::unique_ptr<AuctionHandler> handler_;
};

TEST_F(AuctionHandlerTest, SellListBuyFlowUpdatesInventoryAndGold) {
  const auto seller = CreatePlayer(1001, 5001, "Seller", 1000);
  const auto buyer = CreatePlayer(1002, 5002, "Buyer", 1000);
  CreateInventoryItem(seller.entity, 1, 7001, 3);

  HandlerContext sell_ctx;
  sell_ctx.client_id = seller.client_id;
  sell_ctx.msg_id = static_cast<uint16_t>(mir2::common::MsgId::kAuctionSellReq);
  sell_ctx.entity = seller.entity;
  Dispatch(sell_ctx, BuildAuctionSellReqPayload(1, 7001, 2, 50, 3600));

  const auto sell_rsp = FindResponse(
      seller.client_id, static_cast<uint16_t>(mir2::common::MsgId::kAuctionSellRsp));
  ASSERT_TRUE(sell_rsp.has_value());
  flatbuffers::Verifier sell_verifier(sell_rsp->payload.data(), sell_rsp->payload.size());
  ASSERT_TRUE(sell_verifier.VerifyBuffer<mir2::proto::AuctionSellRsp>(nullptr));
  const auto* sell_root = flatbuffers::GetRoot<mir2::proto::AuctionSellRsp>(sell_rsp->payload.data());
  ASSERT_NE(sell_root, nullptr);
  ASSERT_TRUE(sell_root->success());
  const uint64_t listing_id = sell_root->listing_id();
  EXPECT_EQ(CountOwnedItem(seller.entity, 7001), 1);

  response_sender_->Clear();

  HandlerContext list_ctx;
  list_ctx.client_id = buyer.client_id;
  list_ctx.msg_id = static_cast<uint16_t>(mir2::common::MsgId::kAuctionListReq);
  list_ctx.entity = buyer.entity;
  Dispatch(list_ctx, BuildAuctionListReqPayload(1, 20, false));

  const auto list_rsp = FindResponse(
      buyer.client_id, static_cast<uint16_t>(mir2::common::MsgId::kAuctionListRsp));
  ASSERT_TRUE(list_rsp.has_value());
  flatbuffers::Verifier list_verifier(list_rsp->payload.data(), list_rsp->payload.size());
  ASSERT_TRUE(list_verifier.VerifyBuffer<mir2::proto::AuctionListRsp>(nullptr));
  const auto* list_root = flatbuffers::GetRoot<mir2::proto::AuctionListRsp>(list_rsp->payload.data());
  ASSERT_NE(list_root, nullptr);
  ASSERT_TRUE(list_root->success());
  ASSERT_NE(list_root->listings(), nullptr);
  ASSERT_EQ(list_root->listings()->size(), 1u);
  EXPECT_EQ(list_root->listings()->Get(0)->listing_id(), listing_id);

  response_sender_->Clear();

  HandlerContext buy_ctx;
  buy_ctx.client_id = buyer.client_id;
  buy_ctx.msg_id = static_cast<uint16_t>(mir2::common::MsgId::kAuctionBuyReq);
  buy_ctx.entity = buyer.entity;
  Dispatch(buy_ctx, BuildAuctionBuyReqPayload(listing_id));

  const auto buy_rsp = FindResponse(
      buyer.client_id, static_cast<uint16_t>(mir2::common::MsgId::kAuctionBuyRsp));
  ASSERT_TRUE(buy_rsp.has_value());
  flatbuffers::Verifier buy_verifier(buy_rsp->payload.data(), buy_rsp->payload.size());
  ASSERT_TRUE(buy_verifier.VerifyBuffer<mir2::proto::AuctionBuyRsp>(nullptr));
  const auto* buy_root = flatbuffers::GetRoot<mir2::proto::AuctionBuyRsp>(buy_rsp->payload.data());
  ASSERT_NE(buy_root, nullptr);
  ASSERT_TRUE(buy_root->success());

  const auto& seller_attr = registry_.get<ecs::CharacterAttributesComponent>(seller.entity);
  const auto& buyer_attr = registry_.get<ecs::CharacterAttributesComponent>(buyer.entity);
  EXPECT_EQ(seller_attr.gold, 1100);
  EXPECT_EQ(buyer_attr.gold, 900);
  EXPECT_EQ(CountOwnedItem(buyer.entity, 7001), 2);
}

TEST_F(AuctionHandlerTest, CancelReturnsListedItemToSeller) {
  const auto seller = CreatePlayer(2001, 6001, "Seller", 500);
  CreateInventoryItem(seller.entity, 2, 9001, 1);

  HandlerContext sell_ctx;
  sell_ctx.client_id = seller.client_id;
  sell_ctx.msg_id = static_cast<uint16_t>(mir2::common::MsgId::kAuctionSellReq);
  sell_ctx.entity = seller.entity;
  Dispatch(sell_ctx, BuildAuctionSellReqPayload(2, 9001, 1, 88, 3600));

  const auto sell_rsp = FindResponse(
      seller.client_id, static_cast<uint16_t>(mir2::common::MsgId::kAuctionSellRsp));
  ASSERT_TRUE(sell_rsp.has_value());
  flatbuffers::Verifier sell_verifier(sell_rsp->payload.data(), sell_rsp->payload.size());
  ASSERT_TRUE(sell_verifier.VerifyBuffer<mir2::proto::AuctionSellRsp>(nullptr));
  const auto* sell_root = flatbuffers::GetRoot<mir2::proto::AuctionSellRsp>(sell_rsp->payload.data());
  ASSERT_NE(sell_root, nullptr);
  ASSERT_TRUE(sell_root->success());

  const uint64_t listing_id = sell_root->listing_id();
  EXPECT_EQ(CountOwnedItem(seller.entity, 9001), 0);

  response_sender_->Clear();

  HandlerContext cancel_ctx;
  cancel_ctx.client_id = seller.client_id;
  cancel_ctx.msg_id = static_cast<uint16_t>(mir2::common::MsgId::kAuctionCancelReq);
  cancel_ctx.entity = seller.entity;
  Dispatch(cancel_ctx, BuildAuctionCancelReqPayload(listing_id));

  const auto cancel_rsp = FindResponse(
      seller.client_id, static_cast<uint16_t>(mir2::common::MsgId::kAuctionCancelRsp));
  ASSERT_TRUE(cancel_rsp.has_value());
  flatbuffers::Verifier cancel_verifier(cancel_rsp->payload.data(), cancel_rsp->payload.size());
  ASSERT_TRUE(cancel_verifier.VerifyBuffer<mir2::proto::AuctionCancelRsp>(nullptr));
  const auto* cancel_root =
      flatbuffers::GetRoot<mir2::proto::AuctionCancelRsp>(cancel_rsp->payload.data());
  ASSERT_NE(cancel_root, nullptr);
  ASSERT_TRUE(cancel_root->success());
  EXPECT_EQ(CountOwnedItem(seller.entity, 9001), 1);
}

}  // namespace
}  // namespace mir2::logic::test
