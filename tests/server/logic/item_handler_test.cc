/**
 * @file item_handler_test.cc
 * @brief Tests for logic ItemHandler exception fallback behavior.
 */

#include <gtest/gtest.h>

#include <asio/io_context.hpp>
#include <flatbuffers/flatbuffers.h>

#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "common/enums.h"
#include "item_generated.h"
#include "logic/coroutine_executor.h"
#define private public
#include "logic/handlers/item/item_handler.h"
#undef private
#include "logic/mock_response_sender.h"

namespace mir2::logic::test {
namespace {

std::vector<uint8_t> BuildPickupReq(uint32_t item_id) {
  flatbuffers::FlatBufferBuilder builder;
  const auto req = mir2::proto::CreatePickupItemReq(builder, item_id);
  builder.Finish(req);
  const uint8_t* data = builder.GetBufferPointer();
  return std::vector<uint8_t>(data, data + builder.GetSize());
}

std::vector<uint8_t> BuildUseReq(uint16_t slot, uint32_t item_id) {
  flatbuffers::FlatBufferBuilder builder;
  const auto req = mir2::proto::CreateUseItemReq(builder, slot, item_id);
  builder.Finish(req);
  const uint8_t* data = builder.GetBufferPointer();
  return std::vector<uint8_t>(data, data + builder.GetSize());
}

std::vector<uint8_t> BuildDropReq(uint16_t slot, uint32_t item_id, uint32_t count) {
  flatbuffers::FlatBufferBuilder builder;
  const auto req = mir2::proto::CreateDropItemReq(builder, slot, item_id, count);
  builder.Finish(req);
  const uint8_t* data = builder.GetBufferPointer();
  return std::vector<uint8_t>(data, data + builder.GetSize());
}

std::vector<uint8_t> BuildEquipReq(uint16_t slot, uint32_t item_id) {
  flatbuffers::FlatBufferBuilder builder;
  const auto req = mir2::proto::CreateEquipReq(builder, slot, item_id);
  builder.Finish(req);
  const uint8_t* data = builder.GetBufferPointer();
  return std::vector<uint8_t>(data, data + builder.GetSize());
}

std::vector<uint8_t> BuildUnequipReq(uint16_t slot) {
  flatbuffers::FlatBufferBuilder builder;
  const auto req = mir2::proto::CreateUnequipReq(builder, slot);
  builder.Finish(req);
  const uint8_t* data = builder.GetBufferPointer();
  return std::vector<uint8_t>(data, data + builder.GetSize());
}

class ThrowingInventoryService final : public InventoryService {
 public:
  ItemPickupResult PickupItem(uint64_t, uint32_t) override {
    throw std::runtime_error("pickup failure");
  }

  ItemUseResult UseItem(uint64_t, uint16_t, uint32_t) override {
    throw std::runtime_error("use failure");
  }

  ItemDropResult DropItem(uint64_t, uint16_t, uint32_t, uint32_t) override {
    throw std::runtime_error("drop failure");
  }

  ItemEquipResult EquipItem(uint64_t, uint16_t, uint32_t) override {
    throw std::runtime_error("equip failure");
  }

  ItemUnequipResult UnequipItem(uint64_t, uint16_t) override {
    throw std::runtime_error("unequip failure");
  }
};

class ItemHandlerTest : public ::testing::Test {
 protected:
  void SetUp() override {
    executor_ = std::make_unique<CoroutineExecutor>(io_context_, 1);
    response_sender_ = std::make_unique<MockResponseSender>();
    service_ = std::make_unique<ThrowingInventoryService>();
    handler_ = std::make_unique<ItemHandler>(*executor_, *response_sender_, *service_);
  }

  void RunIoContext() {
    io_context_.run();
    io_context_.restart();
  }

  asio::io_context io_context_;
  std::unique_ptr<CoroutineExecutor> executor_;
  std::unique_ptr<MockResponseSender> response_sender_;
  std::unique_ptr<ThrowingInventoryService> service_;
  std::unique_ptr<ItemHandler> handler_;
};

TEST_F(ItemHandlerTest, PickupExceptionReturnsInvalidAction) {
  HandlerContext ctx;
  ctx.client_id = 88001;
  ctx.msg_id = static_cast<uint16_t>(mir2::common::MsgId::kPickupItemReq);
  const auto payload = BuildPickupReq(1001);

  ASSERT_TRUE(executor_->Spawn(handler_->HandleMessage(ctx, payload.data(), payload.size())));
  RunIoContext();

  const auto responses = response_sender_->GetCapturedResponses();
  ASSERT_EQ(responses.size(), 1u);
  EXPECT_EQ(responses[0].msg_id,
            static_cast<uint16_t>(mir2::common::MsgId::kPickupItemRsp));

  flatbuffers::Verifier verifier(responses[0].payload.data(),
                                 responses[0].payload.size());
  ASSERT_TRUE(verifier.VerifyBuffer<mir2::proto::PickupItemRsp>(nullptr));
  const auto* rsp =
      flatbuffers::GetRoot<mir2::proto::PickupItemRsp>(responses[0].payload.data());
  ASSERT_NE(rsp, nullptr);
  EXPECT_EQ(rsp->code(), mir2::proto::ErrorCode::ERR_INVALID_ACTION);
}

TEST_F(ItemHandlerTest, UseExceptionReturnsInvalidAction) {
  HandlerContext ctx;
  ctx.client_id = 88002;
  ctx.msg_id = static_cast<uint16_t>(mir2::common::MsgId::kUseItemReq);
  const auto payload = BuildUseReq(3, 2002);

  ASSERT_TRUE(executor_->Spawn(handler_->HandleMessage(ctx, payload.data(), payload.size())));
  RunIoContext();

  const auto responses = response_sender_->GetCapturedResponses();
  ASSERT_EQ(responses.size(), 1u);
  EXPECT_EQ(responses[0].msg_id,
            static_cast<uint16_t>(mir2::common::MsgId::kUseItemRsp));

  flatbuffers::Verifier verifier(responses[0].payload.data(),
                                 responses[0].payload.size());
  ASSERT_TRUE(verifier.VerifyBuffer<mir2::proto::UseItemRsp>(nullptr));
  const auto* rsp =
      flatbuffers::GetRoot<mir2::proto::UseItemRsp>(responses[0].payload.data());
  ASSERT_NE(rsp, nullptr);
  EXPECT_EQ(rsp->code(), mir2::proto::ErrorCode::ERR_INVALID_ACTION);
}

TEST_F(ItemHandlerTest, DropExceptionReturnsInvalidAction) {
  HandlerContext ctx;
  ctx.client_id = 88003;
  const auto payload = BuildDropReq(4, 3003, 2);

  flatbuffers::Verifier verifier(payload.data(), payload.size());
  ASSERT_TRUE(verifier.VerifyBuffer<mir2::proto::DropItemReq>(nullptr));
  const auto* req = flatbuffers::GetRoot<mir2::proto::DropItemReq>(payload.data());
  ASSERT_NE(req, nullptr);

  ASSERT_TRUE(executor_->Spawn(handler_->HandleDrop(ctx, req)));
  RunIoContext();

  const auto responses = response_sender_->GetCapturedResponses();
  ASSERT_EQ(responses.size(), 1u);
  EXPECT_EQ(responses[0].msg_id,
            static_cast<uint16_t>(mir2::common::MsgId::kDropItemRsp));

  flatbuffers::Verifier rsp_verifier(responses[0].payload.data(),
                                     responses[0].payload.size());
  ASSERT_TRUE(rsp_verifier.VerifyBuffer<mir2::proto::DropItemRsp>(nullptr));
  const auto* rsp =
      flatbuffers::GetRoot<mir2::proto::DropItemRsp>(responses[0].payload.data());
  ASSERT_NE(rsp, nullptr);
  EXPECT_EQ(rsp->code(), mir2::proto::ErrorCode::ERR_INVALID_ACTION);
}

TEST_F(ItemHandlerTest, EquipExceptionReturnsInvalidAction) {
  HandlerContext ctx;
  ctx.client_id = 88004;
  ctx.msg_id = static_cast<uint16_t>(mir2::common::MsgId::kEquipReq);
  const auto payload = BuildEquipReq(5, 4004);

  ASSERT_TRUE(executor_->Spawn(handler_->HandleMessage(ctx, payload.data(), payload.size())));
  RunIoContext();

  const auto responses = response_sender_->GetCapturedResponses();
  ASSERT_EQ(responses.size(), 1u);
  EXPECT_EQ(responses[0].msg_id,
            static_cast<uint16_t>(mir2::common::MsgId::kEquipRsp));

  flatbuffers::Verifier verifier(responses[0].payload.data(),
                                 responses[0].payload.size());
  ASSERT_TRUE(verifier.VerifyBuffer<mir2::proto::EquipRsp>(nullptr));
  const auto* rsp =
      flatbuffers::GetRoot<mir2::proto::EquipRsp>(responses[0].payload.data());
  ASSERT_NE(rsp, nullptr);
  EXPECT_EQ(rsp->code(), mir2::proto::ErrorCode::ERR_INVALID_ACTION);
}

TEST_F(ItemHandlerTest, UnequipExceptionReturnsInvalidAction) {
  HandlerContext ctx;
  ctx.client_id = 88005;
  ctx.msg_id = static_cast<uint16_t>(mir2::common::MsgId::kUnequipReq);
  const auto payload = BuildUnequipReq(2);

  ASSERT_TRUE(executor_->Spawn(handler_->HandleMessage(ctx, payload.data(), payload.size())));
  RunIoContext();

  const auto responses = response_sender_->GetCapturedResponses();
  ASSERT_EQ(responses.size(), 1u);
  EXPECT_EQ(responses[0].msg_id,
            static_cast<uint16_t>(mir2::common::MsgId::kUnequipRsp));

  flatbuffers::Verifier verifier(responses[0].payload.data(),
                                 responses[0].payload.size());
  ASSERT_TRUE(verifier.VerifyBuffer<mir2::proto::UnequipRsp>(nullptr));
  const auto* rsp =
      flatbuffers::GetRoot<mir2::proto::UnequipRsp>(responses[0].payload.data());
  ASSERT_NE(rsp, nullptr);
  EXPECT_EQ(rsp->code(), mir2::proto::ErrorCode::ERR_INVALID_ACTION);
}

}  // namespace
}  // namespace mir2::logic::test
