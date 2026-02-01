#include <gtest/gtest.h>

#include <flatbuffers/flatbuffers.h>

#include <string>
#include <vector>

#include "client/handlers/combat_handler.h"
#include "common/enums.h"
#include "common/protocol/message_codec.h"
#include "combat_generated.h"
#include "item_generated.h"

namespace {

using mir2::common::MsgId;
using mir2::common::NetworkPacket;
using mir2::game::handlers::CombatHandler;

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

TEST(CombatHandler, AttackResponseTriggersCallback) {
    int called = 0;
    mir2::proto::ErrorCode captured_code = mir2::proto::ErrorCode::ERR_UNKNOWN;
    uint64_t captured_attacker = 0;
    uint64_t captured_target = 0;
    int captured_damage = 0;
    int captured_target_hp = 0;
    bool captured_dead = false;

    CombatHandler::Callbacks callbacks;
    callbacks.on_attack_response = [&](mir2::proto::ErrorCode code,
                                       uint64_t attacker,
                                       uint64_t target,
                                       int damage,
                                       int target_hp,
                                       bool target_dead) {
        captured_code = code;
        captured_attacker = attacker;
        captured_target = target;
        captured_damage = damage;
        captured_target_hp = target_hp;
        captured_dead = target_dead;
        ++called;
    };

    CombatHandler handler(callbacks);

    mir2::common::AttackResponse response;
    response.code = mir2::proto::ErrorCode::ERR_OK;
    response.attacker_id = 11u;
    response.target_id = 22u;
    response.damage = 33;
    response.target_hp = 44;
    response.target_dead = true;
    mir2::common::MessageCodecStatus status = mir2::common::MessageCodecStatus::kOk;
    auto payload = mir2::common::EncodeAttackResponse(response, &status);
    ASSERT_EQ(status, mir2::common::MessageCodecStatus::kOk);

    handler.HandleAttackResponse(MakePacket(MsgId::kAttackRsp, std::move(payload)));

    EXPECT_EQ(called, 1);
    EXPECT_EQ(captured_code, mir2::proto::ErrorCode::ERR_OK);
    EXPECT_EQ(captured_attacker, 11u);
    EXPECT_EQ(captured_target, 22u);
    EXPECT_EQ(captured_damage, 33);
    EXPECT_EQ(captured_target_hp, 44);
    EXPECT_TRUE(captured_dead);
}

TEST(CombatHandler, PickupItemResponseTriggersCallback) {
    int called = 0;
    mir2::proto::ErrorCode captured_code = mir2::proto::ErrorCode::ERR_UNKNOWN;
    uint32_t captured_item = 0;

    CombatHandler::Callbacks callbacks;
    callbacks.on_pickup_item_response = [&](mir2::proto::ErrorCode code, uint32_t item_id) {
        captured_code = code;
        captured_item = item_id;
        ++called;
    };

    CombatHandler handler(callbacks);

    flatbuffers::FlatBufferBuilder builder;
    const auto rsp = mir2::proto::CreatePickupItemRsp(
        builder, mir2::proto::ErrorCode::ERR_OK, 123u);
    builder.Finish(rsp);

    handler.HandlePickupItemResponse(MakePacket(MsgId::kPickupItemRsp, BuildPayload(builder)));

    EXPECT_EQ(called, 1);
    EXPECT_EQ(captured_code, mir2::proto::ErrorCode::ERR_OK);
    EXPECT_EQ(captured_item, 123u);
}

TEST(CombatHandler, AttackResponseInvalidPayloadReportsParseError) {
    int parse_calls = 0;
    std::string captured_error;

    CombatHandler::Callbacks callbacks;
    callbacks.on_parse_error = [&](const std::string& error) {
        captured_error = error;
        ++parse_calls;
    };

    CombatHandler handler(callbacks);

    std::vector<uint8_t> payload = {0x1, 0x2};
    handler.HandleAttackResponse(MakePacket(MsgId::kAttackRsp, payload));

    EXPECT_EQ(parse_calls, 1);
    EXPECT_EQ(captured_error, "Invalid attack response payload");
}

TEST(CombatHandler, AttackResponseWrongMsgIdReportsParseError) {
    int parse_calls = 0;
    std::string captured_error;

    CombatHandler::Callbacks callbacks;
    callbacks.on_parse_error = [&](const std::string& error) {
        captured_error = error;
        ++parse_calls;
    };

    CombatHandler handler(callbacks);

    mir2::common::AttackResponse response;
    response.code = mir2::proto::ErrorCode::ERR_OK;
    response.attacker_id = 1u;
    response.target_id = 2u;
    response.damage = 3;
    response.target_hp = 4;
    response.target_dead = false;
    mir2::common::MessageCodecStatus status = mir2::common::MessageCodecStatus::kOk;
    auto payload = mir2::common::EncodeAttackResponse(response, &status);
    ASSERT_EQ(status, mir2::common::MessageCodecStatus::kOk);

    handler.HandleAttackResponse(MakePacket(MsgId::kMoveRsp, std::move(payload)));

    EXPECT_EQ(parse_calls, 1);
    EXPECT_EQ(captured_error, "Invalid attack response payload");
}

TEST(CombatHandler, PickupItemResponseEmptyPayloadReportsParseError) {
    int parse_calls = 0;
    std::string captured_error;

    CombatHandler::Callbacks callbacks;
    callbacks.on_parse_error = [&](const std::string& error) {
        captured_error = error;
        ++parse_calls;
    };

    CombatHandler handler(callbacks);

    handler.HandlePickupItemResponse(MakePacket(MsgId::kPickupItemRsp, {}));

    EXPECT_EQ(parse_calls, 1);
    EXPECT_EQ(captured_error, "Empty pickup item response payload");
}
