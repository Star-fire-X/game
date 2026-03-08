#include <gtest/gtest.h>

#include <flatbuffers/flatbuffers.h>

#include <string>
#include <vector>

#include "client/handlers/movement_handler.h"
#include "common/enums.h"
#include "common/protocol/message_codec.h"
#include "combat_generated.h"
#include "game_generated.h"

namespace {

using mir2::common::MsgId;
using mir2::common::NetworkPacket;
using mir2::game::handlers::MovementHandler;

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

} // namespace

TEST(MovementHandler, MoveResponseTriggersCallback) {
    int called = 0;
    int captured_x = 0;
    int captured_y = 0;

    MovementHandler::Callbacks callbacks;
    callbacks.on_move_response = [&](int x, int y) {
        captured_x = x;
        captured_y = y;
        ++called;
    };

    MovementHandler handler(callbacks);

    mir2::common::MoveResponse response;
    response.code = mir2::proto::ErrorCode::ERR_OK;
    response.x = 12;
    response.y = 34;
    mir2::common::MessageCodecStatus status = mir2::common::MessageCodecStatus::kOk;
    auto payload = mir2::common::EncodeMoveResponse(response, &status);
    ASSERT_EQ(status, mir2::common::MessageCodecStatus::kOk);

    handler.HandleMoveResponse(MakePacket(MsgId::kMoveRsp, std::move(payload)));

    EXPECT_EQ(called, 1);
    EXPECT_EQ(captured_x, 12);
    EXPECT_EQ(captured_y, 34);
}

TEST(MovementHandler, MoveResponseErrorReportsFailure) {
    int failure_calls = 0;
    std::string captured_error;

    MovementHandler::Callbacks callbacks;
    callbacks.on_move_failed = [&](const std::string& error) {
        captured_error = error;
        ++failure_calls;
    };

    MovementHandler handler(callbacks);

    mir2::common::MoveResponse response;
    response.code = mir2::proto::ErrorCode::ERR_INVALID_ACTION;
    response.x = 0;
    response.y = 0;
    mir2::common::MessageCodecStatus status = mir2::common::MessageCodecStatus::kOk;
    auto payload = mir2::common::EncodeMoveResponse(response, &status);
    ASSERT_EQ(status, mir2::common::MessageCodecStatus::kOk);

    handler.HandleMoveResponse(MakePacket(MsgId::kMoveRsp, std::move(payload)));

    EXPECT_EQ(failure_calls, 1);
    EXPECT_EQ(captured_error, "Invalid action");
}

TEST(MovementHandler, MoveResponseInvalidPayloadReportsParseError) {
    int parse_calls = 0;
    std::string captured_error;

    MovementHandler::Callbacks callbacks;
    callbacks.on_parse_error = [&](const std::string& error) {
        captured_error = error;
        ++parse_calls;
    };

    MovementHandler handler(callbacks);

    std::vector<uint8_t> payload = {0x1, 0x2, 0x3};
    handler.HandleMoveResponse(MakePacket(MsgId::kMoveRsp, payload));

    EXPECT_EQ(parse_calls, 1);
    EXPECT_EQ(captured_error, "Invalid move response payload");
}

TEST(MovementHandler, MoveResponseWrongMsgIdReportsParseError) {
    int parse_calls = 0;
    std::string captured_error;

    MovementHandler::Callbacks callbacks;
    callbacks.on_parse_error = [&](const std::string& error) {
        captured_error = error;
        ++parse_calls;
    };

    MovementHandler handler(callbacks);

    mir2::common::MoveResponse response;
    response.code = mir2::proto::ErrorCode::ERR_OK;
    response.x = 1;
    response.y = 2;
    mir2::common::MessageCodecStatus status = mir2::common::MessageCodecStatus::kOk;
    auto payload = mir2::common::EncodeMoveResponse(response, &status);
    ASSERT_EQ(status, mir2::common::MessageCodecStatus::kOk);

    handler.HandleMoveResponse(MakePacket(MsgId::kAttackRsp, std::move(payload)));

    EXPECT_EQ(parse_calls, 1);
    EXPECT_EQ(captured_error, "Invalid move response payload");
}

TEST(MovementHandler, EntityMoveTriggersCallback) {
    int called = 0;
    uint64_t captured_id = 0;
    int captured_x = 0;
    int captured_y = 0;
    mir2::proto::EntityType captured_type = mir2::proto::EntityType::NONE;
    uint8_t captured_dir = 0;

    MovementHandler::Callbacks callbacks;
    callbacks.on_entity_move = [&](const mir2::game::events::EntityMovedEvent& event) {
        captured_id = event.entity_id;
        captured_type = event.entity_type;
        captured_x = event.x;
        captured_y = event.y;
        captured_dir = event.direction;
        ++called;
    };

    MovementHandler handler(callbacks);

    flatbuffers::FlatBufferBuilder builder;
    const auto move = mir2::proto::CreateEntityMove(
        builder, 55u, mir2::proto::EntityType::PLAYER, 7, 8, 3);
    builder.Finish(move);

    handler.HandleMoveBroadcast(MakePacket(MsgId::kEntityMove, BuildPayload(builder)));

    EXPECT_EQ(called, 1);
    EXPECT_EQ(captured_id, 55u);
    EXPECT_EQ(captured_type, mir2::proto::EntityType::PLAYER);
    EXPECT_EQ(captured_x, 7);
    EXPECT_EQ(captured_y, 8);
    EXPECT_EQ(captured_dir, 3);
}

TEST(MovementHandler, EntityEnterTriggersCallback) {
    int called = 0;
    uint64_t captured_id = 0;
    int captured_x = 0;
    int captured_y = 0;
    mir2::proto::EntityType captured_type = mir2::proto::EntityType::NONE;
    uint8_t captured_dir = 0;

    MovementHandler::Callbacks callbacks;
    callbacks.on_entity_enter = [&](const mir2::game::events::EntityEnteredEvent& event) {
        captured_id = event.entity_id;
        captured_type = event.entity_type;
        captured_x = event.x;
        captured_y = event.y;
        captured_dir = event.direction;
        ++called;
    };

    MovementHandler handler(callbacks);

    flatbuffers::FlatBufferBuilder builder;
    const auto enter = mir2::proto::CreateEntityEnter(
        builder, 88u, mir2::proto::EntityType::PLAYER, 9, 10, 6);
    builder.Finish(enter);

    handler.HandleEntitySpawn(MakePacket(MsgId::kEntityEnter, BuildPayload(builder)));

    EXPECT_EQ(called, 1);
    EXPECT_EQ(captured_id, 88u);
    EXPECT_EQ(captured_type, mir2::proto::EntityType::PLAYER);
    EXPECT_EQ(captured_x, 9);
    EXPECT_EQ(captured_y, 10);
    EXPECT_EQ(captured_dir, 6);
}

TEST(MovementHandler, EntityLeaveTriggersCallback) {
    int called = 0;
    uint64_t captured_id = 0;

    MovementHandler::Callbacks callbacks;
    callbacks.on_entity_leave = [&](const mir2::game::events::EntityLeftEvent& event) {
        captured_id = event.entity_id;
        ++called;
    };

    MovementHandler handler(callbacks);

    flatbuffers::FlatBufferBuilder builder;
    const auto leave = mir2::proto::CreateEntityLeave(
        builder, 66u, mir2::proto::EntityType::PLAYER);
    builder.Finish(leave);

    handler.HandleEntityDespawn(MakePacket(MsgId::kEntityLeave, BuildPayload(builder)));

    EXPECT_EQ(called, 1);
    EXPECT_EQ(captured_id, 66u);
}

TEST(MovementHandler, EntityMoveInvalidPayloadReportsParseError) {
    int parse_calls = 0;
    std::string captured_error;

    MovementHandler::Callbacks callbacks;
    callbacks.on_parse_error = [&](const std::string& error) {
        captured_error = error;
        ++parse_calls;
    };

    MovementHandler handler(callbacks);

    std::vector<uint8_t> payload = {0x9, 0x9};
    handler.HandleMoveBroadcast(MakePacket(MsgId::kEntityMove, payload));

    EXPECT_EQ(parse_calls, 1);
    EXPECT_EQ(captured_error, "Invalid entity move payload");
}

TEST(MovementHandler, ChangeMapTriggersCallback) {
    int called = 0;
    uint32_t captured_map_id = 0;
    int captured_x = 0;
    int captured_y = 0;

    MovementHandler::Callbacks callbacks;
    callbacks.on_change_map = [&](uint32_t map_id, int x, int y) {
        captured_map_id = map_id;
        captured_x = x;
        captured_y = y;
        ++called;
    };

    MovementHandler handler(callbacks);

    flatbuffers::FlatBufferBuilder builder;
    const auto change_map = mir2::proto::CreateChangeMap(builder, /*map_id=*/7u, /*x=*/111, /*y=*/222);
    builder.Finish(change_map);

    handler.HandleChangeMap(MakePacket(MsgId::kChangeMap, BuildPayload(builder)));

    EXPECT_EQ(called, 1);
    EXPECT_EQ(captured_map_id, 7u);
    EXPECT_EQ(captured_x, 111);
    EXPECT_EQ(captured_y, 222);
}

TEST(MovementHandler, TeleportTriggersCallback) {
    int called = 0;
    uint32_t captured_map_id = 0;
    int captured_x = 0;
    int captured_y = 0;

    MovementHandler::Callbacks callbacks;
    callbacks.on_teleport = [&](uint32_t map_id, int x, int y) {
        captured_map_id = map_id;
        captured_x = x;
        captured_y = y;
        ++called;
    };

    MovementHandler handler(callbacks);

    flatbuffers::FlatBufferBuilder builder;
    const auto teleport = mir2::proto::CreateTeleport(builder, /*map_id=*/3u, /*x=*/12, /*y=*/34);
    builder.Finish(teleport);

    handler.HandleTeleport(MakePacket(MsgId::kTeleport, BuildPayload(builder)));

    EXPECT_EQ(called, 1);
    EXPECT_EQ(captured_map_id, 3u);
    EXPECT_EQ(captured_x, 12);
    EXPECT_EQ(captured_y, 34);
}

TEST(MovementHandler, RespawnTriggersCallback) {
    int called = 0;
    mir2::game::events::RespawnEvent captured{};

    MovementHandler::Callbacks callbacks;
    callbacks.on_respawn = [&](const mir2::game::events::RespawnEvent& event) {
        captured = event;
        ++called;
    };

    MovementHandler handler(callbacks);

    flatbuffers::FlatBufferBuilder builder;
    const auto respawn = mir2::proto::CreateRespawn(builder,
                                                    /*entity_id=*/123u,
                                                    mir2::proto::EntityType::PLAYER,
                                                    /*x=*/10,
                                                    /*y=*/20,
                                                    /*hp=*/88,
                                                    /*mp=*/11);
    builder.Finish(respawn);

    handler.HandleRespawn(MakePacket(MsgId::kRespawn, BuildPayload(builder)));

    EXPECT_EQ(called, 1);
    EXPECT_EQ(captured.entity_id, 123u);
    EXPECT_EQ(captured.entity_type, mir2::proto::EntityType::PLAYER);
    EXPECT_EQ(captured.x, 10);
    EXPECT_EQ(captured.y, 20);
    EXPECT_EQ(captured.hp, 88);
    EXPECT_EQ(captured.mp, 11);
}

TEST(MovementHandler, StateSyncTriggersCallback) {
    int called = 0;
    mir2::game::events::StateSyncEvent captured{};

    MovementHandler::Callbacks callbacks;
    callbacks.on_state_sync = [&](const mir2::game::events::StateSyncEvent& event) {
        captured = event;
        ++called;
    };

    MovementHandler handler(callbacks);

    flatbuffers::FlatBufferBuilder builder;
    auto player_name = builder.CreateString("tester");
    auto player = mir2::proto::CreatePlayerInfo(builder,
                                                 /*id=*/1001u,
                                                 player_name,
                                                 mir2::proto::Profession::WARRIOR,
                                                 /*level=*/20,
                                                 /*hp=*/180,
                                                 /*max_hp=*/200,
                                                 /*mp=*/66,
                                                 /*max_mp=*/80,
                                                 /*map_id=*/2u,
                                                 /*x=*/90,
                                                 /*y=*/91,
                                                 /*gold=*/999u);
    std::vector<flatbuffers::Offset<mir2::proto::EntitySnapshot>> entities;
    entities.push_back(mir2::proto::CreateEntitySnapshot(builder,
                                                         /*entity_id=*/2002u,
                                                         mir2::proto::EntityType::PLAYER,
                                                         /*x=*/95,
                                                         /*y=*/96,
                                                         /*direction=*/3,
                                                         /*hp=*/120,
                                                         /*max_hp=*/150,
                                                         /*mp=*/45,
                                                         /*max_mp=*/70));
    auto entities_offset = builder.CreateVector(entities);
    auto sync = mir2::proto::CreateStateSync(builder, player, entities_offset);
    builder.Finish(sync);

    handler.HandleStateSync(MakePacket(MsgId::kStateSync, BuildPayload(builder)));

    EXPECT_EQ(called, 1);
    EXPECT_EQ(captured.player_id, 1001u);
    EXPECT_EQ(captured.map_id, 2u);
    EXPECT_EQ(captured.x, 90);
    EXPECT_EQ(captured.y, 91);
    ASSERT_EQ(captured.entities.size(), 1u);
    EXPECT_EQ(captured.entities[0].entity_id, 2002u);
    EXPECT_EQ(captured.entities[0].direction, 3u);
}
