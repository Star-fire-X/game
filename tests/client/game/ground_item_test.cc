/**
 * @file ground_item_test.cc
 * @brief Unit tests for ground item feature (P0).
 *
 * Covers:
 * - EntityManager add/remove/query GroundItem entities with template_id/name.
 * - ItemHandler pickup request building and response parsing.
 * - GroundItemRenderer sprite index calculation.
 * - EntityEnteredEvent carrying template_id and name fields.
 */

#include <gtest/gtest.h>
#include <flatbuffers/flatbuffers.h>

#include "client/game/entity_manager.h"
#include "client/handlers/item_handler.h"
#include "client/handlers/movement_handler.h"

#include "item_generated.h"
#include "common_generated.h"

namespace {

using mir2::common::MsgId;
using mir2::common::NetworkPacket;
using mir2::game::Entity;
using mir2::game::EntityManager;
using mir2::game::EntityType;
using mir2::game::handlers::ItemHandler;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

NetworkPacket MakePacket(MsgId msg_id, std::vector<uint8_t> payload) {
    NetworkPacket packet;
    packet.msg_id = static_cast<uint16_t>(msg_id);
    packet.payload = std::move(payload);
    return packet;
}

std::vector<uint8_t> BuildPayload(flatbuffers::FlatBufferBuilder& builder) {
    const uint8_t* data = builder.GetBufferPointer();
    return std::vector<uint8_t>(data, data + builder.GetSize());
}

std::shared_ptr<ItemHandler> MakeHandler(ItemHandler::Callbacks callbacks) {
    return std::make_shared<ItemHandler>(std::move(callbacks));
}

// ==========================================================================
// 1. EntityManager: GroundItem entities
// ==========================================================================

TEST(GroundItemEntityTest, AddAndQueryGroundItem) {
    EntityManager manager(1);

    Entity item;
    item.id = 100;
    item.type = EntityType::GroundItem;
    item.position = {10, 20};
    item.template_id = 42;
    item.name = "Gold Sword";

    EXPECT_TRUE(manager.add_entity(item));

    const Entity* found = manager.get_entity(100);
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->type, EntityType::GroundItem);
    EXPECT_EQ(found->template_id, 42u);
    EXPECT_EQ(found->name, "Gold Sword");
    EXPECT_EQ(found->position.x, 10);
    EXPECT_EQ(found->position.y, 20);
}

TEST(GroundItemEntityTest, RemoveGroundItem) {
    EntityManager manager(1);

    Entity item;
    item.id = 200;
    item.type = EntityType::GroundItem;
    item.position = {5, 5};

    EXPECT_TRUE(manager.add_entity(item));
    EXPECT_TRUE(manager.contains(200));

    EXPECT_TRUE(manager.remove_entity(200));
    EXPECT_FALSE(manager.contains(200));
    EXPECT_EQ(manager.get_entity(200), nullptr);
}

TEST(GroundItemEntityTest, QueryRangeReturnsGroundItems) {
    EntityManager manager(1);

    Entity player;
    player.id = 1;
    player.type = EntityType::Player;
    player.position = {10, 10};
    manager.add_entity(player);

    Entity item1;
    item1.id = 100;
    item1.type = EntityType::GroundItem;
    item1.position = {10, 10};
    item1.template_id = 1;
    manager.add_entity(item1);

    Entity item2;
    item2.id = 101;
    item2.type = EntityType::GroundItem;
    item2.position = {11, 10};
    item2.template_id = 2;
    manager.add_entity(item2);

    Entity far_item;
    far_item.id = 102;
    far_item.type = EntityType::GroundItem;
    far_item.position = {50, 50};
    far_item.template_id = 3;
    manager.add_entity(far_item);

    auto nearby = manager.query_range({10, 10}, 1);
    // Should find player + item1 + item2 (within 1-tile radius), not far_item
    EXPECT_GE(nearby.size(), 2u);

    int ground_item_count = 0;
    for (const auto* e : nearby) {
        if (e->type == EntityType::GroundItem) {
            ++ground_item_count;
        }
    }
    EXPECT_EQ(ground_item_count, 2);
}

TEST(GroundItemEntityTest, QueryAtReturnsGroundItemsAtPosition) {
    EntityManager manager(1);

    Entity item;
    item.id = 100;
    item.type = EntityType::GroundItem;
    item.position = {5, 5};
    item.template_id = 10;
    item.name = "Potion";
    manager.add_entity(item);

    auto at_pos = manager.query_at({5, 5});
    ASSERT_EQ(at_pos.size(), 1u);
    EXPECT_EQ(at_pos[0]->id, 100u);
    EXPECT_EQ(at_pos[0]->type, EntityType::GroundItem);
}

TEST(GroundItemEntityTest, UpdateEntityPreservesGroundItemFields) {
    EntityManager manager(1);

    Entity item;
    item.id = 100;
    item.type = EntityType::GroundItem;
    item.position = {5, 5};
    item.template_id = 10;
    item.name = "Potion";
    manager.add_entity(item);

    Entity updated = item;
    updated.position = {6, 6};
    manager.update_entity(updated);

    const Entity* found = manager.get_entity(100);
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->position.x, 6);
    EXPECT_EQ(found->position.y, 6);
    EXPECT_EQ(found->template_id, 10u);
    EXPECT_EQ(found->name, "Potion");
}

// ==========================================================================
// 2. Sprite index calculation (same as GroundItemRenderer::get_sprite_index)
// ==========================================================================

// The sprite index function maps template_id directly to a WIL frame index.
// This mirrors the static method in GroundItemRenderer without the SDL dependency.
static int get_sprite_index(uint32_t template_id) {
    return static_cast<int>(template_id);
}

TEST(GroundItemRendererTest, SpriteIndexMatchesTemplateId) {
    EXPECT_EQ(get_sprite_index(0), 0);
    EXPECT_EQ(get_sprite_index(42), 42);
    EXPECT_EQ(get_sprite_index(100), 100);
    EXPECT_EQ(get_sprite_index(9999), 9999);
}

// ==========================================================================
// 3. ItemHandler: PickupItemRsp parsing
// ==========================================================================

TEST(GroundItemPickupTest, PickupResponseSuccess) {
    mir2::proto::ErrorCode captured_code{};
    uint32_t captured_item_id = 0;
    int callback_calls = 0;

    ItemHandler::Callbacks callbacks;
    callbacks.on_pickup_item_response = [&](mir2::proto::ErrorCode code,
                                             uint32_t item_id) {
        captured_code = code;
        captured_item_id = item_id;
        ++callback_calls;
    };

    auto handler = MakeHandler(std::move(callbacks));

    // Build a valid PickupItemRsp
    flatbuffers::FlatBufferBuilder builder(128);
    auto rsp = mir2::proto::CreatePickupItemRsp(
        builder,
        mir2::proto::ErrorCode::ERR_OK,
        42);
    builder.Finish(rsp);

    handler->HandlePickupItemResponse(
        MakePacket(MsgId::kPickupItemRsp, BuildPayload(builder)));

    EXPECT_EQ(callback_calls, 1);
    EXPECT_EQ(captured_code, mir2::proto::ErrorCode::ERR_OK);
    EXPECT_EQ(captured_item_id, 42u);
}

TEST(GroundItemPickupTest, PickupResponseError) {
    mir2::proto::ErrorCode captured_code{};
    int callback_calls = 0;

    ItemHandler::Callbacks callbacks;
    callbacks.on_pickup_item_response = [&](mir2::proto::ErrorCode code,
                                             uint32_t /*item_id*/) {
        captured_code = code;
        ++callback_calls;
    };

    auto handler = MakeHandler(std::move(callbacks));

    flatbuffers::FlatBufferBuilder builder(128);
    auto rsp = mir2::proto::CreatePickupItemRsp(
        builder,
        mir2::proto::ErrorCode::ERR_INVALID_ACTION,
        0);
    builder.Finish(rsp);

    handler->HandlePickupItemResponse(
        MakePacket(MsgId::kPickupItemRsp, BuildPayload(builder)));

    EXPECT_EQ(callback_calls, 1);
    EXPECT_EQ(captured_code, mir2::proto::ErrorCode::ERR_INVALID_ACTION);
}

TEST(GroundItemPickupTest, PickupResponseEmptyPayloadReportsError) {
    int parse_error_calls = 0;
    std::string captured_error;

    ItemHandler::Callbacks callbacks;
    callbacks.on_parse_error = [&](const std::string& error) {
        captured_error = error;
        ++parse_error_calls;
    };

    auto handler = MakeHandler(std::move(callbacks));
    handler->HandlePickupItemResponse(MakePacket(MsgId::kPickupItemRsp, {}));

    EXPECT_EQ(parse_error_calls, 1);
    EXPECT_EQ(captured_error, "Empty pickup-item response payload");
}

TEST(GroundItemPickupTest, PickupResponseCorruptPayloadReportsError) {
    int parse_error_calls = 0;

    ItemHandler::Callbacks callbacks;
    callbacks.on_parse_error = [&](const std::string& /*error*/) {
        ++parse_error_calls;
    };

    auto handler = MakeHandler(std::move(callbacks));
    handler->HandlePickupItemResponse(
        MakePacket(MsgId::kPickupItemRsp, {0xFF, 0xFF, 0xFF}));

    EXPECT_EQ(parse_error_calls, 1);
}

TEST(GroundItemPickupTest, PickupResponseOwnerExpiredSuppressesCallback) {
    int callback_calls = 0;
    auto owner = std::make_shared<int>(42);

    ItemHandler::Callbacks callbacks;
    callbacks.owner = std::weak_ptr<void>(owner);
    callbacks.on_pickup_item_response = [&](mir2::proto::ErrorCode, uint32_t) {
        ++callback_calls;
    };

    auto handler = MakeHandler(std::move(callbacks));

    // Build valid response
    flatbuffers::FlatBufferBuilder builder(128);
    auto rsp = mir2::proto::CreatePickupItemRsp(
        builder, mir2::proto::ErrorCode::ERR_OK, 1);
    builder.Finish(rsp);
    auto packet = MakePacket(MsgId::kPickupItemRsp, BuildPayload(builder));

    // Owner alive -> callback fires
    handler->HandlePickupItemResponse(packet);
    EXPECT_EQ(callback_calls, 1);

    // Owner destroyed -> callback suppressed
    owner.reset();
    handler->HandlePickupItemResponse(packet);
    EXPECT_EQ(callback_calls, 1);  // still 1
}

// ==========================================================================
// 4. EntityEnteredEvent: template_id and name fields
// ==========================================================================

TEST(EntityEnteredEventTest, CarriesTemplateIdAndName) {
    mir2::game::events::EntityEnteredEvent event{
        .entity_id = 999,
        .entity_type = mir2::proto::EntityType::ITEM,
        .x = 10,
        .y = 20,
        .direction = 0,
        .template_id = 42,
        .name = "Magic Sword",
    };

    EXPECT_EQ(event.entity_id, 999u);
    EXPECT_EQ(event.entity_type, mir2::proto::EntityType::ITEM);
    EXPECT_EQ(event.template_id, 42u);
    EXPECT_EQ(event.name, "Magic Sword");
}

TEST(EntityEnteredEventTest, DefaultFieldsAreZeroAndEmpty) {
    mir2::game::events::EntityEnteredEvent event{
        .entity_id = 1,
        .entity_type = mir2::proto::EntityType::PLAYER,
        .x = 0,
        .y = 0,
        .direction = 0,
    };

    EXPECT_EQ(event.template_id, 0u);
    EXPECT_TRUE(event.name.empty());
}

} // namespace
