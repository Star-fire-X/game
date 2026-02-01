#include <gtest/gtest.h>

#include <flatbuffers/flatbuffers.h>

#include <string>
#include <vector>

#include "client/handlers/character_handler.h"
#include "common/enums.h"
#include "common/protocol/message_codec.h"
#include "game_generated.h"
#include "login_generated.h"

namespace {

using mir2::common::MsgId;
using mir2::common::NetworkPacket;
using mir2::game::handlers::CharacterHandler;
using mir2::common::CharacterClass;
using mir2::common::CharacterData;
using mir2::common::Gender;

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

TEST(CharacterHandlerTest, RoleListParsesCharacters) {
    std::vector<CharacterData> captured_characters;

    CharacterHandler::Callbacks callbacks;
    callbacks.get_account_id = []() { return 123u; };
    callbacks.on_character_list = [&](std::vector<CharacterData> characters) {
        captured_characters = std::move(characters);
    };

    CharacterHandler handler(callbacks);

    flatbuffers::FlatBufferBuilder builder;
    const auto name1 = builder.CreateString("Hero");
    const auto name2 = builder.CreateString("Mage");
    const auto role1 = mir2::proto::CreateCharacterInfo(
        builder, 10u, name1, mir2::proto::Profession::WARRIOR,
        mir2::proto::Gender::MALE, 5, 1, 100, 200, 500u);
    const auto role2 = mir2::proto::CreateCharacterInfo(
        builder, 20u, name2, mir2::proto::Profession::WIZARD,
        mir2::proto::Gender::FEMALE, 7, 2, 300, 400, 900u);
    std::vector<flatbuffers::Offset<mir2::proto::CharacterInfo>> roles = {role1, role2};
    const auto roles_vec = builder.CreateVector(roles);
    const auto rsp = mir2::proto::CreateRoleListRsp(
        builder, mir2::proto::ErrorCode::ERR_OK, roles_vec);
    builder.Finish(rsp);

    handler.HandleCharacterListResponse(MakePacket(MsgId::kRoleListRsp, BuildPayload(builder)));

    ASSERT_EQ(captured_characters.size(), 2u);
    EXPECT_EQ(captured_characters[0].id, 10u);
    EXPECT_EQ(captured_characters[0].account_id, "123");
    EXPECT_EQ(captured_characters[0].name, "Hero");
    EXPECT_EQ(captured_characters[0].char_class, CharacterClass::WARRIOR);
    EXPECT_EQ(captured_characters[0].gender, Gender::MALE);
    EXPECT_EQ(captured_characters[0].stats.level, 5);
    EXPECT_EQ(captured_characters[0].stats.gold, 500);
    EXPECT_EQ(captured_characters[0].map_id, 1u);
    EXPECT_EQ(captured_characters[0].position.x, 100);
    EXPECT_EQ(captured_characters[0].position.y, 200);

    EXPECT_EQ(captured_characters[1].id, 20u);
    EXPECT_EQ(captured_characters[1].account_id, "123");
    EXPECT_EQ(captured_characters[1].name, "Mage");
    EXPECT_EQ(captured_characters[1].char_class, CharacterClass::MAGE);
    EXPECT_EQ(captured_characters[1].gender, Gender::FEMALE);
    EXPECT_EQ(captured_characters[1].stats.level, 7);
    EXPECT_EQ(captured_characters[1].stats.gold, 900);
    EXPECT_EQ(captured_characters[1].map_id, 2u);
    EXPECT_EQ(captured_characters[1].position.x, 300);
    EXPECT_EQ(captured_characters[1].position.y, 400);
}

TEST(CharacterHandlerTest, RoleListErrorReportsFailure) {
    std::string captured_error;

    CharacterHandler::Callbacks callbacks;
    callbacks.on_character_list_failed = [&](const std::string& error) {
        captured_error = error;
    };

    CharacterHandler handler(callbacks);

    flatbuffers::FlatBufferBuilder builder;
    const auto rsp = mir2::proto::CreateRoleListRsp(
        builder, mir2::proto::ErrorCode::ERR_ACCOUNT_NOT_FOUND, 0);
    builder.Finish(rsp);

    handler.HandleCharacterListResponse(MakePacket(MsgId::kRoleListRsp, BuildPayload(builder)));

    EXPECT_EQ(captured_error, "Account not found");
}

TEST(CharacterHandlerTest, RoleListInvalidPayloadIsIgnored) {
    int list_calls = 0;
    int failure_calls = 0;

    CharacterHandler::Callbacks callbacks;
    callbacks.on_character_list = [&](std::vector<CharacterData>) { ++list_calls; };
    callbacks.on_character_list_failed = [&](const std::string&) { ++failure_calls; };

    CharacterHandler handler(callbacks);

    std::vector<uint8_t> bad_payload = {0x01, 0x02};
    handler.HandleCharacterListResponse(MakePacket(MsgId::kRoleListRsp, std::move(bad_payload)));

    EXPECT_EQ(list_calls, 0);
    EXPECT_EQ(failure_calls, 0);
}

TEST(CharacterHandlerTest, CreateRoleSuccessRequestsRoleList) {
    uint64_t created_player_id = 0;
    int request_calls = 0;

    CharacterHandler::Callbacks callbacks;
    callbacks.on_character_created = [&](uint64_t player_id) { created_player_id = player_id; };
    callbacks.request_character_list = [&]() { ++request_calls; };

    CharacterHandler handler(callbacks);

    mir2::common::CreateCharacterResponse response;
    response.code = mir2::proto::ErrorCode::ERR_OK;
    response.player_id = 77u;
    mir2::common::MessageCodecStatus status = mir2::common::MessageCodecStatus::kOk;
    auto payload = mir2::common::EncodeCreateCharacterResponse(response, &status);
    ASSERT_EQ(status, mir2::common::MessageCodecStatus::kOk);

    handler.HandleCharacterCreateResponse(MakePacket(MsgId::kCreateRoleRsp, std::move(payload)));

    EXPECT_EQ(created_player_id, 77u);
    EXPECT_EQ(request_calls, 1);
}

TEST(CharacterHandlerTest, CreateRoleFailureReportsError) {
    std::string captured_error;

    CharacterHandler::Callbacks callbacks;
    callbacks.on_character_create_failed = [&](const std::string& error) { captured_error = error; };

    CharacterHandler handler(callbacks);

    mir2::common::CreateCharacterResponse response;
    response.code = mir2::proto::ErrorCode::ERR_NAME_EXISTS;
    response.player_id = 0u;
    mir2::common::MessageCodecStatus status = mir2::common::MessageCodecStatus::kOk;
    auto payload = mir2::common::EncodeCreateCharacterResponse(response, &status);
    ASSERT_EQ(status, mir2::common::MessageCodecStatus::kOk);

    handler.HandleCharacterCreateResponse(MakePacket(MsgId::kCreateRoleRsp, std::move(payload)));

    EXPECT_EQ(captured_error, "Name already exists");
}

TEST(CharacterHandlerTest, SelectRoleSuccessInvokesCallback) {
    int success_calls = 0;

    CharacterHandler::Callbacks callbacks;
    callbacks.on_select_role_success = [&]() { ++success_calls; };

    CharacterHandler handler(callbacks);

    flatbuffers::FlatBufferBuilder builder;
    const auto rsp = mir2::proto::CreateSelectRoleRsp(
        builder, mir2::proto::ErrorCode::ERR_OK, 99u);
    builder.Finish(rsp);

    handler.HandleCharacterSelectResponse(MakePacket(MsgId::kSelectRoleRsp, BuildPayload(builder)));

    EXPECT_EQ(success_calls, 1);
}

TEST(CharacterHandlerTest, SelectRoleFailureReportsError) {
    std::string captured_error;

    CharacterHandler::Callbacks callbacks;
    callbacks.on_select_role_failed = [&](const std::string& error) { captured_error = error; };

    CharacterHandler handler(callbacks);

    flatbuffers::FlatBufferBuilder builder;
    const auto rsp = mir2::proto::CreateSelectRoleRsp(
        builder, mir2::proto::ErrorCode::ERR_ACCOUNT_NOT_FOUND, 0u);
    builder.Finish(rsp);

    handler.HandleCharacterSelectResponse(MakePacket(MsgId::kSelectRoleRsp, BuildPayload(builder)));

    EXPECT_EQ(captured_error, "Account not found");
}

TEST(CharacterHandlerTest, EnterGameSuccessBuildsCharacterData) {
    CharacterData captured_data;
    int success_calls = 0;

    std::vector<CharacterData> existing_list;
    CharacterData existing;
    existing.id = 42u;
    existing.gender = Gender::FEMALE;
    existing_list.push_back(existing);

    CharacterHandler::Callbacks callbacks;
    callbacks.get_account_id = []() { return 99u; };
    callbacks.get_character_list = [&]() -> const std::vector<CharacterData>& { return existing_list; };
    callbacks.on_enter_game_success = [&](const CharacterData& data) {
        captured_data = data;
        ++success_calls;
    };

    CharacterHandler handler(callbacks);

    flatbuffers::FlatBufferBuilder builder;
    const auto name = builder.CreateString("PlayerOne");
    const auto player = mir2::proto::CreatePlayerInfo(
        builder, 42u, name, mir2::proto::Profession::TAOIST, 15,
        120, 200, 50, 80, 3u, 11, 22, 900u);
    const auto rsp = mir2::proto::CreateEnterGameRsp(
        builder, mir2::proto::ErrorCode::ERR_OK, player);
    builder.Finish(rsp);

    handler.HandleEnterGameResponse(MakePacket(MsgId::kEnterGameRsp, BuildPayload(builder)));

    EXPECT_EQ(success_calls, 1);
    EXPECT_EQ(captured_data.id, 42u);
    EXPECT_EQ(captured_data.account_id, "99");
    EXPECT_EQ(captured_data.name, "PlayerOne");
    EXPECT_EQ(captured_data.char_class, CharacterClass::TAOIST);
    EXPECT_EQ(captured_data.gender, Gender::FEMALE);
    EXPECT_EQ(captured_data.stats.level, 15);
    EXPECT_EQ(captured_data.stats.hp, 120);
    EXPECT_EQ(captured_data.stats.max_hp, 200);
    EXPECT_EQ(captured_data.stats.mp, 50);
    EXPECT_EQ(captured_data.stats.max_mp, 80);
    EXPECT_EQ(captured_data.stats.gold, 900);
    EXPECT_EQ(captured_data.map_id, 3u);
    EXPECT_EQ(captured_data.position.x, 11);
    EXPECT_EQ(captured_data.position.y, 22);
}

TEST(CharacterHandlerTest, EnterGameFailureReportsError) {
    std::string captured_error;

    CharacterHandler::Callbacks callbacks;
    callbacks.on_enter_game_failed = [&](const std::string& error) { captured_error = error; };

    CharacterHandler handler(callbacks);

    flatbuffers::FlatBufferBuilder builder;
    const auto rsp = mir2::proto::CreateEnterGameRsp(
        builder, mir2::proto::ErrorCode::ERR_INVALID_ACTION, 0);
    builder.Finish(rsp);

    handler.HandleEnterGameResponse(MakePacket(MsgId::kEnterGameRsp, BuildPayload(builder)));

    EXPECT_EQ(captured_error, "Invalid action");
}
