/**
 * @file item_handler_test.cc
 * @brief Unit tests for the client-side ItemHandler.
 *
 * Covers:
 *  - Empty / corrupt payload triggers on_parse_error for every Handle* method.
 *  - Valid InventoryUpdate with multiple items is decoded correctly.
 *  - Valid UseItemRsp, DropItemRsp, EquipRsp, UnequipRsp trigger their callbacks
 *    with the correct fields.
 *  - The owner weak_ptr lifetime guard suppresses callbacks when the owner has
 *    expired.
 *  - InventoryUpdate with a null items vector delivers an empty std::vector.
 *  - InventoryUpdate with entries that have a null ItemInfo sub-table still
 *    populates the slot field (item_id / count / durability default to 0).
 */

#include "client/handlers/item_handler.h"

#include <gtest/gtest.h>
#include <flatbuffers/flatbuffers.h>

#include "item_generated.h"
#include "common_generated.h"

namespace {

// ---------------------------------------------------------------------------
// Aliases
// ---------------------------------------------------------------------------
using mir2::common::MsgId;
using mir2::common::NetworkPacket;
using mir2::game::handlers::ClientInventoryItem;
using mir2::game::handlers::ItemHandler;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

/// Build a NetworkPacket from a MsgId and raw payload bytes.
NetworkPacket MakePacket(MsgId msg_id, std::vector<uint8_t> payload) {
    NetworkPacket packet;
    packet.msg_id = static_cast<uint16_t>(msg_id);
    packet.payload = std::move(payload);
    return packet;
}

/// Extract the finished FlatBufferBuilder contents into a byte vector.
std::vector<uint8_t> BuildPayload(flatbuffers::FlatBufferBuilder& builder) {
    const uint8_t* data = builder.GetBufferPointer();
    return std::vector<uint8_t>(data, data + builder.GetSize());
}

/// Create a shared_ptr<ItemHandler> (required because ItemHandler inherits
/// enable_shared_from_this).
std::shared_ptr<ItemHandler> MakeHandler(ItemHandler::Callbacks callbacks) {
    return std::make_shared<ItemHandler>(std::move(callbacks));
}

// ===================================================================
// 1. Empty payload triggers on_parse_error
// ===================================================================

TEST(ItemHandler, InventoryUpdateEmptyPayloadReportsParseError) {
    int parse_calls = 0;
    std::string captured_error;

    ItemHandler::Callbacks callbacks;
    callbacks.on_parse_error = [&](const std::string& error) {
        captured_error = error;
        ++parse_calls;
    };

    auto handler = MakeHandler(std::move(callbacks));
    handler->HandleInventoryUpdate(MakePacket(MsgId::kInventoryUpdate, {}));

    EXPECT_EQ(parse_calls, 1);
    EXPECT_EQ(captured_error, "Empty inventory update payload");
}

TEST(ItemHandler, UseItemResponseEmptyPayloadReportsParseError) {
    int parse_calls = 0;
    std::string captured_error;

    ItemHandler::Callbacks callbacks;
    callbacks.on_parse_error = [&](const std::string& error) {
        captured_error = error;
        ++parse_calls;
    };

    auto handler = MakeHandler(std::move(callbacks));
    handler->HandleUseItemResponse(MakePacket(MsgId::kUseItemRsp, {}));

    EXPECT_EQ(parse_calls, 1);
    EXPECT_EQ(captured_error, "Empty use-item response payload");
}

TEST(ItemHandler, DropItemResponseEmptyPayloadReportsParseError) {
    int parse_calls = 0;
    std::string captured_error;

    ItemHandler::Callbacks callbacks;
    callbacks.on_parse_error = [&](const std::string& error) {
        captured_error = error;
        ++parse_calls;
    };

    auto handler = MakeHandler(std::move(callbacks));
    handler->HandleDropItemResponse(MakePacket(MsgId::kDropItemRsp, {}));

    EXPECT_EQ(parse_calls, 1);
    EXPECT_EQ(captured_error, "Empty drop-item response payload");
}

TEST(ItemHandler, EquipResponseEmptyPayloadReportsParseError) {
    int parse_calls = 0;
    std::string captured_error;

    ItemHandler::Callbacks callbacks;
    callbacks.on_parse_error = [&](const std::string& error) {
        captured_error = error;
        ++parse_calls;
    };

    auto handler = MakeHandler(std::move(callbacks));
    handler->HandleEquipResponse(MakePacket(MsgId::kEquipRsp, {}));

    EXPECT_EQ(parse_calls, 1);
    EXPECT_EQ(captured_error, "Empty equip response payload");
}

TEST(ItemHandler, UnequipResponseEmptyPayloadReportsParseError) {
    int parse_calls = 0;
    std::string captured_error;

    ItemHandler::Callbacks callbacks;
    callbacks.on_parse_error = [&](const std::string& error) {
        captured_error = error;
        ++parse_calls;
    };

    auto handler = MakeHandler(std::move(callbacks));
    handler->HandleUnequipResponse(MakePacket(MsgId::kUnequipRsp, {}));

    EXPECT_EQ(parse_calls, 1);
    EXPECT_EQ(captured_error, "Empty unequip response payload");
}

// ===================================================================
// 2. Valid InventoryUpdate with items
// ===================================================================

TEST(ItemHandler, InventoryUpdateWithItemsTriggersCallback) {
    int called = 0;
    std::vector<ClientInventoryItem> captured_items;

    ItemHandler::Callbacks callbacks;
    callbacks.on_inventory_update = [&](const std::vector<ClientInventoryItem>& items) {
        captured_items = items;
        ++called;
    };

    auto handler = MakeHandler(std::move(callbacks));

    // Build a FlatBuffers InventoryUpdate with two items.
    flatbuffers::FlatBufferBuilder builder;

    auto info_a = mir2::proto::CreateItemInfo(builder,
                                              /*item_id=*/1001,
                                              /*count=*/5,
                                              /*durability=*/100);
    auto entry_a = mir2::proto::CreateInventoryItem(builder,
                                                    /*slot=*/0,
                                                    info_a);

    auto info_b = mir2::proto::CreateItemInfo(builder,
                                              /*item_id=*/2002,
                                              /*count=*/1,
                                              /*durability=*/-1);
    auto entry_b = mir2::proto::CreateInventoryItem(builder,
                                                    /*slot=*/3,
                                                    info_b);

    std::vector<flatbuffers::Offset<mir2::proto::InventoryItem>> entries = {
        entry_a, entry_b};
    auto items_vec = builder.CreateVector(entries);
    auto update = mir2::proto::CreateInventoryUpdate(builder, items_vec);
    builder.Finish(update);

    handler->HandleInventoryUpdate(
        MakePacket(MsgId::kInventoryUpdate, BuildPayload(builder)));

    ASSERT_EQ(called, 1);
    ASSERT_EQ(captured_items.size(), 2u);

    EXPECT_EQ(captured_items[0].slot, 0);
    EXPECT_EQ(captured_items[0].item_id, 1001u);
    EXPECT_EQ(captured_items[0].count, 5u);
    EXPECT_EQ(captured_items[0].durability, 100);

    EXPECT_EQ(captured_items[1].slot, 3);
    EXPECT_EQ(captured_items[1].item_id, 2002u);
    EXPECT_EQ(captured_items[1].count, 1u);
    EXPECT_EQ(captured_items[1].durability, -1);
}

TEST(ItemHandler, InventoryUpdateWithNullItemsVectorDeliversEmptyVector) {
    int called = 0;
    std::vector<ClientInventoryItem> captured_items;

    ItemHandler::Callbacks callbacks;
    callbacks.on_inventory_update = [&](const std::vector<ClientInventoryItem>& items) {
        captured_items = items;
        ++called;
    };

    auto handler = MakeHandler(std::move(callbacks));

    // Build an InventoryUpdate with no items vector (items offset = 0).
    flatbuffers::FlatBufferBuilder builder;
    auto update = mir2::proto::CreateInventoryUpdate(builder, /*items=*/0);
    builder.Finish(update);

    handler->HandleInventoryUpdate(
        MakePacket(MsgId::kInventoryUpdate, BuildPayload(builder)));

    ASSERT_EQ(called, 1);
    EXPECT_TRUE(captured_items.empty());
}

TEST(ItemHandler, InventoryUpdateWithNullItemInfoDefaultsToZero) {
    int called = 0;
    std::vector<ClientInventoryItem> captured_items;

    ItemHandler::Callbacks callbacks;
    callbacks.on_inventory_update = [&](const std::vector<ClientInventoryItem>& items) {
        captured_items = items;
        ++called;
    };

    auto handler = MakeHandler(std::move(callbacks));

    // Build an InventoryItem with slot=7 but no ItemInfo sub-table.
    flatbuffers::FlatBufferBuilder builder;
    auto entry = mir2::proto::CreateInventoryItem(builder,
                                                  /*slot=*/7,
                                                  /*item=*/0);
    std::vector<flatbuffers::Offset<mir2::proto::InventoryItem>> entries = {entry};
    auto items_vec = builder.CreateVector(entries);
    auto update = mir2::proto::CreateInventoryUpdate(builder, items_vec);
    builder.Finish(update);

    handler->HandleInventoryUpdate(
        MakePacket(MsgId::kInventoryUpdate, BuildPayload(builder)));

    ASSERT_EQ(called, 1);
    ASSERT_EQ(captured_items.size(), 1u);
    EXPECT_EQ(captured_items[0].slot, 7);
    EXPECT_EQ(captured_items[0].item_id, 0u);
    EXPECT_EQ(captured_items[0].count, 0u);
    EXPECT_EQ(captured_items[0].durability, 0);
}

// ===================================================================
// 3. Valid UseItemRsp with error code and fields
// ===================================================================

TEST(ItemHandler, UseItemResponseTriggersCallback) {
    int called = 0;
    mir2::proto::ErrorCode captured_code = mir2::proto::ErrorCode::ERR_UNKNOWN;
    uint16_t captured_slot = 0;
    uint32_t captured_item_id = 0;
    uint32_t captured_remaining = 0;

    ItemHandler::Callbacks callbacks;
    callbacks.on_use_item_response = [&](mir2::proto::ErrorCode code,
                                         uint16_t slot,
                                         uint32_t item_id,
                                         uint32_t remaining) {
        captured_code = code;
        captured_slot = slot;
        captured_item_id = item_id;
        captured_remaining = remaining;
        ++called;
    };

    auto handler = MakeHandler(std::move(callbacks));

    flatbuffers::FlatBufferBuilder builder;
    auto rsp = mir2::proto::CreateUseItemRsp(builder,
                                             mir2::proto::ErrorCode::ERR_OK,
                                             /*slot=*/2,
                                             /*item_id=*/555,
                                             /*remaining=*/9);
    builder.Finish(rsp);

    handler->HandleUseItemResponse(
        MakePacket(MsgId::kUseItemRsp, BuildPayload(builder)));

    EXPECT_EQ(called, 1);
    EXPECT_EQ(captured_code, mir2::proto::ErrorCode::ERR_OK);
    EXPECT_EQ(captured_slot, 2);
    EXPECT_EQ(captured_item_id, 555u);
    EXPECT_EQ(captured_remaining, 9u);
}

TEST(ItemHandler, UseItemResponseWithErrorCode) {
    int called = 0;
    mir2::proto::ErrorCode captured_code = mir2::proto::ErrorCode::ERR_OK;

    ItemHandler::Callbacks callbacks;
    callbacks.on_use_item_response = [&](mir2::proto::ErrorCode code,
                                         uint16_t /*slot*/,
                                         uint32_t /*item_id*/,
                                         uint32_t /*remaining*/) {
        captured_code = code;
        ++called;
    };

    auto handler = MakeHandler(std::move(callbacks));

    flatbuffers::FlatBufferBuilder builder;
    auto rsp = mir2::proto::CreateUseItemRsp(builder,
                                             mir2::proto::ErrorCode::ERR_INVALID_ACTION,
                                             /*slot=*/0,
                                             /*item_id=*/0,
                                             /*remaining=*/0);
    builder.Finish(rsp);

    handler->HandleUseItemResponse(
        MakePacket(MsgId::kUseItemRsp, BuildPayload(builder)));

    EXPECT_EQ(called, 1);
    EXPECT_EQ(captured_code, mir2::proto::ErrorCode::ERR_INVALID_ACTION);
}

// ===================================================================
// 4. Valid DropItemRsp
// ===================================================================

TEST(ItemHandler, DropItemResponseTriggersCallback) {
    int called = 0;
    mir2::proto::ErrorCode captured_code = mir2::proto::ErrorCode::ERR_UNKNOWN;
    uint32_t captured_item_id = 0;
    uint32_t captured_count = 0;

    ItemHandler::Callbacks callbacks;
    callbacks.on_drop_item_response = [&](mir2::proto::ErrorCode code,
                                          uint32_t item_id,
                                          uint32_t count) {
        captured_code = code;
        captured_item_id = item_id;
        captured_count = count;
        ++called;
    };

    auto handler = MakeHandler(std::move(callbacks));

    flatbuffers::FlatBufferBuilder builder;
    auto rsp = mir2::proto::CreateDropItemRsp(builder,
                                              mir2::proto::ErrorCode::ERR_OK,
                                              /*item_id=*/777,
                                              /*count=*/3);
    builder.Finish(rsp);

    handler->HandleDropItemResponse(
        MakePacket(MsgId::kDropItemRsp, BuildPayload(builder)));

    EXPECT_EQ(called, 1);
    EXPECT_EQ(captured_code, mir2::proto::ErrorCode::ERR_OK);
    EXPECT_EQ(captured_item_id, 777u);
    EXPECT_EQ(captured_count, 3u);
}

// ===================================================================
// 5. Valid EquipRsp
// ===================================================================

TEST(ItemHandler, EquipResponseTriggersCallback) {
    int called = 0;
    mir2::proto::ErrorCode captured_code = mir2::proto::ErrorCode::ERR_UNKNOWN;
    uint16_t captured_slot = 0;
    uint32_t captured_item_id = 0;

    ItemHandler::Callbacks callbacks;
    callbacks.on_equip_response = [&](mir2::proto::ErrorCode code,
                                      uint16_t slot,
                                      uint32_t item_id) {
        captured_code = code;
        captured_slot = slot;
        captured_item_id = item_id;
        ++called;
    };

    auto handler = MakeHandler(std::move(callbacks));

    flatbuffers::FlatBufferBuilder builder;
    auto rsp = mir2::proto::CreateEquipRsp(builder,
                                           mir2::proto::ErrorCode::ERR_OK,
                                           /*slot=*/4,
                                           /*item_id=*/888);
    builder.Finish(rsp);

    handler->HandleEquipResponse(
        MakePacket(MsgId::kEquipRsp, BuildPayload(builder)));

    EXPECT_EQ(called, 1);
    EXPECT_EQ(captured_code, mir2::proto::ErrorCode::ERR_OK);
    EXPECT_EQ(captured_slot, 4);
    EXPECT_EQ(captured_item_id, 888u);
}

// ===================================================================
// 6. Valid UnequipRsp
// ===================================================================

TEST(ItemHandler, UnequipResponseTriggersCallback) {
    int called = 0;
    mir2::proto::ErrorCode captured_code = mir2::proto::ErrorCode::ERR_UNKNOWN;
    uint16_t captured_slot = 0;
    uint32_t captured_item_id = 0;

    ItemHandler::Callbacks callbacks;
    callbacks.on_unequip_response = [&](mir2::proto::ErrorCode code,
                                        uint16_t slot,
                                        uint32_t item_id) {
        captured_code = code;
        captured_slot = slot;
        captured_item_id = item_id;
        ++called;
    };

    auto handler = MakeHandler(std::move(callbacks));

    flatbuffers::FlatBufferBuilder builder;
    auto rsp = mir2::proto::CreateUnequipRsp(builder,
                                             mir2::proto::ErrorCode::ERR_OK,
                                             /*slot=*/1,
                                             /*item_id=*/999);
    builder.Finish(rsp);

    handler->HandleUnequipResponse(
        MakePacket(MsgId::kUnequipRsp, BuildPayload(builder)));

    EXPECT_EQ(called, 1);
    EXPECT_EQ(captured_code, mir2::proto::ErrorCode::ERR_OK);
    EXPECT_EQ(captured_slot, 1);
    EXPECT_EQ(captured_item_id, 999u);
}

// ===================================================================
// 7. Corrupt (non-empty but invalid) payload triggers on_parse_error
// ===================================================================

TEST(ItemHandler, InventoryUpdateCorruptPayloadReportsParseError) {
    int parse_calls = 0;
    std::string captured_error;

    ItemHandler::Callbacks callbacks;
    callbacks.on_parse_error = [&](const std::string& error) {
        captured_error = error;
        ++parse_calls;
    };

    auto handler = MakeHandler(std::move(callbacks));

    // Two garbage bytes -- non-empty but not a valid FlatBuffer.
    std::vector<uint8_t> garbage = {0xDE, 0xAD};
    handler->HandleInventoryUpdate(
        MakePacket(MsgId::kInventoryUpdate, garbage));

    EXPECT_EQ(parse_calls, 1);
    EXPECT_EQ(captured_error, "Inventory update verification failed");
}

TEST(ItemHandler, UseItemResponseCorruptPayloadReportsParseError) {
    int parse_calls = 0;
    std::string captured_error;

    ItemHandler::Callbacks callbacks;
    callbacks.on_parse_error = [&](const std::string& error) {
        captured_error = error;
        ++parse_calls;
    };

    auto handler = MakeHandler(std::move(callbacks));

    std::vector<uint8_t> garbage = {0x01, 0x02, 0x03};
    handler->HandleUseItemResponse(
        MakePacket(MsgId::kUseItemRsp, garbage));

    EXPECT_EQ(parse_calls, 1);
    EXPECT_EQ(captured_error, "Use-item response verification failed");
}

// ===================================================================
// 8. Owner weak_ptr expiry guard
// ===================================================================

TEST(ItemHandler, OwnerExpiredSuppressesInventoryUpdateCallback) {
    int called = 0;

    // Create an owner and let the handler capture a weak_ptr to it.
    auto owner = std::make_shared<int>(42);

    ItemHandler::Callbacks callbacks;
    callbacks.owner = std::weak_ptr<void>(owner);
    callbacks.on_inventory_update = [&](const std::vector<ClientInventoryItem>&) {
        ++called;
    };

    auto handler = MakeHandler(std::move(callbacks));

    // Build a valid InventoryUpdate payload.
    flatbuffers::FlatBufferBuilder builder;
    auto update = mir2::proto::CreateInventoryUpdate(builder, /*items=*/0);
    builder.Finish(update);
    auto payload = BuildPayload(builder);

    // Sanity: callback fires while owner is alive.
    handler->HandleInventoryUpdate(
        MakePacket(MsgId::kInventoryUpdate, payload));
    ASSERT_EQ(called, 1);

    // Destroy the owner -- the weak_ptr should now be expired.
    owner.reset();

    // The callback must NOT fire.
    handler->HandleInventoryUpdate(
        MakePacket(MsgId::kInventoryUpdate, payload));
    EXPECT_EQ(called, 1);  // Still 1 -- not incremented.
}

TEST(ItemHandler, OwnerExpiredSuppressesUseItemResponseCallback) {
    int called = 0;

    auto owner = std::make_shared<int>(0);

    ItemHandler::Callbacks callbacks;
    callbacks.owner = std::weak_ptr<void>(owner);
    callbacks.on_use_item_response = [&](mir2::proto::ErrorCode,
                                         uint16_t, uint32_t, uint32_t) {
        ++called;
    };

    auto handler = MakeHandler(std::move(callbacks));

    flatbuffers::FlatBufferBuilder builder;
    auto rsp = mir2::proto::CreateUseItemRsp(builder,
                                             mir2::proto::ErrorCode::ERR_OK,
                                             0, 0, 0);
    builder.Finish(rsp);
    auto payload = BuildPayload(builder);

    // Alive -- should fire.
    handler->HandleUseItemResponse(MakePacket(MsgId::kUseItemRsp, payload));
    ASSERT_EQ(called, 1);

    // Expire owner.
    owner.reset();

    // Should not fire.
    handler->HandleUseItemResponse(MakePacket(MsgId::kUseItemRsp, payload));
    EXPECT_EQ(called, 1);
}

TEST(ItemHandler, OwnerExpiredSuppressesDropItemResponseCallback) {
    int called = 0;

    auto owner = std::make_shared<int>(0);

    ItemHandler::Callbacks callbacks;
    callbacks.owner = std::weak_ptr<void>(owner);
    callbacks.on_drop_item_response = [&](mir2::proto::ErrorCode,
                                          uint32_t, uint32_t) {
        ++called;
    };

    auto handler = MakeHandler(std::move(callbacks));

    flatbuffers::FlatBufferBuilder builder;
    auto rsp = mir2::proto::CreateDropItemRsp(builder,
                                              mir2::proto::ErrorCode::ERR_OK,
                                              0, 0);
    builder.Finish(rsp);
    auto payload = BuildPayload(builder);

    handler->HandleDropItemResponse(MakePacket(MsgId::kDropItemRsp, payload));
    ASSERT_EQ(called, 1);

    owner.reset();

    handler->HandleDropItemResponse(MakePacket(MsgId::kDropItemRsp, payload));
    EXPECT_EQ(called, 1);
}

TEST(ItemHandler, OwnerExpiredSuppressesEquipResponseCallback) {
    int called = 0;

    auto owner = std::make_shared<int>(0);

    ItemHandler::Callbacks callbacks;
    callbacks.owner = std::weak_ptr<void>(owner);
    callbacks.on_equip_response = [&](mir2::proto::ErrorCode,
                                      uint16_t, uint32_t) {
        ++called;
    };

    auto handler = MakeHandler(std::move(callbacks));

    flatbuffers::FlatBufferBuilder builder;
    auto rsp = mir2::proto::CreateEquipRsp(builder,
                                           mir2::proto::ErrorCode::ERR_OK,
                                           0, 0);
    builder.Finish(rsp);
    auto payload = BuildPayload(builder);

    handler->HandleEquipResponse(MakePacket(MsgId::kEquipRsp, payload));
    ASSERT_EQ(called, 1);

    owner.reset();

    handler->HandleEquipResponse(MakePacket(MsgId::kEquipRsp, payload));
    EXPECT_EQ(called, 1);
}

TEST(ItemHandler, OwnerExpiredSuppressesUnequipResponseCallback) {
    int called = 0;

    auto owner = std::make_shared<int>(0);

    ItemHandler::Callbacks callbacks;
    callbacks.owner = std::weak_ptr<void>(owner);
    callbacks.on_unequip_response = [&](mir2::proto::ErrorCode,
                                        uint16_t, uint32_t) {
        ++called;
    };

    auto handler = MakeHandler(std::move(callbacks));

    flatbuffers::FlatBufferBuilder builder;
    auto rsp = mir2::proto::CreateUnequipRsp(builder,
                                             mir2::proto::ErrorCode::ERR_OK,
                                             0, 0);
    builder.Finish(rsp);
    auto payload = BuildPayload(builder);

    handler->HandleUnequipResponse(MakePacket(MsgId::kUnequipRsp, payload));
    ASSERT_EQ(called, 1);

    owner.reset();

    handler->HandleUnequipResponse(MakePacket(MsgId::kUnequipRsp, payload));
    EXPECT_EQ(called, 1);
}

// ===================================================================
// 9. No-op when callback is not set (does not crash)
// ===================================================================

TEST(ItemHandler, MissingCallbackDoesNotCrash) {
    // Leave all callbacks as nullptr / default.
    ItemHandler::Callbacks callbacks;

    auto handler = MakeHandler(std::move(callbacks));

    // Build a valid InventoryUpdate payload.
    flatbuffers::FlatBufferBuilder builder;
    auto update = mir2::proto::CreateInventoryUpdate(builder, /*items=*/0);
    builder.Finish(update);

    // Must not crash even though on_inventory_update is not set.
    EXPECT_NO_FATAL_FAILURE(handler->HandleInventoryUpdate(
        MakePacket(MsgId::kInventoryUpdate, BuildPayload(builder))));

    // Also try an empty payload -- on_parse_error is also not set.
    EXPECT_NO_FATAL_FAILURE(handler->HandleInventoryUpdate(
        MakePacket(MsgId::kInventoryUpdate, {})));
}

// ===================================================================
// 10. Owner expiry also suppresses on_parse_error
// ===================================================================

TEST(ItemHandler, OwnerExpiredSuppressesParseErrorCallback) {
    int parse_calls = 0;

    auto owner = std::make_shared<int>(0);

    ItemHandler::Callbacks callbacks;
    callbacks.owner = std::weak_ptr<void>(owner);
    callbacks.on_parse_error = [&](const std::string&) {
        ++parse_calls;
    };

    auto handler = MakeHandler(std::move(callbacks));

    // Expire owner before sending an empty payload.
    owner.reset();

    handler->HandleInventoryUpdate(
        MakePacket(MsgId::kInventoryUpdate, {}));

    // The parse-error callback must not fire because the owner is dead.
    EXPECT_EQ(parse_calls, 0);
}

} // namespace
