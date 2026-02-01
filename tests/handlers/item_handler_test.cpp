#include <gtest/gtest.h>

#include <flatbuffers/flatbuffers.h>

#include "server/common/error_codes.h"
#include "common/enums.h"
#include "handlers/item/item_handler.h"
#include "item_generated.h"

namespace {

class StubInventoryService : public legend2::handlers::InventoryService {
public:
    legend2::handlers::ItemPickupResult pickup_result;
    legend2::handlers::ItemUseResult use_result;
    legend2::handlers::ItemDropResult drop_result;

    legend2::handlers::ItemPickupResult PickupItem(uint64_t /*character_id*/,
                                                   uint32_t /*item_id*/) override {
        return pickup_result;
    }

    legend2::handlers::ItemUseResult UseItem(uint64_t /*character_id*/,
                                             uint16_t /*slot*/,
                                             uint32_t /*item_id*/) override {
        return use_result;
    }

    legend2::handlers::ItemDropResult DropItem(uint64_t /*character_id*/,
                                               uint16_t /*slot*/,
                                               uint32_t /*item_id*/,
                                               uint32_t /*count*/) override {
        return drop_result;
    }
};

std::vector<uint8_t> BuildUseItemReq(uint16_t slot, uint32_t item_id) {
    flatbuffers::FlatBufferBuilder builder;
    const auto req = mir2::proto::CreateUseItemReq(builder, slot, item_id);
    builder.Finish(req);
    const uint8_t* data = builder.GetBufferPointer();
    return std::vector<uint8_t>(data, data + builder.GetSize());
}

std::vector<uint8_t> BuildDropItemReq(uint16_t slot, uint32_t item_id, uint32_t count) {
    flatbuffers::FlatBufferBuilder builder;
    const auto req = mir2::proto::CreateDropItemReq(builder, slot, item_id, count);
    builder.Finish(req);
    const uint8_t* data = builder.GetBufferPointer();
    return std::vector<uint8_t>(data, data + builder.GetSize());
}

std::vector<uint8_t> BuildPickupItemReq(uint32_t item_id) {
    flatbuffers::FlatBufferBuilder builder;
    const auto req = mir2::proto::CreatePickupItemReq(builder, item_id);
    builder.Finish(req);
    const uint8_t* data = builder.GetBufferPointer();
    return std::vector<uint8_t>(data, data + builder.GetSize());
}

}  // namespace

TEST(ItemHandlerTest, UseItemReturnsResponse) {
    StubInventoryService service;
    service.use_result.code = mir2::common::ErrorCode::kOk;
    service.use_result.slot = 2;
    service.use_result.item_id = 100;
    service.use_result.remaining = 1;

    legend2::handlers::ItemHandler handler(service);
    legend2::handlers::HandlerContext context;
    context.client_id = 5;

    const auto payload = BuildUseItemReq(2, 100);
    legend2::handlers::ResponseList responses;
    handler.Handle(context,
                   static_cast<uint16_t>(mir2::common::MsgId::kUseItemReq),
                   payload,
                   [&responses](const legend2::handlers::ResponseList& rsp) { responses = rsp; });

    ASSERT_EQ(responses.size(), 1u);
    flatbuffers::Verifier verifier(responses[0].payload.data(), responses[0].payload.size());
    ASSERT_TRUE(verifier.VerifyBuffer<mir2::proto::UseItemRsp>(nullptr));
    const auto* rsp = flatbuffers::GetRoot<mir2::proto::UseItemRsp>(responses[0].payload.data());
    ASSERT_NE(rsp, nullptr);
    EXPECT_EQ(static_cast<uint16_t>(rsp->code()),
              static_cast<uint16_t>(mir2::common::ErrorCode::kOk));
    EXPECT_EQ(rsp->remaining(), 1u);
}

TEST(ItemHandlerTest, DropItemInvalidPayloadReturnsError) {
    StubInventoryService service;
    legend2::handlers::ItemHandler handler(service);
    legend2::handlers::HandlerContext context;
    context.client_id = 5;

    legend2::handlers::ResponseList responses;
    handler.Handle(context,
                   static_cast<uint16_t>(mir2::common::MsgId::kDropItemReq),
                   {},
                   [&responses](const legend2::handlers::ResponseList& rsp) { responses = rsp; });

    ASSERT_EQ(responses.size(), 1u);
    flatbuffers::Verifier verifier(responses[0].payload.data(), responses[0].payload.size());
    ASSERT_TRUE(verifier.VerifyBuffer<mir2::proto::DropItemRsp>(nullptr));
    const auto* rsp = flatbuffers::GetRoot<mir2::proto::DropItemRsp>(responses[0].payload.data());
    ASSERT_NE(rsp, nullptr);
    EXPECT_EQ(static_cast<uint16_t>(rsp->code()),
              static_cast<uint16_t>(mir2::common::ErrorCode::kInvalidAction));
}

TEST(ItemHandlerTest, PickupItemZeroIdReturnsError) {
    StubInventoryService service;
    legend2::handlers::ItemHandler handler(service);
    legend2::handlers::HandlerContext context;
    context.client_id = 5;

    const auto payload = BuildPickupItemReq(0);
    legend2::handlers::ResponseList responses;
    handler.Handle(context,
                   static_cast<uint16_t>(mir2::common::MsgId::kPickupItemReq),
                   payload,
                   [&responses](const legend2::handlers::ResponseList& rsp) { responses = rsp; });

    ASSERT_EQ(responses.size(), 1u);
    flatbuffers::Verifier verifier(responses[0].payload.data(), responses[0].payload.size());
    ASSERT_TRUE(verifier.VerifyBuffer<mir2::proto::PickupItemRsp>(nullptr));
    const auto* rsp = flatbuffers::GetRoot<mir2::proto::PickupItemRsp>(responses[0].payload.data());
    ASSERT_NE(rsp, nullptr);
    EXPECT_EQ(static_cast<uint16_t>(rsp->code()),
              static_cast<uint16_t>(mir2::common::ErrorCode::kInvalidAction));
}
