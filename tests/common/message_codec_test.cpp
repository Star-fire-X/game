#include <gtest/gtest.h>

#include <limits>
#include <string>

#include "common/protocol/message_codec.h"

namespace {

std::string MakeString(size_t length) {
    return std::string(length, 'a');
}

}  // namespace

TEST(MessageCodecTest, LoginRequestRoundTrip) {
    mir2::common::LoginRequest request;
    request.username = "user";
    request.password = "pass";
    request.version = "0.1.0";

    mir2::common::MessageCodecStatus status = mir2::common::MessageCodecStatus::kOk;
    auto payload = mir2::common::EncodeLoginRequest(request, &status);
    ASSERT_EQ(status, mir2::common::MessageCodecStatus::kOk);
    ASSERT_FALSE(payload.empty());

    mir2::common::LoginRequest decoded;
    status = mir2::common::DecodeLoginRequest(mir2::common::kLoginRequestMsgId, payload, &decoded);
    EXPECT_EQ(status, mir2::common::MessageCodecStatus::kOk);
    EXPECT_EQ(decoded.username, request.username);
    EXPECT_EQ(decoded.password, request.password);
    EXPECT_EQ(decoded.version, request.version);
}

TEST(MessageCodecTest, LoginResponseRoundTrip) {
    mir2::common::LoginResponse response;
    response.code = mir2::proto::ErrorCode::ERR_OK;
    response.account_id = 42;
    response.session_token = "token";

    mir2::common::MessageCodecStatus status = mir2::common::MessageCodecStatus::kOk;
    auto payload = mir2::common::EncodeLoginResponse(response, &status);
    ASSERT_EQ(status, mir2::common::MessageCodecStatus::kOk);
    ASSERT_FALSE(payload.empty());

    mir2::common::LoginResponse decoded;
    status = mir2::common::DecodeLoginResponse(mir2::common::kLoginResponseMsgId, payload, &decoded);
    EXPECT_EQ(status, mir2::common::MessageCodecStatus::kOk);
    EXPECT_EQ(decoded.code, response.code);
    EXPECT_EQ(decoded.account_id, response.account_id);
    EXPECT_EQ(decoded.session_token, response.session_token);
}

TEST(MessageCodecTest, CreateCharacterRequestRoundTrip) {
    mir2::common::CreateCharacterRequest request;
    request.name = MakeString(mir2::common::kMaxCharacterNameLength);
    request.profession = mir2::proto::Profession::WARRIOR;
    request.gender = mir2::proto::Gender::FEMALE;

    mir2::common::MessageCodecStatus status = mir2::common::MessageCodecStatus::kOk;
    auto payload = mir2::common::EncodeCreateCharacterRequest(request, &status);
    ASSERT_EQ(status, mir2::common::MessageCodecStatus::kOk);
    ASSERT_FALSE(payload.empty());

    mir2::common::CreateCharacterRequest decoded;
    status = mir2::common::DecodeCreateCharacterRequest(
        mir2::common::kCreateCharacterRequestMsgId, payload, &decoded);
    EXPECT_EQ(status, mir2::common::MessageCodecStatus::kOk);
    EXPECT_EQ(decoded.name, request.name);
    EXPECT_EQ(decoded.profession, request.profession);
    EXPECT_EQ(decoded.gender, request.gender);
}

TEST(MessageCodecTest, CreateCharacterResponseRoundTrip) {
    mir2::common::CreateCharacterResponse response;
    response.code = mir2::proto::ErrorCode::ERR_NAME_EXISTS;
    response.player_id = 0;

    mir2::common::MessageCodecStatus status = mir2::common::MessageCodecStatus::kOk;
    auto payload = mir2::common::EncodeCreateCharacterResponse(response, &status);
    ASSERT_EQ(status, mir2::common::MessageCodecStatus::kOk);
    ASSERT_FALSE(payload.empty());

    mir2::common::CreateCharacterResponse decoded;
    status = mir2::common::DecodeCreateCharacterResponse(
        mir2::common::kCreateCharacterResponseMsgId, payload, &decoded);
    EXPECT_EQ(status, mir2::common::MessageCodecStatus::kOk);
    EXPECT_EQ(decoded.code, response.code);
    EXPECT_EQ(decoded.player_id, response.player_id);
}

TEST(MessageCodecTest, MoveRequestRoundTrip) {
    mir2::common::MoveRequest request;
    request.target_x = 0;
    request.target_y = std::numeric_limits<int32_t>::max();

    mir2::common::MessageCodecStatus status = mir2::common::MessageCodecStatus::kOk;
    auto payload = mir2::common::EncodeMoveRequest(request, &status);
    ASSERT_EQ(status, mir2::common::MessageCodecStatus::kOk);
    ASSERT_FALSE(payload.empty());

    mir2::common::MoveRequest decoded;
    status = mir2::common::DecodeMoveRequest(mir2::common::kMoveRequestMsgId, payload, &decoded);
    EXPECT_EQ(status, mir2::common::MessageCodecStatus::kOk);
    EXPECT_EQ(decoded.target_x, request.target_x);
    EXPECT_EQ(decoded.target_y, request.target_y);
}

TEST(MessageCodecTest, MoveResponseRoundTrip) {
    mir2::common::MoveResponse response;
    response.code = mir2::proto::ErrorCode::ERR_OK;
    response.x = std::numeric_limits<int32_t>::max();
    response.y = 0;

    mir2::common::MessageCodecStatus status = mir2::common::MessageCodecStatus::kOk;
    auto payload = mir2::common::EncodeMoveResponse(response, &status);
    ASSERT_EQ(status, mir2::common::MessageCodecStatus::kOk);
    ASSERT_FALSE(payload.empty());

    mir2::common::MoveResponse decoded;
    status = mir2::common::DecodeMoveResponse(mir2::common::kMoveResponseMsgId, payload, &decoded);
    EXPECT_EQ(status, mir2::common::MessageCodecStatus::kOk);
    EXPECT_EQ(decoded.code, response.code);
    EXPECT_EQ(decoded.x, response.x);
    EXPECT_EQ(decoded.y, response.y);
}

TEST(MessageCodecTest, AttackRequestRoundTrip) {
    mir2::common::AttackRequest request;
    request.target_id = 123;
    request.target_type = mir2::proto::EntityType::PLAYER;

    mir2::common::MessageCodecStatus status = mir2::common::MessageCodecStatus::kOk;
    auto payload = mir2::common::EncodeAttackRequest(request, &status);
    ASSERT_EQ(status, mir2::common::MessageCodecStatus::kOk);
    ASSERT_FALSE(payload.empty());

    mir2::common::AttackRequest decoded;
    status = mir2::common::DecodeAttackRequest(mir2::common::kAttackRequestMsgId, payload, &decoded);
    EXPECT_EQ(status, mir2::common::MessageCodecStatus::kOk);
    EXPECT_EQ(decoded.target_id, request.target_id);
    EXPECT_EQ(decoded.target_type, request.target_type);
}

TEST(MessageCodecTest, AttackResponseRoundTrip) {
    mir2::common::AttackResponse response;
    response.code = mir2::proto::ErrorCode::ERR_OK;
    response.attacker_id = 11;
    response.target_id = 22;
    response.damage = 5;
    response.target_hp = 9;
    response.target_dead = false;

    mir2::common::MessageCodecStatus status = mir2::common::MessageCodecStatus::kOk;
    auto payload = mir2::common::EncodeAttackResponse(response, &status);
    ASSERT_EQ(status, mir2::common::MessageCodecStatus::kOk);
    ASSERT_FALSE(payload.empty());

    mir2::common::AttackResponse decoded;
    status = mir2::common::DecodeAttackResponse(mir2::common::kAttackResponseMsgId, payload, &decoded);
    EXPECT_EQ(status, mir2::common::MessageCodecStatus::kOk);
    EXPECT_EQ(decoded.code, response.code);
    EXPECT_EQ(decoded.attacker_id, response.attacker_id);
    EXPECT_EQ(decoded.target_id, response.target_id);
    EXPECT_EQ(decoded.damage, response.damage);
    EXPECT_EQ(decoded.target_hp, response.target_hp);
    EXPECT_EQ(decoded.target_dead, response.target_dead);
}

TEST(MessageCodecTest, SkillRequestRoundTrip) {
    mir2::common::SkillRequest request;
    request.skill_id = 11001;
    request.target_id = 22002;

    mir2::common::MessageCodecStatus status = mir2::common::MessageCodecStatus::kOk;
    auto payload = mir2::common::EncodeSkillRequest(request, &status);
    ASSERT_EQ(status, mir2::common::MessageCodecStatus::kOk);
    ASSERT_FALSE(payload.empty());

    mir2::common::SkillRequest decoded;
    status = mir2::common::DecodeSkillRequest(mir2::common::kSkillRequestMsgId, payload, &decoded);
    EXPECT_EQ(status, mir2::common::MessageCodecStatus::kOk);
    EXPECT_EQ(decoded.skill_id, request.skill_id);
    EXPECT_EQ(decoded.target_id, request.target_id);
}

TEST(MessageCodecTest, SkillResponseRoundTrip) {
    mir2::common::SkillResponse response;
    response.code = mir2::proto::ErrorCode::ERR_OK;
    response.caster_id = 31;
    response.target_id = 32;
    response.damage = 120;
    response.healing = 5;
    response.target_dead = false;
    response.skill_id = 410;
    response.result = mir2::proto::SkillResult::HIT;
    response.cooldown_ms = 1500;

    mir2::common::MessageCodecStatus status = mir2::common::MessageCodecStatus::kOk;
    auto payload = mir2::common::EncodeSkillResponse(response, &status);
    ASSERT_EQ(status, mir2::common::MessageCodecStatus::kOk);
    ASSERT_FALSE(payload.empty());

    mir2::common::SkillResponse decoded;
    status = mir2::common::DecodeSkillResponse(mir2::common::kSkillResponseMsgId, payload, &decoded);
    EXPECT_EQ(status, mir2::common::MessageCodecStatus::kOk);
    EXPECT_EQ(decoded.code, response.code);
    EXPECT_EQ(decoded.caster_id, response.caster_id);
    EXPECT_EQ(decoded.target_id, response.target_id);
    EXPECT_EQ(decoded.damage, response.damage);
    EXPECT_EQ(decoded.healing, response.healing);
    EXPECT_EQ(decoded.target_dead, response.target_dead);
    EXPECT_EQ(decoded.skill_id, response.skill_id);
    EXPECT_EQ(decoded.result, response.result);
    EXPECT_EQ(decoded.cooldown_ms, response.cooldown_ms);
}

TEST(MessageCodecTest, UseItemRoundTrip) {
    mir2::common::UseItemRequest request;
    request.slot = 8;
    request.item_id = 5001;
    mir2::common::MessageCodecStatus status = mir2::common::MessageCodecStatus::kOk;
    auto req_payload = mir2::common::EncodeUseItemRequest(request, &status);
    ASSERT_EQ(status, mir2::common::MessageCodecStatus::kOk);
    ASSERT_FALSE(req_payload.empty());
    mir2::common::UseItemRequest decoded_req;
    status = mir2::common::DecodeUseItemRequest(mir2::common::kUseItemRequestMsgId, req_payload, &decoded_req);
    EXPECT_EQ(status, mir2::common::MessageCodecStatus::kOk);
    EXPECT_EQ(decoded_req.slot, request.slot);
    EXPECT_EQ(decoded_req.item_id, request.item_id);

    mir2::common::UseItemResponse response;
    response.code = mir2::proto::ErrorCode::ERR_OK;
    response.slot = request.slot;
    response.item_id = request.item_id;
    response.remaining = 3;
    auto rsp_payload = mir2::common::EncodeUseItemResponse(response, &status);
    ASSERT_EQ(status, mir2::common::MessageCodecStatus::kOk);
    ASSERT_FALSE(rsp_payload.empty());
    mir2::common::UseItemResponse decoded_rsp;
    status = mir2::common::DecodeUseItemResponse(mir2::common::kUseItemResponseMsgId, rsp_payload, &decoded_rsp);
    EXPECT_EQ(status, mir2::common::MessageCodecStatus::kOk);
    EXPECT_EQ(decoded_rsp.code, response.code);
    EXPECT_EQ(decoded_rsp.slot, response.slot);
    EXPECT_EQ(decoded_rsp.item_id, response.item_id);
    EXPECT_EQ(decoded_rsp.remaining, response.remaining);
}

TEST(MessageCodecTest, EquipUnequipRoundTrip) {
    mir2::common::EquipRequest equip_request;
    equip_request.slot = 1;
    equip_request.item_id = 7001;
    mir2::common::MessageCodecStatus status = mir2::common::MessageCodecStatus::kOk;
    auto equip_req_payload = mir2::common::EncodeEquipRequest(equip_request, &status);
    ASSERT_EQ(status, mir2::common::MessageCodecStatus::kOk);
    ASSERT_FALSE(equip_req_payload.empty());
    mir2::common::EquipRequest decoded_equip_req;
    status = mir2::common::DecodeEquipRequest(mir2::common::kEquipRequestMsgId, equip_req_payload, &decoded_equip_req);
    EXPECT_EQ(status, mir2::common::MessageCodecStatus::kOk);
    EXPECT_EQ(decoded_equip_req.slot, equip_request.slot);
    EXPECT_EQ(decoded_equip_req.item_id, equip_request.item_id);

    mir2::common::EquipResponse equip_response;
    equip_response.code = mir2::proto::ErrorCode::ERR_OK;
    equip_response.slot = equip_request.slot;
    equip_response.item_id = equip_request.item_id;
    auto equip_rsp_payload = mir2::common::EncodeEquipResponse(equip_response, &status);
    ASSERT_EQ(status, mir2::common::MessageCodecStatus::kOk);
    ASSERT_FALSE(equip_rsp_payload.empty());
    mir2::common::EquipResponse decoded_equip_rsp;
    status = mir2::common::DecodeEquipResponse(mir2::common::kEquipResponseMsgId, equip_rsp_payload, &decoded_equip_rsp);
    EXPECT_EQ(status, mir2::common::MessageCodecStatus::kOk);
    EXPECT_EQ(decoded_equip_rsp.code, equip_response.code);
    EXPECT_EQ(decoded_equip_rsp.slot, equip_response.slot);
    EXPECT_EQ(decoded_equip_rsp.item_id, equip_response.item_id);

    mir2::common::UnequipRequest unequip_request;
    unequip_request.slot = equip_request.slot;
    auto unequip_req_payload = mir2::common::EncodeUnequipRequest(unequip_request, &status);
    ASSERT_EQ(status, mir2::common::MessageCodecStatus::kOk);
    ASSERT_FALSE(unequip_req_payload.empty());
    mir2::common::UnequipRequest decoded_unequip_req;
    status = mir2::common::DecodeUnequipRequest(
        mir2::common::kUnequipRequestMsgId, unequip_req_payload, &decoded_unequip_req);
    EXPECT_EQ(status, mir2::common::MessageCodecStatus::kOk);
    EXPECT_EQ(decoded_unequip_req.slot, unequip_request.slot);

    mir2::common::UnequipResponse unequip_response;
    unequip_response.code = mir2::proto::ErrorCode::ERR_OK;
    unequip_response.slot = equip_request.slot;
    unequip_response.item_id = equip_request.item_id;
    auto unequip_rsp_payload = mir2::common::EncodeUnequipResponse(unequip_response, &status);
    ASSERT_EQ(status, mir2::common::MessageCodecStatus::kOk);
    ASSERT_FALSE(unequip_rsp_payload.empty());
    mir2::common::UnequipResponse decoded_unequip_rsp;
    status = mir2::common::DecodeUnequipResponse(
        mir2::common::kUnequipResponseMsgId, unequip_rsp_payload, &decoded_unequip_rsp);
    EXPECT_EQ(status, mir2::common::MessageCodecStatus::kOk);
    EXPECT_EQ(decoded_unequip_rsp.code, unequip_response.code);
    EXPECT_EQ(decoded_unequip_rsp.slot, unequip_response.slot);
    EXPECT_EQ(decoded_unequip_rsp.item_id, unequip_response.item_id);
}

TEST(MessageCodecTest, PickupDropRoundTrip) {
    mir2::common::PickupItemRequest pickup_request;
    pickup_request.item_id = 5002;
    mir2::common::MessageCodecStatus status = mir2::common::MessageCodecStatus::kOk;
    auto pickup_req_payload = mir2::common::EncodePickupItemRequest(pickup_request, &status);
    ASSERT_EQ(status, mir2::common::MessageCodecStatus::kOk);
    ASSERT_FALSE(pickup_req_payload.empty());
    mir2::common::PickupItemRequest decoded_pickup_req;
    status = mir2::common::DecodePickupItemRequest(
        mir2::common::kPickupItemRequestMsgId, pickup_req_payload, &decoded_pickup_req);
    EXPECT_EQ(status, mir2::common::MessageCodecStatus::kOk);
    EXPECT_EQ(decoded_pickup_req.item_id, pickup_request.item_id);

    mir2::common::PickupItemResponse pickup_response;
    pickup_response.code = mir2::proto::ErrorCode::ERR_OK;
    pickup_response.item_id = pickup_request.item_id;
    auto pickup_rsp_payload = mir2::common::EncodePickupItemResponse(pickup_response, &status);
    ASSERT_EQ(status, mir2::common::MessageCodecStatus::kOk);
    ASSERT_FALSE(pickup_rsp_payload.empty());
    mir2::common::PickupItemResponse decoded_pickup_rsp;
    status = mir2::common::DecodePickupItemResponse(
        mir2::common::kPickupItemResponseMsgId, pickup_rsp_payload, &decoded_pickup_rsp);
    EXPECT_EQ(status, mir2::common::MessageCodecStatus::kOk);
    EXPECT_EQ(decoded_pickup_rsp.code, pickup_response.code);
    EXPECT_EQ(decoded_pickup_rsp.item_id, pickup_response.item_id);

    mir2::common::DropItemRequest drop_request;
    drop_request.slot = 8;
    drop_request.item_id = 5002;
    drop_request.count = 3;
    auto drop_req_payload = mir2::common::EncodeDropItemRequest(drop_request, &status);
    ASSERT_EQ(status, mir2::common::MessageCodecStatus::kOk);
    ASSERT_FALSE(drop_req_payload.empty());
    mir2::common::DropItemRequest decoded_drop_req;
    status = mir2::common::DecodeDropItemRequest(
        mir2::common::kDropItemRequestMsgId, drop_req_payload, &decoded_drop_req);
    EXPECT_EQ(status, mir2::common::MessageCodecStatus::kOk);
    EXPECT_EQ(decoded_drop_req.slot, drop_request.slot);
    EXPECT_EQ(decoded_drop_req.item_id, drop_request.item_id);
    EXPECT_EQ(decoded_drop_req.count, drop_request.count);

    mir2::common::DropItemResponse drop_response;
    drop_response.code = mir2::proto::ErrorCode::ERR_OK;
    drop_response.item_id = drop_request.item_id;
    drop_response.count = drop_request.count;
    auto drop_rsp_payload = mir2::common::EncodeDropItemResponse(drop_response, &status);
    ASSERT_EQ(status, mir2::common::MessageCodecStatus::kOk);
    ASSERT_FALSE(drop_rsp_payload.empty());
    mir2::common::DropItemResponse decoded_drop_rsp;
    status = mir2::common::DecodeDropItemResponse(
        mir2::common::kDropItemResponseMsgId, drop_rsp_payload, &decoded_drop_rsp);
    EXPECT_EQ(status, mir2::common::MessageCodecStatus::kOk);
    EXPECT_EQ(decoded_drop_rsp.code, drop_response.code);
    EXPECT_EQ(decoded_drop_rsp.item_id, drop_response.item_id);
    EXPECT_EQ(decoded_drop_rsp.count, drop_response.count);
}

TEST(MessageCodecTest, ChatRoundTrip) {
    mir2::common::ChatRequest request;
    request.channel = mir2::proto::ChatChannel::WORLD;
    request.content = "hello world";
    request.target_id = 0;
    mir2::common::MessageCodecStatus status = mir2::common::MessageCodecStatus::kOk;
    auto req_payload = mir2::common::EncodeChatRequest(request, &status);
    ASSERT_EQ(status, mir2::common::MessageCodecStatus::kOk);
    ASSERT_FALSE(req_payload.empty());
    mir2::common::ChatRequest decoded_req;
    status = mir2::common::DecodeChatRequest(mir2::common::kChatRequestMsgId, req_payload, &decoded_req);
    EXPECT_EQ(status, mir2::common::MessageCodecStatus::kOk);
    EXPECT_EQ(decoded_req.channel, request.channel);
    EXPECT_EQ(decoded_req.content, request.content);
    EXPECT_EQ(decoded_req.target_id, request.target_id);

    mir2::common::ChatResponse response;
    response.code = mir2::proto::ErrorCode::ERR_OK;
    auto rsp_payload = mir2::common::EncodeChatResponse(response, &status);
    ASSERT_EQ(status, mir2::common::MessageCodecStatus::kOk);
    ASSERT_FALSE(rsp_payload.empty());
    mir2::common::ChatResponse decoded_rsp;
    status = mir2::common::DecodeChatResponse(mir2::common::kChatResponseMsgId, rsp_payload, &decoded_rsp);
    EXPECT_EQ(status, mir2::common::MessageCodecStatus::kOk);
    EXPECT_EQ(decoded_rsp.code, response.code);
}

TEST(MessageCodecTest, GuildCoreRoundTrip) {
    mir2::common::GuildCreateRequest create_request;
    create_request.guild_name = "Crimson";
    mir2::common::MessageCodecStatus status = mir2::common::MessageCodecStatus::kOk;
    auto create_req_payload = mir2::common::EncodeGuildCreateRequest(create_request, &status);
    ASSERT_EQ(status, mir2::common::MessageCodecStatus::kOk);
    ASSERT_FALSE(create_req_payload.empty());
    mir2::common::GuildCreateRequest decoded_create_req;
    status = mir2::common::DecodeGuildCreateRequest(
        mir2::common::kGuildCreateRequestMsgId, create_req_payload, &decoded_create_req);
    EXPECT_EQ(status, mir2::common::MessageCodecStatus::kOk);
    EXPECT_EQ(decoded_create_req.guild_name, create_request.guild_name);

    mir2::common::GuildCreateResponse create_response;
    create_response.success = true;
    create_response.error_code = 0;
    create_response.guild_id = 8001;
    auto create_rsp_payload = mir2::common::EncodeGuildCreateResponse(create_response, &status);
    ASSERT_EQ(status, mir2::common::MessageCodecStatus::kOk);
    ASSERT_FALSE(create_rsp_payload.empty());
    mir2::common::GuildCreateResponse decoded_create_rsp;
    status = mir2::common::DecodeGuildCreateResponse(
        mir2::common::kGuildCreateResponseMsgId, create_rsp_payload, &decoded_create_rsp);
    EXPECT_EQ(status, mir2::common::MessageCodecStatus::kOk);
    EXPECT_EQ(decoded_create_rsp.success, create_response.success);
    EXPECT_EQ(decoded_create_rsp.error_code, create_response.error_code);
    EXPECT_EQ(decoded_create_rsp.guild_id, create_response.guild_id);

    mir2::common::GuildJoinRequest join_request;
    join_request.guild_id = 8001;
    auto join_req_payload = mir2::common::EncodeGuildJoinRequest(join_request, &status);
    ASSERT_EQ(status, mir2::common::MessageCodecStatus::kOk);
    ASSERT_FALSE(join_req_payload.empty());
    mir2::common::GuildJoinRequest decoded_join_req;
    status = mir2::common::DecodeGuildJoinRequest(
        mir2::common::kGuildJoinRequestMsgId, join_req_payload, &decoded_join_req);
    EXPECT_EQ(status, mir2::common::MessageCodecStatus::kOk);
    EXPECT_EQ(decoded_join_req.guild_id, join_request.guild_id);

    mir2::common::GuildJoinResponse join_response;
    join_response.success = true;
    join_response.error_code = 0;
    auto join_rsp_payload = mir2::common::EncodeGuildJoinResponse(join_response, &status);
    ASSERT_EQ(status, mir2::common::MessageCodecStatus::kOk);
    ASSERT_FALSE(join_rsp_payload.empty());
    mir2::common::GuildJoinResponse decoded_join_rsp;
    status = mir2::common::DecodeGuildJoinResponse(
        mir2::common::kGuildJoinResponseMsgId, join_rsp_payload, &decoded_join_rsp);
    EXPECT_EQ(status, mir2::common::MessageCodecStatus::kOk);
    EXPECT_EQ(decoded_join_rsp.success, join_response.success);
    EXPECT_EQ(decoded_join_rsp.error_code, join_response.error_code);

    mir2::common::GuildLeaveRequest leave_request;
    auto leave_req_payload = mir2::common::EncodeGuildLeaveRequest(leave_request, &status);
    ASSERT_EQ(status, mir2::common::MessageCodecStatus::kOk);
    ASSERT_FALSE(leave_req_payload.empty());
    mir2::common::GuildLeaveRequest decoded_leave_req;
    status = mir2::common::DecodeGuildLeaveRequest(
        mir2::common::kGuildLeaveRequestMsgId, leave_req_payload, &decoded_leave_req);
    EXPECT_EQ(status, mir2::common::MessageCodecStatus::kOk);

    mir2::common::GuildLeaveResponse leave_response;
    leave_response.success = true;
    leave_response.error_code = 0;
    auto leave_rsp_payload = mir2::common::EncodeGuildLeaveResponse(leave_response, &status);
    ASSERT_EQ(status, mir2::common::MessageCodecStatus::kOk);
    ASSERT_FALSE(leave_rsp_payload.empty());
    mir2::common::GuildLeaveResponse decoded_leave_rsp;
    status = mir2::common::DecodeGuildLeaveResponse(
        mir2::common::kGuildLeaveResponseMsgId, leave_rsp_payload, &decoded_leave_rsp);
    EXPECT_EQ(status, mir2::common::MessageCodecStatus::kOk);
    EXPECT_EQ(decoded_leave_rsp.success, leave_response.success);
    EXPECT_EQ(decoded_leave_rsp.error_code, leave_response.error_code);
}

TEST(MessageCodecTest, GuildExtendedRoundTrip) {
    mir2::common::MessageCodecStatus status = mir2::common::MessageCodecStatus::kOk;

    mir2::common::GuildKickRequest kick_request;
    kick_request.target_character_id = 7001;
    auto kick_req_payload = mir2::common::EncodeGuildKickRequest(kick_request, &status);
    ASSERT_EQ(status, mir2::common::MessageCodecStatus::kOk);
    ASSERT_FALSE(kick_req_payload.empty());
    mir2::common::GuildKickRequest decoded_kick_req;
    status = mir2::common::DecodeGuildKickRequest(
        mir2::common::kGuildKickRequestMsgId, kick_req_payload, &decoded_kick_req);
    EXPECT_EQ(status, mir2::common::MessageCodecStatus::kOk);
    EXPECT_EQ(decoded_kick_req.target_character_id, kick_request.target_character_id);

    mir2::common::GuildKickResponse kick_response;
    kick_response.success = true;
    kick_response.error_code = 0;
    auto kick_rsp_payload = mir2::common::EncodeGuildKickResponse(kick_response, &status);
    ASSERT_EQ(status, mir2::common::MessageCodecStatus::kOk);
    ASSERT_FALSE(kick_rsp_payload.empty());
    mir2::common::GuildKickResponse decoded_kick_rsp;
    status = mir2::common::DecodeGuildKickResponse(
        mir2::common::kGuildKickResponseMsgId, kick_rsp_payload, &decoded_kick_rsp);
    EXPECT_EQ(status, mir2::common::MessageCodecStatus::kOk);
    EXPECT_EQ(decoded_kick_rsp.success, kick_response.success);
    EXPECT_EQ(decoded_kick_rsp.error_code, kick_response.error_code);

    mir2::common::GuildDeclareWarRequest declare_war_request;
    declare_war_request.target_guild_id = 9102;
    auto declare_req_payload = mir2::common::EncodeGuildDeclareWarRequest(declare_war_request, &status);
    ASSERT_EQ(status, mir2::common::MessageCodecStatus::kOk);
    ASSERT_FALSE(declare_req_payload.empty());
    mir2::common::GuildDeclareWarRequest decoded_declare_req;
    status = mir2::common::DecodeGuildDeclareWarRequest(
        mir2::common::kGuildDeclareWarRequestMsgId, declare_req_payload, &decoded_declare_req);
    EXPECT_EQ(status, mir2::common::MessageCodecStatus::kOk);
    EXPECT_EQ(decoded_declare_req.target_guild_id, declare_war_request.target_guild_id);

    mir2::common::GuildDeclareWarResponse declare_war_response;
    declare_war_response.success = true;
    declare_war_response.error_code = 0;
    auto declare_rsp_payload =
        mir2::common::EncodeGuildDeclareWarResponse(declare_war_response, &status);
    ASSERT_EQ(status, mir2::common::MessageCodecStatus::kOk);
    ASSERT_FALSE(declare_rsp_payload.empty());
    mir2::common::GuildDeclareWarResponse decoded_declare_rsp;
    status = mir2::common::DecodeGuildDeclareWarResponse(
        mir2::common::kGuildDeclareWarResponseMsgId, declare_rsp_payload, &decoded_declare_rsp);
    EXPECT_EQ(status, mir2::common::MessageCodecStatus::kOk);
    EXPECT_EQ(decoded_declare_rsp.success, declare_war_response.success);
    EXPECT_EQ(decoded_declare_rsp.error_code, declare_war_response.error_code);

    mir2::common::GuildCancelWarRequest cancel_war_request;
    cancel_war_request.target_guild_id = 9102;
    auto cancel_req_payload = mir2::common::EncodeGuildCancelWarRequest(cancel_war_request, &status);
    ASSERT_EQ(status, mir2::common::MessageCodecStatus::kOk);
    ASSERT_FALSE(cancel_req_payload.empty());
    mir2::common::GuildCancelWarRequest decoded_cancel_req;
    status = mir2::common::DecodeGuildCancelWarRequest(
        mir2::common::kGuildCancelWarRequestMsgId, cancel_req_payload, &decoded_cancel_req);
    EXPECT_EQ(status, mir2::common::MessageCodecStatus::kOk);
    EXPECT_EQ(decoded_cancel_req.target_guild_id, cancel_war_request.target_guild_id);

    mir2::common::GuildCancelWarResponse cancel_war_response;
    cancel_war_response.success = true;
    cancel_war_response.error_code = 0;
    auto cancel_rsp_payload = mir2::common::EncodeGuildCancelWarResponse(cancel_war_response, &status);
    ASSERT_EQ(status, mir2::common::MessageCodecStatus::kOk);
    ASSERT_FALSE(cancel_rsp_payload.empty());
    mir2::common::GuildCancelWarResponse decoded_cancel_rsp;
    status = mir2::common::DecodeGuildCancelWarResponse(
        mir2::common::kGuildCancelWarResponseMsgId, cancel_rsp_payload, &decoded_cancel_rsp);
    EXPECT_EQ(status, mir2::common::MessageCodecStatus::kOk);
    EXPECT_EQ(decoded_cancel_rsp.success, cancel_war_response.success);
    EXPECT_EQ(decoded_cancel_rsp.error_code, cancel_war_response.error_code);

    mir2::common::GuildMakeAllyRequest make_ally_request;
    make_ally_request.target_guild_id = 9103;
    auto make_ally_req_payload = mir2::common::EncodeGuildMakeAllyRequest(make_ally_request, &status);
    ASSERT_EQ(status, mir2::common::MessageCodecStatus::kOk);
    ASSERT_FALSE(make_ally_req_payload.empty());
    mir2::common::GuildMakeAllyRequest decoded_make_ally_req;
    status = mir2::common::DecodeGuildMakeAllyRequest(
        mir2::common::kGuildMakeAllyRequestMsgId, make_ally_req_payload, &decoded_make_ally_req);
    EXPECT_EQ(status, mir2::common::MessageCodecStatus::kOk);
    EXPECT_EQ(decoded_make_ally_req.target_guild_id, make_ally_request.target_guild_id);

    mir2::common::GuildMakeAllyResponse make_ally_response;
    make_ally_response.success = true;
    make_ally_response.error_code = 0;
    auto make_ally_rsp_payload = mir2::common::EncodeGuildMakeAllyResponse(make_ally_response, &status);
    ASSERT_EQ(status, mir2::common::MessageCodecStatus::kOk);
    ASSERT_FALSE(make_ally_rsp_payload.empty());
    mir2::common::GuildMakeAllyResponse decoded_make_ally_rsp;
    status = mir2::common::DecodeGuildMakeAllyResponse(
        mir2::common::kGuildMakeAllyResponseMsgId, make_ally_rsp_payload, &decoded_make_ally_rsp);
    EXPECT_EQ(status, mir2::common::MessageCodecStatus::kOk);
    EXPECT_EQ(decoded_make_ally_rsp.success, make_ally_response.success);
    EXPECT_EQ(decoded_make_ally_rsp.error_code, make_ally_response.error_code);

    mir2::common::GuildBreakAllyRequest break_ally_request;
    break_ally_request.target_guild_id = 9103;
    auto break_ally_req_payload =
        mir2::common::EncodeGuildBreakAllyRequest(break_ally_request, &status);
    ASSERT_EQ(status, mir2::common::MessageCodecStatus::kOk);
    ASSERT_FALSE(break_ally_req_payload.empty());
    mir2::common::GuildBreakAllyRequest decoded_break_ally_req;
    status = mir2::common::DecodeGuildBreakAllyRequest(
        mir2::common::kGuildBreakAllyRequestMsgId, break_ally_req_payload, &decoded_break_ally_req);
    EXPECT_EQ(status, mir2::common::MessageCodecStatus::kOk);
    EXPECT_EQ(decoded_break_ally_req.target_guild_id, break_ally_request.target_guild_id);

    mir2::common::GuildBreakAllyResponse break_ally_response;
    break_ally_response.success = true;
    break_ally_response.error_code = 0;
    auto break_ally_rsp_payload =
        mir2::common::EncodeGuildBreakAllyResponse(break_ally_response, &status);
    ASSERT_EQ(status, mir2::common::MessageCodecStatus::kOk);
    ASSERT_FALSE(break_ally_rsp_payload.empty());
    mir2::common::GuildBreakAllyResponse decoded_break_ally_rsp;
    status = mir2::common::DecodeGuildBreakAllyResponse(
        mir2::common::kGuildBreakAllyResponseMsgId, break_ally_rsp_payload, &decoded_break_ally_rsp);
    EXPECT_EQ(status, mir2::common::MessageCodecStatus::kOk);
    EXPECT_EQ(decoded_break_ally_rsp.success, break_ally_response.success);
    EXPECT_EQ(decoded_break_ally_rsp.error_code, break_ally_response.error_code);
}

TEST(MessageCodecTest, TradeRoundTrip) {
    mir2::common::MessageCodecStatus status = mir2::common::MessageCodecStatus::kOk;

    mir2::common::TradeRequest trade_request;
    trade_request.target_character_id = 2002;
    auto trade_req_payload = mir2::common::EncodeTradeRequest(trade_request, &status);
    ASSERT_EQ(status, mir2::common::MessageCodecStatus::kOk);
    ASSERT_FALSE(trade_req_payload.empty());
    mir2::common::TradeRequest decoded_trade_req;
    status = mir2::common::DecodeTradeRequest(
        mir2::common::kTradeRequestMsgId, trade_req_payload, &decoded_trade_req);
    EXPECT_EQ(status, mir2::common::MessageCodecStatus::kOk);
    EXPECT_EQ(decoded_trade_req.target_character_id, trade_request.target_character_id);

    mir2::common::TradeResponse trade_response;
    trade_response.success = true;
    trade_response.error_code = 0;
    trade_response.trade_id = 9001;
    auto trade_rsp_payload = mir2::common::EncodeTradeResponse(trade_response, &status);
    ASSERT_EQ(status, mir2::common::MessageCodecStatus::kOk);
    ASSERT_FALSE(trade_rsp_payload.empty());
    mir2::common::TradeResponse decoded_trade_rsp;
    status = mir2::common::DecodeTradeResponse(
        mir2::common::kTradeResponseMsgId, trade_rsp_payload, &decoded_trade_rsp);
    EXPECT_EQ(status, mir2::common::MessageCodecStatus::kOk);
    EXPECT_EQ(decoded_trade_rsp.success, trade_response.success);
    EXPECT_EQ(decoded_trade_rsp.error_code, trade_response.error_code);
    EXPECT_EQ(decoded_trade_rsp.trade_id, trade_response.trade_id);

    mir2::common::TradeAddItemRequest add_item_request;
    add_item_request.trade_id = 9001;
    add_item_request.inventory_slot = 4;
    add_item_request.item_id = 5001;
    add_item_request.count = 1;
    auto add_item_req_payload = mir2::common::EncodeTradeAddItemRequest(add_item_request, &status);
    ASSERT_EQ(status, mir2::common::MessageCodecStatus::kOk);
    ASSERT_FALSE(add_item_req_payload.empty());
    mir2::common::TradeAddItemRequest decoded_add_item_req;
    status = mir2::common::DecodeTradeAddItemRequest(
        mir2::common::kTradeAddItemRequestMsgId, add_item_req_payload, &decoded_add_item_req);
    EXPECT_EQ(status, mir2::common::MessageCodecStatus::kOk);
    EXPECT_EQ(decoded_add_item_req.trade_id, add_item_request.trade_id);
    EXPECT_EQ(decoded_add_item_req.inventory_slot, add_item_request.inventory_slot);
    EXPECT_EQ(decoded_add_item_req.item_id, add_item_request.item_id);
    EXPECT_EQ(decoded_add_item_req.count, add_item_request.count);

    mir2::common::TradeAddItemResponse add_item_response;
    add_item_response.success = true;
    add_item_response.error_code = 0;
    auto add_item_rsp_payload = mir2::common::EncodeTradeAddItemResponse(add_item_response, &status);
    ASSERT_EQ(status, mir2::common::MessageCodecStatus::kOk);
    ASSERT_FALSE(add_item_rsp_payload.empty());
    mir2::common::TradeAddItemResponse decoded_add_item_rsp;
    status = mir2::common::DecodeTradeAddItemResponse(
        mir2::common::kTradeAddItemResponseMsgId, add_item_rsp_payload, &decoded_add_item_rsp);
    EXPECT_EQ(status, mir2::common::MessageCodecStatus::kOk);
    EXPECT_EQ(decoded_add_item_rsp.success, add_item_response.success);
    EXPECT_EQ(decoded_add_item_rsp.error_code, add_item_response.error_code);

    mir2::common::TradeSetGoldRequest set_gold_request;
    set_gold_request.trade_id = 9001;
    set_gold_request.gold = 1234;
    auto set_gold_req_payload = mir2::common::EncodeTradeSetGoldRequest(set_gold_request, &status);
    ASSERT_EQ(status, mir2::common::MessageCodecStatus::kOk);
    ASSERT_FALSE(set_gold_req_payload.empty());
    mir2::common::TradeSetGoldRequest decoded_set_gold_req;
    status = mir2::common::DecodeTradeSetGoldRequest(
        mir2::common::kTradeSetGoldRequestMsgId, set_gold_req_payload, &decoded_set_gold_req);
    EXPECT_EQ(status, mir2::common::MessageCodecStatus::kOk);
    EXPECT_EQ(decoded_set_gold_req.trade_id, set_gold_request.trade_id);
    EXPECT_EQ(decoded_set_gold_req.gold, set_gold_request.gold);

    mir2::common::TradeSetGoldResponse set_gold_response;
    set_gold_response.success = true;
    set_gold_response.error_code = 0;
    auto set_gold_rsp_payload = mir2::common::EncodeTradeSetGoldResponse(set_gold_response, &status);
    ASSERT_EQ(status, mir2::common::MessageCodecStatus::kOk);
    ASSERT_FALSE(set_gold_rsp_payload.empty());
    mir2::common::TradeSetGoldResponse decoded_set_gold_rsp;
    status = mir2::common::DecodeTradeSetGoldResponse(
        mir2::common::kTradeSetGoldResponseMsgId, set_gold_rsp_payload, &decoded_set_gold_rsp);
    EXPECT_EQ(status, mir2::common::MessageCodecStatus::kOk);
    EXPECT_EQ(decoded_set_gold_rsp.success, set_gold_response.success);
    EXPECT_EQ(decoded_set_gold_rsp.error_code, set_gold_response.error_code);

    mir2::common::TradeConfirmRequest confirm_request;
    confirm_request.trade_id = 9001;
    auto confirm_req_payload = mir2::common::EncodeTradeConfirmRequest(confirm_request, &status);
    ASSERT_EQ(status, mir2::common::MessageCodecStatus::kOk);
    ASSERT_FALSE(confirm_req_payload.empty());
    mir2::common::TradeConfirmRequest decoded_confirm_req;
    status = mir2::common::DecodeTradeConfirmRequest(
        mir2::common::kTradeConfirmRequestMsgId, confirm_req_payload, &decoded_confirm_req);
    EXPECT_EQ(status, mir2::common::MessageCodecStatus::kOk);
    EXPECT_EQ(decoded_confirm_req.trade_id, confirm_request.trade_id);

    mir2::common::TradeConfirmResponse confirm_response;
    confirm_response.success = true;
    confirm_response.error_code = 0;
    auto confirm_rsp_payload = mir2::common::EncodeTradeConfirmResponse(confirm_response, &status);
    ASSERT_EQ(status, mir2::common::MessageCodecStatus::kOk);
    ASSERT_FALSE(confirm_rsp_payload.empty());
    mir2::common::TradeConfirmResponse decoded_confirm_rsp;
    status = mir2::common::DecodeTradeConfirmResponse(
        mir2::common::kTradeConfirmResponseMsgId, confirm_rsp_payload, &decoded_confirm_rsp);
    EXPECT_EQ(status, mir2::common::MessageCodecStatus::kOk);
    EXPECT_EQ(decoded_confirm_rsp.success, confirm_response.success);
    EXPECT_EQ(decoded_confirm_rsp.error_code, confirm_response.error_code);

    mir2::common::TradeCancelRequest cancel_request;
    cancel_request.trade_id = 9001;
    auto cancel_req_payload = mir2::common::EncodeTradeCancelRequest(cancel_request, &status);
    ASSERT_EQ(status, mir2::common::MessageCodecStatus::kOk);
    ASSERT_FALSE(cancel_req_payload.empty());
    mir2::common::TradeCancelRequest decoded_cancel_req;
    status = mir2::common::DecodeTradeCancelRequest(
        mir2::common::kTradeCancelRequestMsgId, cancel_req_payload, &decoded_cancel_req);
    EXPECT_EQ(status, mir2::common::MessageCodecStatus::kOk);
    EXPECT_EQ(decoded_cancel_req.trade_id, cancel_request.trade_id);

    mir2::common::TradeCancelResponse cancel_response;
    cancel_response.success = true;
    cancel_response.error_code = 0;
    auto cancel_rsp_payload = mir2::common::EncodeTradeCancelResponse(cancel_response, &status);
    ASSERT_EQ(status, mir2::common::MessageCodecStatus::kOk);
    ASSERT_FALSE(cancel_rsp_payload.empty());
    mir2::common::TradeCancelResponse decoded_cancel_rsp;
    status = mir2::common::DecodeTradeCancelResponse(
        mir2::common::kTradeCancelResponseMsgId, cancel_rsp_payload, &decoded_cancel_rsp);
    EXPECT_EQ(status, mir2::common::MessageCodecStatus::kOk);
    EXPECT_EQ(decoded_cancel_rsp.success, cancel_response.success);
    EXPECT_EQ(decoded_cancel_rsp.error_code, cancel_response.error_code);

    mir2::common::TradeUpdateMessage update_message;
    update_message.trade_id = 9001;
    update_message.left_character_id = 1001;
    update_message.right_character_id = 1002;
    update_message.left_items.push_back({1, 5001, 1});
    update_message.right_items.push_back({2, 5002, 2});
    update_message.left_gold = 100;
    update_message.right_gold = 200;
    update_message.left_confirmed = true;
    update_message.right_confirmed = false;
    auto update_payload = mir2::common::EncodeTradeUpdateMessage(update_message, &status);
    ASSERT_EQ(status, mir2::common::MessageCodecStatus::kOk);
    ASSERT_FALSE(update_payload.empty());
    mir2::common::TradeUpdateMessage decoded_update;
    status = mir2::common::DecodeTradeUpdateMessage(
        mir2::common::kTradeUpdateMsgId, update_payload, &decoded_update);
    EXPECT_EQ(status, mir2::common::MessageCodecStatus::kOk);
    EXPECT_EQ(decoded_update.trade_id, update_message.trade_id);
    EXPECT_EQ(decoded_update.left_character_id, update_message.left_character_id);
    EXPECT_EQ(decoded_update.right_character_id, update_message.right_character_id);
    EXPECT_EQ(decoded_update.left_items.size(), update_message.left_items.size());
    EXPECT_EQ(decoded_update.right_items.size(), update_message.right_items.size());

    mir2::common::TradeCompleteMessage complete_message;
    complete_message.trade_id = 9001;
    complete_message.success = true;
    complete_message.error_code = 0;
    auto complete_payload = mir2::common::EncodeTradeCompleteMessage(complete_message, &status);
    ASSERT_EQ(status, mir2::common::MessageCodecStatus::kOk);
    ASSERT_FALSE(complete_payload.empty());
    mir2::common::TradeCompleteMessage decoded_complete;
    status = mir2::common::DecodeTradeCompleteMessage(
        mir2::common::kTradeCompleteMsgId, complete_payload, &decoded_complete);
    EXPECT_EQ(status, mir2::common::MessageCodecStatus::kOk);
    EXPECT_EQ(decoded_complete.trade_id, complete_message.trade_id);
    EXPECT_EQ(decoded_complete.success, complete_message.success);
    EXPECT_EQ(decoded_complete.error_code, complete_message.error_code);
}

TEST(MessageCodecTest, PartyRoundTrip) {
    mir2::common::MessageCodecStatus status = mir2::common::MessageCodecStatus::kOk;

    mir2::common::PartyInviteRequest invite_request;
    invite_request.target_character_id = 2002;
    auto invite_req_payload = mir2::common::EncodePartyInviteRequest(invite_request, &status);
    ASSERT_EQ(status, mir2::common::MessageCodecStatus::kOk);
    ASSERT_FALSE(invite_req_payload.empty());
    mir2::common::PartyInviteRequest decoded_invite_req;
    status = mir2::common::DecodePartyInviteRequest(
        mir2::common::kPartyInviteRequestMsgId, invite_req_payload, &decoded_invite_req);
    EXPECT_EQ(status, mir2::common::MessageCodecStatus::kOk);
    EXPECT_EQ(decoded_invite_req.target_character_id, invite_request.target_character_id);

    mir2::common::PartyInviteResponse invite_response;
    invite_response.success = true;
    invite_response.error_code = 0;
    auto invite_rsp_payload = mir2::common::EncodePartyInviteResponse(invite_response, &status);
    ASSERT_EQ(status, mir2::common::MessageCodecStatus::kOk);
    ASSERT_FALSE(invite_rsp_payload.empty());
    mir2::common::PartyInviteResponse decoded_invite_rsp;
    status = mir2::common::DecodePartyInviteResponse(
        mir2::common::kPartyInviteResponseMsgId, invite_rsp_payload, &decoded_invite_rsp);
    EXPECT_EQ(status, mir2::common::MessageCodecStatus::kOk);
    EXPECT_EQ(decoded_invite_rsp.success, invite_response.success);
    EXPECT_EQ(decoded_invite_rsp.error_code, invite_response.error_code);

    mir2::common::PartyJoinRequest join_request;
    join_request.party_id = 7001;
    auto join_req_payload = mir2::common::EncodePartyJoinRequest(join_request, &status);
    ASSERT_EQ(status, mir2::common::MessageCodecStatus::kOk);
    ASSERT_FALSE(join_req_payload.empty());
    mir2::common::PartyJoinRequest decoded_join_req;
    status = mir2::common::DecodePartyJoinRequest(
        mir2::common::kPartyJoinRequestMsgId, join_req_payload, &decoded_join_req);
    EXPECT_EQ(status, mir2::common::MessageCodecStatus::kOk);
    EXPECT_EQ(decoded_join_req.party_id, join_request.party_id);

    mir2::common::PartyJoinResponse join_response;
    join_response.success = true;
    join_response.error_code = 0;
    auto join_rsp_payload = mir2::common::EncodePartyJoinResponse(join_response, &status);
    ASSERT_EQ(status, mir2::common::MessageCodecStatus::kOk);
    ASSERT_FALSE(join_rsp_payload.empty());
    mir2::common::PartyJoinResponse decoded_join_rsp;
    status = mir2::common::DecodePartyJoinResponse(
        mir2::common::kPartyJoinResponseMsgId, join_rsp_payload, &decoded_join_rsp);
    EXPECT_EQ(status, mir2::common::MessageCodecStatus::kOk);
    EXPECT_EQ(decoded_join_rsp.success, join_response.success);
    EXPECT_EQ(decoded_join_rsp.error_code, join_response.error_code);

    mir2::common::PartyLeaveRequest leave_request;
    auto leave_req_payload = mir2::common::EncodePartyLeaveRequest(leave_request, &status);
    ASSERT_EQ(status, mir2::common::MessageCodecStatus::kOk);
    ASSERT_FALSE(leave_req_payload.empty());
    mir2::common::PartyLeaveRequest decoded_leave_req;
    status = mir2::common::DecodePartyLeaveRequest(
        mir2::common::kPartyLeaveRequestMsgId, leave_req_payload, &decoded_leave_req);
    EXPECT_EQ(status, mir2::common::MessageCodecStatus::kOk);

    mir2::common::PartyLeaveResponse leave_response;
    leave_response.success = true;
    leave_response.error_code = 0;
    auto leave_rsp_payload = mir2::common::EncodePartyLeaveResponse(leave_response, &status);
    ASSERT_EQ(status, mir2::common::MessageCodecStatus::kOk);
    ASSERT_FALSE(leave_rsp_payload.empty());
    mir2::common::PartyLeaveResponse decoded_leave_rsp;
    status = mir2::common::DecodePartyLeaveResponse(
        mir2::common::kPartyLeaveResponseMsgId, leave_rsp_payload, &decoded_leave_rsp);
    EXPECT_EQ(status, mir2::common::MessageCodecStatus::kOk);
    EXPECT_EQ(decoded_leave_rsp.success, leave_response.success);
    EXPECT_EQ(decoded_leave_rsp.error_code, leave_response.error_code);

    mir2::common::PartyKickRequest kick_request;
    kick_request.target_character_id = 2002;
    auto kick_req_payload = mir2::common::EncodePartyKickRequest(kick_request, &status);
    ASSERT_EQ(status, mir2::common::MessageCodecStatus::kOk);
    ASSERT_FALSE(kick_req_payload.empty());
    mir2::common::PartyKickRequest decoded_kick_req;
    status = mir2::common::DecodePartyKickRequest(
        mir2::common::kPartyKickRequestMsgId, kick_req_payload, &decoded_kick_req);
    EXPECT_EQ(status, mir2::common::MessageCodecStatus::kOk);
    EXPECT_EQ(decoded_kick_req.target_character_id, kick_request.target_character_id);

    mir2::common::PartyKickResponse kick_response;
    kick_response.success = true;
    kick_response.error_code = 0;
    auto kick_rsp_payload = mir2::common::EncodePartyKickResponse(kick_response, &status);
    ASSERT_EQ(status, mir2::common::MessageCodecStatus::kOk);
    ASSERT_FALSE(kick_rsp_payload.empty());
    mir2::common::PartyKickResponse decoded_kick_rsp;
    status = mir2::common::DecodePartyKickResponse(
        mir2::common::kPartyKickResponseMsgId, kick_rsp_payload, &decoded_kick_rsp);
    EXPECT_EQ(status, mir2::common::MessageCodecStatus::kOk);
    EXPECT_EQ(decoded_kick_rsp.success, kick_response.success);
    EXPECT_EQ(decoded_kick_rsp.error_code, kick_response.error_code);

    mir2::common::PartyUpdateMessage update_message;
    update_message.party_id = 7001;
    update_message.leader_character_id = 1001;
    update_message.members.push_back({1001, "Leader", 100, 120, 1, 10, 10, true});
    update_message.members.push_back({1002, "Member", 90, 120, 1, 11, 10, true});
    auto update_payload = mir2::common::EncodePartyUpdateMessage(update_message, &status);
    ASSERT_EQ(status, mir2::common::MessageCodecStatus::kOk);
    ASSERT_FALSE(update_payload.empty());
    mir2::common::PartyUpdateMessage decoded_update;
    status = mir2::common::DecodePartyUpdateMessage(
        mir2::common::kPartyUpdateMsgId, update_payload, &decoded_update);
    EXPECT_EQ(status, mir2::common::MessageCodecStatus::kOk);
    EXPECT_EQ(decoded_update.party_id, update_message.party_id);
    EXPECT_EQ(decoded_update.leader_character_id, update_message.leader_character_id);
    EXPECT_EQ(decoded_update.members.size(), update_message.members.size());

    mir2::common::PartyUpdateMessage clear_message;
    clear_message.party_id = 0;
    clear_message.leader_character_id = 0;
    auto clear_payload = mir2::common::EncodePartyUpdateMessage(clear_message, &status);
    ASSERT_EQ(status, mir2::common::MessageCodecStatus::kOk);
    ASSERT_FALSE(clear_payload.empty());
    mir2::common::PartyUpdateMessage decoded_clear;
    status = mir2::common::DecodePartyUpdateMessage(
        mir2::common::kPartyUpdateMsgId, clear_payload, &decoded_clear);
    EXPECT_EQ(status, mir2::common::MessageCodecStatus::kOk);
    EXPECT_EQ(decoded_clear.party_id, 0u);
    EXPECT_EQ(decoded_clear.leader_character_id, 0u);
}

TEST(MessageCodecTest, GuildExtendedV2RoundTrip) {
    mir2::common::MessageCodecStatus status = mir2::common::MessageCodecStatus::kOk;

    mir2::common::GuildUpdateNoticeRequest notice_request;
    notice_request.notice_lines = {"line1", "line2"};
    auto notice_req_payload = mir2::common::EncodeGuildUpdateNoticeRequest(notice_request, &status);
    ASSERT_EQ(status, mir2::common::MessageCodecStatus::kOk);
    ASSERT_FALSE(notice_req_payload.empty());
    mir2::common::GuildUpdateNoticeRequest decoded_notice_req;
    status = mir2::common::DecodeGuildUpdateNoticeRequest(
        mir2::common::kGuildUpdateNoticeRequestMsgId, notice_req_payload, &decoded_notice_req);
    EXPECT_EQ(status, mir2::common::MessageCodecStatus::kOk);
    EXPECT_EQ(decoded_notice_req.notice_lines, notice_request.notice_lines);

    mir2::common::GuildUpdateRankRequest rank_request;
    rank_request.members.push_back({1001, 1});
    auto rank_req_payload = mir2::common::EncodeGuildUpdateRankRequest(rank_request, &status);
    ASSERT_EQ(status, mir2::common::MessageCodecStatus::kOk);
    ASSERT_FALSE(rank_req_payload.empty());
    mir2::common::GuildUpdateRankRequest decoded_rank_req;
    status = mir2::common::DecodeGuildUpdateRankRequest(
        mir2::common::kGuildUpdateRankRequestMsgId, rank_req_payload, &decoded_rank_req);
    EXPECT_EQ(status, mir2::common::MessageCodecStatus::kOk);
    ASSERT_EQ(decoded_rank_req.members.size(), 1u);
    EXPECT_EQ(decoded_rank_req.members.front().character_id, 1001u);

    mir2::common::GuildInfoSyncMessage sync_message;
    sync_message.has_guild = true;
    sync_message.guild_id = 9001;
    sync_message.guild_name = "Crimson";
    auto sync_payload = mir2::common::EncodeGuildInfoSyncMessage(sync_message, &status);
    ASSERT_EQ(status, mir2::common::MessageCodecStatus::kOk);
    ASSERT_FALSE(sync_payload.empty());
    mir2::common::GuildInfoSyncMessage decoded_sync;
    status = mir2::common::DecodeGuildInfoSyncMessage(
        mir2::common::kGuildInfoSyncMsgId, sync_payload, &decoded_sync);
    EXPECT_EQ(status, mir2::common::MessageCodecStatus::kOk);
    EXPECT_EQ(decoded_sync.has_guild, sync_message.has_guild);
    EXPECT_EQ(decoded_sync.guild_id, sync_message.guild_id);
    EXPECT_EQ(decoded_sync.guild_name, sync_message.guild_name);
}

TEST(MessageCodecTest, RankingMailAchievementAuctionRoundTrip) {
    mir2::common::MessageCodecStatus status = mir2::common::MessageCodecStatus::kOk;

    mir2::common::RankingRequest ranking_request;
    ranking_request.ranking_type = mir2::proto::RankingType::LEVEL;
    ranking_request.page = 1;
    ranking_request.page_size = 20;
    auto ranking_req_payload = mir2::common::EncodeRankingRequest(ranking_request, &status);
    ASSERT_EQ(status, mir2::common::MessageCodecStatus::kOk);
    ASSERT_FALSE(ranking_req_payload.empty());
    mir2::common::RankingRequest decoded_ranking_req;
    status = mir2::common::DecodeRankingRequest(
        mir2::common::kRankingRequestMsgId, ranking_req_payload, &decoded_ranking_req);
    EXPECT_EQ(status, mir2::common::MessageCodecStatus::kOk);
    EXPECT_EQ(decoded_ranking_req.ranking_type, ranking_request.ranking_type);

    mir2::common::MailSendRequest mail_send_request;
    mail_send_request.target_character_id = 1002;
    mail_send_request.subject = "subject";
    mail_send_request.content = "content";
    mail_send_request.gold = 10;
    mail_send_request.items.push_back({5001, 1});
    auto mail_send_req_payload = mir2::common::EncodeMailSendRequest(mail_send_request, &status);
    ASSERT_EQ(status, mir2::common::MessageCodecStatus::kOk);
    ASSERT_FALSE(mail_send_req_payload.empty());
    mir2::common::MailSendRequest decoded_mail_send_req;
    status = mir2::common::DecodeMailSendRequest(
        mir2::common::kMailSendRequestMsgId, mail_send_req_payload, &decoded_mail_send_req);
    EXPECT_EQ(status, mir2::common::MessageCodecStatus::kOk);
    EXPECT_EQ(decoded_mail_send_req.target_character_id, mail_send_request.target_character_id);

    mir2::common::AchievementClaimRequest achievement_claim_request;
    achievement_claim_request.achievement_id = 66;
    auto achievement_claim_req_payload =
        mir2::common::EncodeAchievementClaimRequest(achievement_claim_request, &status);
    ASSERT_EQ(status, mir2::common::MessageCodecStatus::kOk);
    ASSERT_FALSE(achievement_claim_req_payload.empty());
    mir2::common::AchievementClaimRequest decoded_achievement_claim_req;
    status = mir2::common::DecodeAchievementClaimRequest(
        mir2::common::kAchievementClaimRequestMsgId, achievement_claim_req_payload,
        &decoded_achievement_claim_req);
    EXPECT_EQ(status, mir2::common::MessageCodecStatus::kOk);
    EXPECT_EQ(decoded_achievement_claim_req.achievement_id,
              achievement_claim_request.achievement_id);

    mir2::common::AuctionSellRequest auction_sell_request;
    auction_sell_request.inventory_slot = 2;
    auction_sell_request.item_id = 5001;
    auction_sell_request.count = 1;
    auction_sell_request.unit_price = 777;
    auction_sell_request.duration_sec = 3600;
    auto auction_sell_req_payload =
        mir2::common::EncodeAuctionSellRequest(auction_sell_request, &status);
    ASSERT_EQ(status, mir2::common::MessageCodecStatus::kOk);
    ASSERT_FALSE(auction_sell_req_payload.empty());
    mir2::common::AuctionSellRequest decoded_auction_sell_req;
    status = mir2::common::DecodeAuctionSellRequest(
        mir2::common::kAuctionSellRequestMsgId, auction_sell_req_payload, &decoded_auction_sell_req);
    EXPECT_EQ(status, mir2::common::MessageCodecStatus::kOk);
    EXPECT_EQ(decoded_auction_sell_req.item_id, auction_sell_request.item_id);
}

TEST(MessageCodecTest, NewMessageValidationRejectsMissingFields) {
    mir2::common::GuildInfoSyncMessage guild_sync;
    guild_sync.has_guild = true;
    guild_sync.guild_id = 0;
    EXPECT_EQ(mir2::common::ValidateGuildInfoSyncMessage(guild_sync),
              mir2::common::MessageCodecStatus::kMissingField);

    mir2::common::RankingRequest ranking_request;
    ranking_request.ranking_type = mir2::proto::RankingType::LEVEL;
    ranking_request.page = 0;
    ranking_request.page_size = 20;
    EXPECT_EQ(mir2::common::ValidateRankingRequest(ranking_request),
              mir2::common::MessageCodecStatus::kValueOutOfRange);

    mir2::common::MailSendRequest mail_send_request;
    mail_send_request.target_character_id = 0;
    mail_send_request.subject = "s";
    mail_send_request.content = "c";
    EXPECT_EQ(mir2::common::ValidateMailSendRequest(mail_send_request),
              mir2::common::MessageCodecStatus::kMissingField);

    mir2::common::AchievementClaimRequest achievement_claim_request;
    achievement_claim_request.achievement_id = 0;
    EXPECT_EQ(mir2::common::ValidateAchievementClaimRequest(achievement_claim_request),
              mir2::common::MessageCodecStatus::kMissingField);

    mir2::common::AuctionSellRequest auction_sell_request;
    auction_sell_request.item_id = 0;
    auction_sell_request.count = 1;
    auction_sell_request.unit_price = 1;
    auction_sell_request.duration_sec = 1;
    EXPECT_EQ(mir2::common::ValidateAuctionSellRequest(auction_sell_request),
              mir2::common::MessageCodecStatus::kMissingField);
}

TEST(MessageCodecTest, LoginRequestRejectsEmptyFields) {
    mir2::common::LoginRequest request;
    request.username = "";
    request.password = "pass";

    EXPECT_EQ(mir2::common::ValidateLoginRequest(request),
              mir2::common::MessageCodecStatus::kMissingField);

    mir2::common::MessageCodecStatus status = mir2::common::MessageCodecStatus::kOk;
    auto payload = mir2::common::EncodeLoginRequest(request, &status);
    EXPECT_TRUE(payload.empty());
    EXPECT_EQ(status, mir2::common::MessageCodecStatus::kMissingField);
}

TEST(MessageCodecTest, LoginRequestRejectsLongVersion) {
    mir2::common::LoginRequest request;
    request.username = "user";
    request.password = "pass";
    request.version = MakeString(mir2::common::kMaxLoginVersionLength + 1);

    EXPECT_EQ(mir2::common::ValidateLoginRequest(request),
              mir2::common::MessageCodecStatus::kStringTooLong);

    mir2::common::MessageCodecStatus status = mir2::common::MessageCodecStatus::kOk;
    auto payload = mir2::common::EncodeLoginRequest(request, &status);
    EXPECT_TRUE(payload.empty());
    EXPECT_EQ(status, mir2::common::MessageCodecStatus::kStringTooLong);
}

TEST(MessageCodecTest, LoginResponseRejectsMissingFieldsOnSuccess) {
    mir2::common::LoginResponse response;
    response.code = mir2::proto::ErrorCode::ERR_OK;
    response.account_id = 0;
    response.session_token = "token";

    EXPECT_EQ(mir2::common::ValidateLoginResponse(response),
              mir2::common::MessageCodecStatus::kMissingField);

    mir2::common::MessageCodecStatus status = mir2::common::MessageCodecStatus::kOk;
    auto payload = mir2::common::EncodeLoginResponse(response, &status);
    EXPECT_TRUE(payload.empty());
    EXPECT_EQ(status, mir2::common::MessageCodecStatus::kMissingField);

    response.account_id = 42;
    response.session_token.clear();
    EXPECT_EQ(mir2::common::ValidateLoginResponse(response),
              mir2::common::MessageCodecStatus::kMissingField);
}

TEST(MessageCodecTest, CreateCharacterRejectsLongName) {
    mir2::common::CreateCharacterRequest request;
    request.name = MakeString(mir2::common::kMaxCharacterNameLength + 1);
    request.profession = mir2::proto::Profession::WARRIOR;
    request.gender = mir2::proto::Gender::MALE;

    EXPECT_EQ(mir2::common::ValidateCreateCharacterRequest(request),
              mir2::common::MessageCodecStatus::kStringTooLong);
}

TEST(MessageCodecTest, CreateCharacterRejectsShortName) {
    mir2::common::CreateCharacterRequest request;
    request.name = "a";
    request.profession = mir2::proto::Profession::WARRIOR;
    request.gender = mir2::proto::Gender::MALE;

    EXPECT_EQ(mir2::common::ValidateCreateCharacterRequest(request),
              mir2::common::MessageCodecStatus::kValueOutOfRange);
}

TEST(MessageCodecTest, CreateCharacterRejectsInvalidEnums) {
    mir2::common::CreateCharacterRequest request;
    request.name = "ab";
    request.profession = static_cast<mir2::proto::Profession>(99);
    request.gender = mir2::proto::Gender::MALE;

    EXPECT_EQ(mir2::common::ValidateCreateCharacterRequest(request),
              mir2::common::MessageCodecStatus::kValueOutOfRange);

    request.profession = mir2::proto::Profession::WARRIOR;
    request.gender = static_cast<mir2::proto::Gender>(99);

    EXPECT_EQ(mir2::common::ValidateCreateCharacterRequest(request),
              mir2::common::MessageCodecStatus::kValueOutOfRange);
}

TEST(MessageCodecTest, CreateCharacterResponseRequiresPlayerIdOnSuccess) {
    mir2::common::CreateCharacterResponse response;
    response.code = mir2::proto::ErrorCode::ERR_OK;
    response.player_id = 0;

    EXPECT_EQ(mir2::common::ValidateCreateCharacterResponse(response),
              mir2::common::MessageCodecStatus::kMissingField);

    mir2::common::MessageCodecStatus status = mir2::common::MessageCodecStatus::kOk;
    auto payload = mir2::common::EncodeCreateCharacterResponse(response, &status);
    EXPECT_TRUE(payload.empty());
    EXPECT_EQ(status, mir2::common::MessageCodecStatus::kMissingField);
}

TEST(MessageCodecTest, MoveRequestRejectsNegativePosition) {
    mir2::common::MoveRequest request;
    request.target_x = -1;
    request.target_y = 5;

    EXPECT_EQ(mir2::common::ValidateMoveRequest(request),
              mir2::common::MessageCodecStatus::kValueOutOfRange);
}

TEST(MessageCodecTest, MoveResponseRejectsNegativePositionOnSuccess) {
    mir2::common::MoveResponse response;
    response.code = mir2::proto::ErrorCode::ERR_OK;
    response.x = -1;
    response.y = 0;

    EXPECT_EQ(mir2::common::ValidateMoveResponse(response),
              mir2::common::MessageCodecStatus::kValueOutOfRange);

    response.x = 0;
    response.y = -1;
    EXPECT_EQ(mir2::common::ValidateMoveResponse(response),
              mir2::common::MessageCodecStatus::kValueOutOfRange);
}

TEST(MessageCodecTest, MoveResponseAllowsNegativePositionOnError) {
    mir2::common::MoveResponse response;
    response.code = mir2::proto::ErrorCode::ERR_INVALID_ACTION;
    response.x = -1;
    response.y = -1;

    EXPECT_EQ(mir2::common::ValidateMoveResponse(response),
              mir2::common::MessageCodecStatus::kOk);
}

TEST(MessageCodecTest, AttackRequestRejectsMissingTarget) {
    mir2::common::AttackRequest request;
    request.target_id = 0;
    request.target_type = mir2::proto::EntityType::NONE;

    EXPECT_EQ(mir2::common::ValidateAttackRequest(request),
              mir2::common::MessageCodecStatus::kMissingField);
}

TEST(MessageCodecTest, AttackRequestRejectsInvalidTargetType) {
    mir2::common::AttackRequest request;
    request.target_id = 123;
    request.target_type = static_cast<mir2::proto::EntityType>(99);

    EXPECT_EQ(mir2::common::ValidateAttackRequest(request),
              mir2::common::MessageCodecStatus::kValueOutOfRange);
}

TEST(MessageCodecTest, AttackResponseRequiresIdsOnSuccess) {
    mir2::common::AttackResponse response;
    response.code = mir2::proto::ErrorCode::ERR_OK;
    response.attacker_id = 0;
    response.target_id = 0;

    EXPECT_EQ(mir2::common::ValidateAttackResponse(response),
              mir2::common::MessageCodecStatus::kMissingField);

    mir2::common::MessageCodecStatus status = mir2::common::MessageCodecStatus::kOk;
    auto payload = mir2::common::EncodeAttackResponse(response, &status);
    EXPECT_TRUE(payload.empty());
    EXPECT_EQ(status, mir2::common::MessageCodecStatus::kMissingField);

    response.code = mir2::proto::ErrorCode::ERR_TARGET_NOT_FOUND;
    EXPECT_EQ(mir2::common::ValidateAttackResponse(response),
              mir2::common::MessageCodecStatus::kOk);
}

TEST(MessageCodecTest, SkillRequestRejectsMissingFields) {
    mir2::common::SkillRequest request;
    request.skill_id = 0;
    request.target_id = 100;
    EXPECT_EQ(mir2::common::ValidateSkillRequest(request),
              mir2::common::MessageCodecStatus::kMissingField);

    request.skill_id = 100;
    request.target_id = 0;
    EXPECT_EQ(mir2::common::ValidateSkillRequest(request),
              mir2::common::MessageCodecStatus::kMissingField);
}

TEST(MessageCodecTest, SkillResponseRequiresCasterAndSkillOnSuccess) {
    mir2::common::SkillResponse response;
    response.code = mir2::proto::ErrorCode::ERR_OK;
    response.caster_id = 0;
    response.skill_id = 110;
    response.result = mir2::proto::SkillResult::HIT;
    EXPECT_EQ(mir2::common::ValidateSkillResponse(response),
              mir2::common::MessageCodecStatus::kMissingField);

    response.caster_id = 10;
    response.skill_id = 0;
    EXPECT_EQ(mir2::common::ValidateSkillResponse(response),
              mir2::common::MessageCodecStatus::kMissingField);

    response.code = mir2::proto::ErrorCode::ERR_INVALID_ACTION;
    response.result = static_cast<mir2::proto::SkillResult>(255);
    EXPECT_EQ(mir2::common::ValidateSkillResponse(response),
              mir2::common::MessageCodecStatus::kValueOutOfRange);
}

TEST(MessageCodecTest, ItemAndGuildRequestsRejectMissingFields) {
    mir2::common::UseItemRequest use_item_request;
    use_item_request.slot = 1;
    use_item_request.item_id = 0;
    EXPECT_EQ(mir2::common::ValidateUseItemRequest(use_item_request),
              mir2::common::MessageCodecStatus::kMissingField);

    mir2::common::EquipRequest equip_request;
    equip_request.slot = 1;
    equip_request.item_id = 0;
    EXPECT_EQ(mir2::common::ValidateEquipRequest(equip_request),
              mir2::common::MessageCodecStatus::kMissingField);

    mir2::common::PickupItemRequest pickup_request;
    pickup_request.item_id = 0;
    EXPECT_EQ(mir2::common::ValidatePickupItemRequest(pickup_request),
              mir2::common::MessageCodecStatus::kMissingField);

    mir2::common::DropItemRequest drop_request;
    drop_request.slot = 1;
    drop_request.item_id = 0;
    drop_request.count = 1;
    EXPECT_EQ(mir2::common::ValidateDropItemRequest(drop_request),
              mir2::common::MessageCodecStatus::kMissingField);
    drop_request.item_id = 5001;
    drop_request.count = 0;
    EXPECT_EQ(mir2::common::ValidateDropItemRequest(drop_request),
              mir2::common::MessageCodecStatus::kMissingField);

    mir2::common::GuildCreateRequest create_request;
    create_request.guild_name = "";
    EXPECT_EQ(mir2::common::ValidateGuildCreateRequest(create_request),
              mir2::common::MessageCodecStatus::kMissingField);

    mir2::common::GuildJoinRequest join_request;
    join_request.guild_id = 0;
    EXPECT_EQ(mir2::common::ValidateGuildJoinRequest(join_request),
              mir2::common::MessageCodecStatus::kMissingField);

    mir2::common::GuildKickRequest kick_request;
    kick_request.target_character_id = 0;
    EXPECT_EQ(mir2::common::ValidateGuildKickRequest(kick_request),
              mir2::common::MessageCodecStatus::kMissingField);

    mir2::common::GuildDeclareWarRequest declare_war_request;
    declare_war_request.target_guild_id = 0;
    EXPECT_EQ(mir2::common::ValidateGuildDeclareWarRequest(declare_war_request),
              mir2::common::MessageCodecStatus::kMissingField);

    mir2::common::GuildCancelWarRequest cancel_war_request;
    cancel_war_request.target_guild_id = 0;
    EXPECT_EQ(mir2::common::ValidateGuildCancelWarRequest(cancel_war_request),
              mir2::common::MessageCodecStatus::kMissingField);

    mir2::common::GuildMakeAllyRequest make_ally_request;
    make_ally_request.target_guild_id = 0;
    EXPECT_EQ(mir2::common::ValidateGuildMakeAllyRequest(make_ally_request),
              mir2::common::MessageCodecStatus::kMissingField);

    mir2::common::GuildBreakAllyRequest break_ally_request;
    break_ally_request.target_guild_id = 0;
    EXPECT_EQ(mir2::common::ValidateGuildBreakAllyRequest(break_ally_request),
              mir2::common::MessageCodecStatus::kMissingField);

    mir2::common::TradeRequest trade_request;
    trade_request.target_character_id = 0;
    EXPECT_EQ(mir2::common::ValidateTradeRequest(trade_request),
              mir2::common::MessageCodecStatus::kMissingField);

    mir2::common::TradeAddItemRequest trade_add_item_request;
    trade_add_item_request.trade_id = 0;
    trade_add_item_request.item_id = 0;
    trade_add_item_request.count = 0;
    EXPECT_EQ(mir2::common::ValidateTradeAddItemRequest(trade_add_item_request),
              mir2::common::MessageCodecStatus::kMissingField);

    mir2::common::TradeSetGoldRequest trade_set_gold_request;
    trade_set_gold_request.trade_id = 0;
    EXPECT_EQ(mir2::common::ValidateTradeSetGoldRequest(trade_set_gold_request),
              mir2::common::MessageCodecStatus::kMissingField);

    mir2::common::TradeConfirmRequest trade_confirm_request;
    trade_confirm_request.trade_id = 0;
    EXPECT_EQ(mir2::common::ValidateTradeConfirmRequest(trade_confirm_request),
              mir2::common::MessageCodecStatus::kMissingField);

    mir2::common::TradeCancelRequest trade_cancel_request;
    trade_cancel_request.trade_id = 0;
    EXPECT_EQ(mir2::common::ValidateTradeCancelRequest(trade_cancel_request),
              mir2::common::MessageCodecStatus::kMissingField);

    mir2::common::TradeUpdateMessage trade_update_message;
    trade_update_message.trade_id = 0;
    trade_update_message.left_character_id = 0;
    trade_update_message.right_character_id = 0;
    EXPECT_EQ(mir2::common::ValidateTradeUpdateMessage(trade_update_message),
              mir2::common::MessageCodecStatus::kMissingField);

    mir2::common::TradeCompleteMessage trade_complete_message;
    trade_complete_message.trade_id = 0;
    EXPECT_EQ(mir2::common::ValidateTradeCompleteMessage(trade_complete_message),
              mir2::common::MessageCodecStatus::kMissingField);

    mir2::common::PartyInviteRequest party_invite_request;
    party_invite_request.target_character_id = 0;
    EXPECT_EQ(mir2::common::ValidatePartyInviteRequest(party_invite_request),
              mir2::common::MessageCodecStatus::kMissingField);

    mir2::common::PartyJoinRequest party_join_request;
    party_join_request.party_id = 0;
    EXPECT_EQ(mir2::common::ValidatePartyJoinRequest(party_join_request),
              mir2::common::MessageCodecStatus::kMissingField);

    mir2::common::PartyKickRequest party_kick_request;
    party_kick_request.target_character_id = 0;
    EXPECT_EQ(mir2::common::ValidatePartyKickRequest(party_kick_request),
              mir2::common::MessageCodecStatus::kMissingField);

    mir2::common::PartyUpdateMessage party_update_message;
    party_update_message.party_id = 0;
    party_update_message.leader_character_id = 1;
    EXPECT_EQ(mir2::common::ValidatePartyUpdateMessage(party_update_message),
              mir2::common::MessageCodecStatus::kValueOutOfRange);
}

TEST(MessageCodecTest, ChatRequestRejectsInvalidChannelAndContent) {
    mir2::common::ChatRequest request;
    request.channel = static_cast<mir2::proto::ChatChannel>(255);
    request.content = "ok";
    EXPECT_EQ(mir2::common::ValidateChatRequest(request),
              mir2::common::MessageCodecStatus::kValueOutOfRange);

    request.channel = mir2::proto::ChatChannel::WORLD;
    request.content.clear();
    EXPECT_EQ(mir2::common::ValidateChatRequest(request),
              mir2::common::MessageCodecStatus::kMissingField);

    request.content = MakeString(mir2::common::kMaxChatContentLength + 1);
    EXPECT_EQ(mir2::common::ValidateChatRequest(request),
              mir2::common::MessageCodecStatus::kStringTooLong);
}

TEST(MessageCodecTest, DecodeRejectsNullOutput) {
    EXPECT_EQ(mir2::common::DecodeLoginRequest(
                  mir2::common::kLoginRequestMsgId, nullptr, 0, nullptr),
              mir2::common::MessageCodecStatus::kInvalidPayload);
}

TEST(MessageCodecTest, DecodeRejectsWrongMsgId) {
    mir2::common::LoginRequest request;
    request.username = "user";
    request.password = "pass";

    mir2::common::MessageCodecStatus status = mir2::common::MessageCodecStatus::kOk;
    auto payload = mir2::common::EncodeLoginRequest(request, &status);
    ASSERT_EQ(status, mir2::common::MessageCodecStatus::kOk);

    mir2::common::LoginRequest decoded;
    status = mir2::common::DecodeLoginRequest(mir2::common::kMoveRequestMsgId, payload, &decoded);
    EXPECT_EQ(status, mir2::common::MessageCodecStatus::kInvalidMsgId);
}

TEST(MessageCodecTest, DecodeRejectsInvalidPayload) {
    mir2::common::LoginRequest request;
    request.username = "user";
    request.password = "pass";

    mir2::common::MessageCodecStatus status = mir2::common::MessageCodecStatus::kOk;
    auto payload = mir2::common::EncodeLoginRequest(request, &status);
    ASSERT_EQ(status, mir2::common::MessageCodecStatus::kOk);
    ASSERT_FALSE(payload.empty());

    std::vector<uint8_t> truncated(payload.begin(), payload.begin() + 1);
    mir2::common::LoginRequest decoded;
    status = mir2::common::DecodeLoginRequest(mir2::common::kLoginRequestMsgId, truncated, &decoded);
    EXPECT_EQ(status, mir2::common::MessageCodecStatus::kInvalidPayload);
}
