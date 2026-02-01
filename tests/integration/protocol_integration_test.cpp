#include <gtest/gtest.h>

#include <entt/entt.hpp>

#include <algorithm>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "client/handlers/character_handler.h"
#include "client/handlers/combat_handler.h"
#include "client/handlers/login_handler.h"
#include "client/handlers/movement_handler.h"
#include "server/common/error_codes.h"
#include "common/enums.h"
#include "common/protocol/message_codec.h"
#include "common/protocol/packet_codec.h"
#include "ecs/character_entity_manager.h"
#include "ecs/components/character_components.h"
#include "handlers/character/character_handler.h"
#include "handlers/client_registry.h"
#include "handlers/combat/combat_handler.h"
#include "handlers/login/login_handler.h"
#include "handlers/movement/movement_handler.h"
#include "game/map/scene_manager.h"
#include "world/role_store.h"

namespace {

using mir2::common::DecodeStatus;
using mir2::common::NetworkPacket;
using mir2::common::PacketHeader;

struct PacketInspection {
    bool header_ok = false;
    PacketHeader header{};
    DecodeStatus decode_status = DecodeStatus::kTruncated;
    NetworkPacket packet{};
};

PacketInspection InspectPacket(const std::vector<uint8_t>& bytes) {
    PacketInspection inspection;
    inspection.header_ok = PacketHeader::FromBytes(bytes.data(), bytes.size(), &inspection.header);
    inspection.decode_status = mir2::common::DecodePacket(bytes.data(), bytes.size(), &inspection.packet);
    return inspection;
}

void ExpectPacketMatches(uint16_t msg_id,
                         const std::vector<uint8_t>& payload,
                         const std::vector<uint8_t>& bytes) {
    ASSERT_FALSE(bytes.empty());
    const auto inspection = InspectPacket(bytes);
    EXPECT_TRUE(inspection.header_ok);
    EXPECT_EQ(inspection.header.magic, PacketHeader::kMagic);
    EXPECT_EQ(inspection.header.msg_id, msg_id);
    EXPECT_EQ(inspection.header.payload_size, payload.size());
    EXPECT_EQ(inspection.decode_status, DecodeStatus::kOk);
    EXPECT_EQ(inspection.packet.msg_id, msg_id);
    EXPECT_EQ(inspection.packet.payload, payload);
}

std::vector<uint8_t> EncodePacketBytes(uint16_t msg_id, const std::vector<uint8_t>& payload) {
    const uint8_t* data = payload.empty() ? nullptr : payload.data();
    return mir2::common::EncodePacket(msg_id, data, payload.size());
}

mir2::game::map::SceneManager::MapConfig BuildMapConfig(int map_id,
                                                        int width,
                                                        int height) {
    mir2::game::map::SceneManager::MapConfig config;
    config.map_id = map_id;
    config.width = width;
    config.height = height;
    config.grid_size = 10;
    config.load_walkability = false;
    return config;
}

legend2::handlers::ResponseList HandleServerRequest(legend2::handlers::IMessageHandler& handler,
                                                    uint64_t client_id,
                                                    uint16_t msg_id,
                                                    const std::vector<uint8_t>& payload) {
    legend2::handlers::HandlerContext context;
    context.client_id = client_id;
    context.post = [](std::function<void()> fn) {
        if (fn) {
            fn();
        }
    };

    legend2::handlers::ResponseList responses;
    handler.Handle(context, msg_id, payload,
                   [&responses](const legend2::handlers::ResponseList& rsp) { responses = rsp; });
    return responses;
}

class StubLoginService : public legend2::handlers::LoginService {
public:
    legend2::handlers::LoginResult result;
    std::string last_username;
    std::string last_password;
    int calls = 0;

    void Login(const std::string& username,
               const std::string& password,
               legend2::handlers::LoginCallback callback) override {
        last_username = username;
        last_password = password;
        ++calls;
        if (callback) {
            callback(result);
        }
    }
};

class StubCombatService : public legend2::handlers::CombatService {
public:
    legend2::handlers::CombatResult attack_result;
    uint64_t last_attacker_id = 0;
    uint64_t last_target_id = 0;
    int attack_calls = 0;

    legend2::handlers::CombatResult Attack(uint64_t attacker_id,
                                           uint64_t target_id) override {
        last_attacker_id = attacker_id;
        last_target_id = target_id;
        ++attack_calls;
        return attack_result;
    }

    legend2::handlers::CombatResult UseSkill(uint64_t, uint64_t, uint32_t) override {
        return {};
    }
};

TEST(ProtocolIntegrationTest, LoginSuccessEndToEnd) {
    StubLoginService service;
    service.result.code = mir2::common::ErrorCode::kOk;
    service.result.account_id = 42;
    service.result.token = "token_42";

    legend2::handlers::LoginHandler server_handler(service);
    const uint64_t client_id = 7;

    mir2::common::LoginRequest request;
    request.username = "user";
    request.password = "pass";
    request.version = "0.1.0";
    mir2::common::MessageCodecStatus status = mir2::common::MessageCodecStatus::kOk;
    const auto payload = mir2::common::EncodeLoginRequest(request, &status);
    ASSERT_EQ(status, mir2::common::MessageCodecStatus::kOk);
    ASSERT_FALSE(payload.empty());

    const auto request_bytes = EncodePacketBytes(mir2::common::kLoginRequestMsgId, payload);
    ExpectPacketMatches(mir2::common::kLoginRequestMsgId, payload, request_bytes);
    const auto request_packet = InspectPacket(request_bytes).packet;

    const auto responses = HandleServerRequest(server_handler,
                                               client_id,
                                               request_packet.msg_id,
                                               request_packet.payload);
    ASSERT_EQ(service.calls, 1);
    EXPECT_EQ(service.last_username, "user");
    EXPECT_EQ(service.last_password, "pass");
    ASSERT_EQ(responses.size(), 1u);
    EXPECT_EQ(responses[0].msg_id, mir2::common::kLoginResponseMsgId);

    const auto response_bytes = EncodePacketBytes(responses[0].msg_id, responses[0].payload);
    ExpectPacketMatches(responses[0].msg_id, responses[0].payload, response_bytes);
    const auto response_packet = InspectPacket(response_bytes).packet;

    bool login_success_called = false;
    bool request_list_called = false;
    bool login_failure_called = false;
    uint64_t captured_account = 0;
    std::string captured_token;

    mir2::game::handlers::LoginHandler::Callbacks callbacks;
    callbacks.on_login_success = [&](uint64_t account_id, const std::string& token) {
        login_success_called = true;
        captured_account = account_id;
        captured_token = token;
    };
    callbacks.on_login_failure = [&](const std::string&) { login_failure_called = true; };
    callbacks.request_character_list = [&]() { request_list_called = true; };

    mir2::game::handlers::LoginHandler client_handler(std::move(callbacks));
    client_handler.HandleLoginResponse(response_packet);

    EXPECT_TRUE(login_success_called);
    EXPECT_FALSE(login_failure_called);
    EXPECT_TRUE(request_list_called);
    EXPECT_EQ(captured_account, 42u);
    EXPECT_EQ(captured_token, "token_42");
}

TEST(ProtocolIntegrationTest, LoginInvalidPasswordEndToEnd) {
    StubLoginService service;
    service.result.code = mir2::common::ErrorCode::kPasswordWrong;

    legend2::handlers::LoginHandler server_handler(service);
    const uint64_t client_id = 8;

    mir2::common::LoginRequest request;
    request.username = "user";
    request.password = "bad";
    mir2::common::MessageCodecStatus status = mir2::common::MessageCodecStatus::kOk;
    const auto payload = mir2::common::EncodeLoginRequest(request, &status);
    ASSERT_EQ(status, mir2::common::MessageCodecStatus::kOk);

    const auto request_bytes = EncodePacketBytes(mir2::common::kLoginRequestMsgId, payload);
    ExpectPacketMatches(mir2::common::kLoginRequestMsgId, payload, request_bytes);
    const auto request_packet = InspectPacket(request_bytes).packet;

    const auto responses = HandleServerRequest(server_handler,
                                               client_id,
                                               request_packet.msg_id,
                                               request_packet.payload);
    ASSERT_EQ(responses.size(), 1u);
    EXPECT_EQ(responses[0].msg_id, mir2::common::kLoginResponseMsgId);

    const auto response_bytes = EncodePacketBytes(responses[0].msg_id, responses[0].payload);
    ExpectPacketMatches(responses[0].msg_id, responses[0].payload, response_bytes);
    const auto response_packet = InspectPacket(response_bytes).packet;

    bool login_success_called = false;
    bool login_failure_called = false;
    std::string error_message;

    mir2::game::handlers::LoginHandler::Callbacks callbacks;
    callbacks.on_login_success = [&](uint64_t, const std::string&) { login_success_called = true; };
    callbacks.on_login_failure = [&](const std::string& error) {
        login_failure_called = true;
        error_message = error;
    };

    mir2::game::handlers::LoginHandler client_handler(std::move(callbacks));
    client_handler.HandleLoginResponse(response_packet);

    EXPECT_FALSE(login_success_called);
    EXPECT_TRUE(login_failure_called);
    EXPECT_EQ(error_message, "Password incorrect");
}

TEST(ProtocolIntegrationTest, CreateCharacterSuccessEndToEnd) {
    mir2::world::RoleStore role_store;
    entt::registry registry;
    mir2::ecs::CharacterEntityManager entity_manager(registry);
    const uint64_t account_id = 9001;
    const uint64_t client_id = 11;
    role_store.BindClientAccount(client_id, account_id);

    legend2::handlers::CharacterHandler server_handler(entity_manager, role_store);

    mir2::common::CreateCharacterRequest request;
    request.name = "Alice";
    request.profession = mir2::proto::Profession::WARRIOR;
    request.gender = mir2::proto::Gender::FEMALE;
    mir2::common::MessageCodecStatus status = mir2::common::MessageCodecStatus::kOk;
    const auto payload = mir2::common::EncodeCreateCharacterRequest(request, &status);
    ASSERT_EQ(status, mir2::common::MessageCodecStatus::kOk);
    ASSERT_FALSE(payload.empty());

    const auto request_bytes = EncodePacketBytes(mir2::common::kCreateCharacterRequestMsgId, payload);
    ExpectPacketMatches(mir2::common::kCreateCharacterRequestMsgId, payload, request_bytes);
    const auto request_packet = InspectPacket(request_bytes).packet;

    const auto responses = HandleServerRequest(server_handler,
                                               client_id,
                                               request_packet.msg_id,
                                               request_packet.payload);
    ASSERT_EQ(responses.size(), 1u);
    EXPECT_EQ(responses[0].msg_id, mir2::common::kCreateCharacterResponseMsgId);
    EXPECT_TRUE(role_store.RoleNameExists("Alice"));

    const auto response_bytes = EncodePacketBytes(responses[0].msg_id, responses[0].payload);
    ExpectPacketMatches(responses[0].msg_id, responses[0].payload, response_bytes);
    const auto response_packet = InspectPacket(response_bytes).packet;

    bool created_called = false;
    bool create_failed_called = false;
    bool request_list_called = false;
    uint64_t created_player_id = 0;

    mir2::game::handlers::CharacterHandler::Callbacks callbacks;
    callbacks.on_character_created = [&](uint64_t player_id) {
        created_called = true;
        created_player_id = player_id;
    };
    callbacks.on_character_create_failed = [&](const std::string&) { create_failed_called = true; };
    callbacks.request_character_list = [&]() { request_list_called = true; };

    mir2::game::handlers::CharacterHandler client_handler(std::move(callbacks));
    client_handler.HandleCharacterCreateResponse(response_packet);

    EXPECT_TRUE(created_called);
    EXPECT_FALSE(create_failed_called);
    EXPECT_TRUE(request_list_called);
    EXPECT_GT(created_player_id, 0u);
}

TEST(ProtocolIntegrationTest, CreateCharacterDuplicateNameEndToEnd) {
    mir2::world::RoleStore role_store;
    entt::registry registry;
    mir2::ecs::CharacterEntityManager entity_manager(registry);
    const uint64_t account_id = 9002;
    const uint64_t client_id = 12;
    role_store.BindClientAccount(client_id, account_id);
    mir2::world::RoleRecord record;
    ASSERT_EQ(role_store.CreateRole(account_id, "Alice", 1, 0, &record),
              mir2::common::ErrorCode::kOk);

    legend2::handlers::CharacterHandler server_handler(entity_manager, role_store);

    mir2::common::CreateCharacterRequest request;
    request.name = "Alice";
    request.profession = mir2::proto::Profession::WARRIOR;
    request.gender = mir2::proto::Gender::MALE;
    mir2::common::MessageCodecStatus status = mir2::common::MessageCodecStatus::kOk;
    const auto payload = mir2::common::EncodeCreateCharacterRequest(request, &status);
    ASSERT_EQ(status, mir2::common::MessageCodecStatus::kOk);

    const auto request_bytes = EncodePacketBytes(mir2::common::kCreateCharacterRequestMsgId, payload);
    ExpectPacketMatches(mir2::common::kCreateCharacterRequestMsgId, payload, request_bytes);
    const auto request_packet = InspectPacket(request_bytes).packet;

    const auto responses = HandleServerRequest(server_handler,
                                               client_id,
                                               request_packet.msg_id,
                                               request_packet.payload);
    ASSERT_EQ(responses.size(), 1u);
    EXPECT_EQ(responses[0].msg_id, mir2::common::kCreateCharacterResponseMsgId);

    const auto response_bytes = EncodePacketBytes(responses[0].msg_id, responses[0].payload);
    ExpectPacketMatches(responses[0].msg_id, responses[0].payload, response_bytes);
    const auto response_packet = InspectPacket(response_bytes).packet;

    bool created_called = false;
    bool create_failed_called = false;
    std::string error_message;

    mir2::game::handlers::CharacterHandler::Callbacks callbacks;
    callbacks.on_character_created = [&](uint64_t) { created_called = true; };
    callbacks.on_character_create_failed = [&](const std::string& error) {
        create_failed_called = true;
        error_message = error;
    };

    mir2::game::handlers::CharacterHandler client_handler(std::move(callbacks));
    client_handler.HandleCharacterCreateResponse(response_packet);

    EXPECT_FALSE(created_called);
    EXPECT_TRUE(create_failed_called);
    EXPECT_EQ(error_message, "Name already exists");
}

TEST(ProtocolIntegrationTest, MoveSuccessEndToEnd) {
    legend2::handlers::ClientRegistry registry;
    entt::registry ecs_registry;
    mir2::ecs::CharacterEntityManager character_manager(ecs_registry);
    const uint64_t client_id = 21;
    registry.Track(client_id);
    mir2::game::map::SceneManager scene_manager;
    scene_manager.GetOrCreateMap(BuildMapConfig(1, 300, 300));
    legend2::handlers::MovementHandler server_handler(registry,
                                                      character_manager,
                                                      scene_manager,
                                                      ecs_registry);

    character_manager.SetPosition(static_cast<uint32_t>(client_id), 95, 195, 1);
    auto entity = character_manager.TryGet(static_cast<uint32_t>(client_id));
    ASSERT_TRUE(entity.has_value());
    auto& attrs = ecs_registry.get_or_emplace<mir2::ecs::CharacterAttributesComponent>(*entity);
    attrs.speed = 8;

    mir2::common::MoveRequest request;
    request.target_x = 100;
    request.target_y = 200;
    mir2::common::MessageCodecStatus status = mir2::common::MessageCodecStatus::kOk;
    const auto payload = mir2::common::EncodeMoveRequest(request, &status);
    ASSERT_EQ(status, mir2::common::MessageCodecStatus::kOk);

    const auto request_bytes = EncodePacketBytes(mir2::common::kMoveRequestMsgId, payload);
    ExpectPacketMatches(mir2::common::kMoveRequestMsgId, payload, request_bytes);
    const auto request_packet = InspectPacket(request_bytes).packet;

    const auto responses = HandleServerRequest(server_handler,
                                               client_id,
                                               request_packet.msg_id,
                                               request_packet.payload);
    ASSERT_GE(responses.size(), 1u);

    auto move_it = std::find_if(responses.begin(), responses.end(),
                                [](const legend2::handlers::HandlerResponse& response) {
                                    return response.msg_id == mir2::common::kMoveResponseMsgId;
                                });
    ASSERT_NE(move_it, responses.end());

    const auto response_bytes = EncodePacketBytes(move_it->msg_id, move_it->payload);
    ExpectPacketMatches(move_it->msg_id, move_it->payload, response_bytes);
    const auto response_packet = InspectPacket(response_bytes).packet;

    bool move_called = false;
    bool move_failed_called = false;
    int captured_x = 0;
    int captured_y = 0;

    mir2::game::handlers::MovementHandler::Callbacks callbacks;
    callbacks.on_move_response = [&](int x, int y) {
        move_called = true;
        captured_x = x;
        captured_y = y;
    };
    callbacks.on_move_failed = [&](const std::string&) { move_failed_called = true; };
    callbacks.on_parse_error = [&](const std::string&) { move_failed_called = true; };

    mir2::game::handlers::MovementHandler client_handler(std::move(callbacks));
    client_handler.HandleMoveResponse(response_packet);

    EXPECT_TRUE(move_called);
    EXPECT_FALSE(move_failed_called);
    EXPECT_EQ(captured_x, 100);
    EXPECT_EQ(captured_y, 200);

    entity = character_manager.TryGet(static_cast<uint32_t>(client_id));
    ASSERT_TRUE(entity.has_value());
    const auto& state = ecs_registry.get<mir2::ecs::CharacterStateComponent>(*entity);
    EXPECT_EQ(state.position.x, 100);
    EXPECT_EQ(state.position.y, 200);
}

TEST(ProtocolIntegrationTest, MoveOutOfBoundsEndToEnd) {
    legend2::handlers::ClientRegistry registry;
    entt::registry ecs_registry;
    mir2::ecs::CharacterEntityManager character_manager(ecs_registry);
    const uint64_t client_id = 22;
    registry.Track(client_id);
    mir2::game::map::SceneManager scene_manager;
    scene_manager.GetOrCreateMap(BuildMapConfig(1, 300, 300));
    legend2::handlers::MovementHandler server_handler(registry,
                                                      character_manager,
                                                      scene_manager,
                                                      ecs_registry);

    character_manager.SetPosition(static_cast<uint32_t>(client_id), 1, 1, 1);
    auto entity = character_manager.TryGet(static_cast<uint32_t>(client_id));
    ASSERT_TRUE(entity.has_value());
    auto& attrs = ecs_registry.get_or_emplace<mir2::ecs::CharacterAttributesComponent>(*entity);
    attrs.speed = 8;

    mir2::common::MoveRequest request;
    request.target_x = 10001;
    request.target_y = 200;
    mir2::common::MessageCodecStatus status = mir2::common::MessageCodecStatus::kOk;
    const auto payload = mir2::common::EncodeMoveRequest(request, &status);
    ASSERT_EQ(status, mir2::common::MessageCodecStatus::kOk);

    const auto request_bytes = EncodePacketBytes(mir2::common::kMoveRequestMsgId, payload);
    ExpectPacketMatches(mir2::common::kMoveRequestMsgId, payload, request_bytes);
    const auto request_packet = InspectPacket(request_bytes).packet;

    const auto responses = HandleServerRequest(server_handler,
                                               client_id,
                                               request_packet.msg_id,
                                               request_packet.payload);
    ASSERT_EQ(responses.size(), 1u);
    EXPECT_EQ(responses[0].msg_id, mir2::common::kMoveResponseMsgId);

    const auto response_bytes = EncodePacketBytes(responses[0].msg_id, responses[0].payload);
    ExpectPacketMatches(responses[0].msg_id, responses[0].payload, response_bytes);
    const auto response_packet = InspectPacket(response_bytes).packet;

    bool move_called = false;
    bool move_failed_called = false;
    std::string error_message;

    mir2::game::handlers::MovementHandler::Callbacks callbacks;
    callbacks.on_move_response = [&](int, int) { move_called = true; };
    callbacks.on_move_failed = [&](const std::string& error) {
        move_failed_called = true;
        error_message = error;
    };

    mir2::game::handlers::MovementHandler client_handler(std::move(callbacks));
    client_handler.HandleMoveResponse(response_packet);

    EXPECT_FALSE(move_called);
    EXPECT_TRUE(move_failed_called);
    EXPECT_EQ(error_message, "Target out of range");
}

TEST(ProtocolIntegrationTest, AttackSuccessEndToEnd) {
    StubCombatService service;
    service.attack_result.code = mir2::common::ErrorCode::kOk;
    service.attack_result.damage = 12;
    service.attack_result.target_hp = 88;
    service.attack_result.target_dead = false;

    legend2::handlers::CombatHandler server_handler(service);
    const uint64_t client_id = 31;
    const uint64_t target_id = 99;

    mir2::common::AttackRequest request;
    request.target_id = target_id;
    request.target_type = mir2::proto::EntityType::PLAYER;
    mir2::common::MessageCodecStatus status = mir2::common::MessageCodecStatus::kOk;
    const auto payload = mir2::common::EncodeAttackRequest(request, &status);
    ASSERT_EQ(status, mir2::common::MessageCodecStatus::kOk);
    ASSERT_FALSE(payload.empty());

    const auto request_bytes = EncodePacketBytes(mir2::common::kAttackRequestMsgId, payload);
    ExpectPacketMatches(mir2::common::kAttackRequestMsgId, payload, request_bytes);
    const auto request_packet = InspectPacket(request_bytes).packet;

    const auto responses = HandleServerRequest(server_handler,
                                               client_id,
                                               request_packet.msg_id,
                                               request_packet.payload);
    ASSERT_EQ(service.attack_calls, 1);
    EXPECT_EQ(service.last_attacker_id, client_id);
    EXPECT_EQ(service.last_target_id, target_id);
    ASSERT_EQ(responses.size(), 1u);
    EXPECT_EQ(responses[0].msg_id, mir2::common::kAttackResponseMsgId);

    const auto response_bytes = EncodePacketBytes(responses[0].msg_id, responses[0].payload);
    ExpectPacketMatches(responses[0].msg_id, responses[0].payload, response_bytes);
    const auto response_packet = InspectPacket(response_bytes).packet;

    bool attack_called = false;
    mir2::proto::ErrorCode captured_code = mir2::proto::ErrorCode::ERR_UNKNOWN;
    uint64_t captured_attacker_id = 0;
    uint64_t captured_target_id = 0;
    int captured_damage = 0;
    int captured_target_hp = 0;
    bool captured_target_dead = true;

    mir2::game::handlers::CombatHandler::Callbacks callbacks;
    callbacks.on_attack_response = [&](mir2::proto::ErrorCode code,
                                       uint64_t attacker_id,
                                       uint64_t target,
                                       int damage,
                                       int target_hp,
                                       bool target_dead) {
        attack_called = true;
        captured_code = code;
        captured_attacker_id = attacker_id;
        captured_target_id = target;
        captured_damage = damage;
        captured_target_hp = target_hp;
        captured_target_dead = target_dead;
    };

    mir2::game::handlers::CombatHandler client_handler(std::move(callbacks));
    client_handler.HandleAttackResponse(response_packet);

    EXPECT_TRUE(attack_called);
    EXPECT_EQ(captured_code, mir2::proto::ErrorCode::ERR_OK);
    EXPECT_EQ(captured_attacker_id, client_id);
    EXPECT_EQ(captured_target_id, target_id);
    EXPECT_EQ(captured_damage, 12);
    EXPECT_EQ(captured_target_hp, 88);
    EXPECT_FALSE(captured_target_dead);
}

TEST(ProtocolIntegrationTest, AttackTargetNotFoundEndToEnd) {
    StubCombatService service;
    service.attack_result.code = mir2::common::ErrorCode::kTargetNotFound;

    legend2::handlers::CombatHandler server_handler(service);
    const uint64_t client_id = 32;
    const uint64_t target_id = 404;

    mir2::common::AttackRequest request;
    request.target_id = target_id;
    request.target_type = mir2::proto::EntityType::MONSTER;
    mir2::common::MessageCodecStatus status = mir2::common::MessageCodecStatus::kOk;
    const auto payload = mir2::common::EncodeAttackRequest(request, &status);
    ASSERT_EQ(status, mir2::common::MessageCodecStatus::kOk);

    const auto request_bytes = EncodePacketBytes(mir2::common::kAttackRequestMsgId, payload);
    ExpectPacketMatches(mir2::common::kAttackRequestMsgId, payload, request_bytes);
    const auto request_packet = InspectPacket(request_bytes).packet;

    const auto responses = HandleServerRequest(server_handler,
                                               client_id,
                                               request_packet.msg_id,
                                               request_packet.payload);
    ASSERT_EQ(responses.size(), 1u);
    EXPECT_EQ(responses[0].msg_id, mir2::common::kAttackResponseMsgId);

    const auto response_bytes = EncodePacketBytes(responses[0].msg_id, responses[0].payload);
    ExpectPacketMatches(responses[0].msg_id, responses[0].payload, response_bytes);
    const auto response_packet = InspectPacket(response_bytes).packet;

    bool attack_called = false;
    mir2::proto::ErrorCode captured_code = mir2::proto::ErrorCode::ERR_UNKNOWN;

    mir2::game::handlers::CombatHandler::Callbacks callbacks;
    callbacks.on_attack_response = [&](mir2::proto::ErrorCode code,
                                       uint64_t,
                                       uint64_t,
                                       int,
                                       int,
                                       bool) {
        attack_called = true;
        captured_code = code;
    };

    mir2::game::handlers::CombatHandler client_handler(std::move(callbacks));
    client_handler.HandleAttackResponse(response_packet);

    EXPECT_TRUE(attack_called);
    EXPECT_EQ(captured_code, mir2::proto::ErrorCode::ERR_TARGET_NOT_FOUND);
}

}  // namespace
