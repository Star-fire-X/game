#include <gtest/gtest.h>

#include <flatbuffers/flatbuffers.h>

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "client/handlers/character_handler.h"
#include "client/handlers/combat_handler.h"
#include "client/handlers/handler_registry.h"
#include "client/handlers/login_handler.h"
#include "client/handlers/movement_handler.h"
#include "client/handlers/system_handler.h"
#include "client/network/network_manager.h"
#include "combat_generated.h"
#include "common/enums.h"
#include "common/protocol/message_codec.h"
#include "game_generated.h"
#include "login_generated.h"
#include "system_generated.h"

namespace {

using legend2::ConnectionState;
using mir2::common::ErrorCode;
using legend2::INetworkClient;
using legend2::NetworkManager;
using mir2::common::MsgId;
using mir2::common::NetworkPacket;
using mir2::game::handlers::CharacterHandler;
using mir2::game::handlers::CombatHandler;
using mir2::game::handlers::HandlerRegistry;
using mir2::game::handlers::LoginHandler;
using mir2::game::handlers::MovementHandler;
using mir2::game::handlers::SystemHandler;

class FakeNetworkClient : public INetworkClient {
public:
    bool connect(const std::string& host, uint16_t port) override {
        host_ = host;
        port_ = port;
        connected_ = true;
        if (on_connect_) {
            on_connect_();
        }
        return true;
    }

    void disconnect() override {
        connected_ = false;
        if (on_disconnect_) {
            on_disconnect_();
        }
    }

    bool is_connected() const override {
        return connected_;
    }

    void send(uint16_t msg_id, const std::vector<uint8_t>& payload) override {
        sent_messages_.push_back({msg_id, payload});
    }

    std::optional<NetworkPacket> receive() override {
        return std::nullopt;
    }

    void update() override {
    }

    ConnectionState get_state() const override {
        return connected_ ? ConnectionState::CONNECTED : ConnectionState::DISCONNECTED;
    }

    ErrorCode get_last_error() const override {
        return ErrorCode::SUCCESS;
    }

    void set_on_message(MessageCallback callback) override {
        on_message_ = std::move(callback);
    }

    void set_on_disconnect(EventCallback callback) override {
        on_disconnect_ = std::move(callback);
    }

    void set_on_connect(EventCallback callback) override {
        on_connect_ = std::move(callback);
    }

    void EmitPacket(const NetworkPacket& packet) {
        if (on_message_) {
            on_message_(packet);
        }
    }

private:
    struct SentMessage {
        uint16_t msg_id = 0;
        std::vector<uint8_t> payload;
    };

    bool connected_ = false;
    std::string host_;
    uint16_t port_ = 0;
    std::vector<SentMessage> sent_messages_;
    MessageCallback on_message_;
    EventCallback on_disconnect_;
    EventCallback on_connect_;
};

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

TEST(HandlerRegistry, RegistersHandlersAndDispatchesCoreMessages) {
    auto fake = std::make_unique<FakeNetworkClient>();
    FakeNetworkClient* fake_ptr = fake.get();
    NetworkManager manager(std::move(fake));

    int login_calls = 0;
    int role_list_calls = 0;
    int move_calls = 0;
    int attack_calls = 0;
    int notice_calls = 0;

    LoginHandler::Callbacks login_callbacks;
    login_callbacks.on_login_success = [&](uint64_t, const std::string&) { ++login_calls; };
    login_callbacks.request_character_list = []() {};

    CharacterHandler::Callbacks character_callbacks;
    character_callbacks.get_account_id = []() { return 7u; };
    character_callbacks.on_character_list = [&](std::vector<mir2::common::CharacterData>) {
        ++role_list_calls;
    };

    MovementHandler::Callbacks movement_callbacks;
    movement_callbacks.on_move_response = [&](int, int) { ++move_calls; };

    CombatHandler::Callbacks combat_callbacks;
    combat_callbacks.on_attack_response = [&](mir2::proto::ErrorCode,
                                              uint64_t,
                                              uint64_t,
                                              int,
                                              int,
                                              bool) {
        ++attack_calls;
    };

    SystemHandler::Callbacks system_callbacks;
    system_callbacks.on_server_notice = [&](uint16_t, const std::string&, uint32_t) {
        ++notice_calls;
    };

    [[maybe_unused]] LoginHandler login_handler(login_callbacks);
    [[maybe_unused]] CharacterHandler character_handler(character_callbacks);
    [[maybe_unused]] MovementHandler movement_handler(movement_callbacks);
    [[maybe_unused]] CombatHandler combat_handler(combat_callbacks);
    [[maybe_unused]] SystemHandler system_handler(system_callbacks);

    HandlerRegistry::RegisterHandlers(manager);

    mir2::common::LoginResponse login_response;
    login_response.code = mir2::proto::ErrorCode::ERR_OK;
    login_response.account_id = 42u;
    login_response.session_token = "token";
    mir2::common::MessageCodecStatus login_status = mir2::common::MessageCodecStatus::kOk;
    auto login_payload = mir2::common::EncodeLoginResponse(login_response, &login_status);
    ASSERT_EQ(login_status, mir2::common::MessageCodecStatus::kOk);
    fake_ptr->EmitPacket(MakePacket(MsgId::kLoginRsp, std::move(login_payload)));

    flatbuffers::FlatBufferBuilder list_builder;
    const auto name = list_builder.CreateString("Hero");
    const auto role = mir2::proto::CreateCharacterInfo(
        list_builder, 1u, name, mir2::proto::Profession::WARRIOR,
        mir2::proto::Gender::MALE, 1, 0, 10, 10, 0u);
    std::vector<flatbuffers::Offset<mir2::proto::CharacterInfo>> roles = {role};
    const auto roles_vec = list_builder.CreateVector(roles);
    const auto list_rsp = mir2::proto::CreateRoleListRsp(
        list_builder, mir2::proto::ErrorCode::ERR_OK, roles_vec);
    list_builder.Finish(list_rsp);
    fake_ptr->EmitPacket(MakePacket(MsgId::kRoleListRsp, BuildPayload(list_builder)));

    mir2::common::MoveResponse move_response;
    move_response.code = mir2::proto::ErrorCode::ERR_OK;
    move_response.x = 5;
    move_response.y = 6;
    mir2::common::MessageCodecStatus move_status = mir2::common::MessageCodecStatus::kOk;
    auto move_payload = mir2::common::EncodeMoveResponse(move_response, &move_status);
    ASSERT_EQ(move_status, mir2::common::MessageCodecStatus::kOk);
    fake_ptr->EmitPacket(MakePacket(MsgId::kMoveRsp, std::move(move_payload)));

    mir2::common::AttackResponse attack_response;
    attack_response.code = mir2::proto::ErrorCode::ERR_OK;
    attack_response.attacker_id = 11u;
    attack_response.target_id = 22u;
    attack_response.damage = 3;
    attack_response.target_hp = 9;
    attack_response.target_dead = false;
    mir2::common::MessageCodecStatus attack_status = mir2::common::MessageCodecStatus::kOk;
    auto attack_payload = mir2::common::EncodeAttackResponse(attack_response, &attack_status);
    ASSERT_EQ(attack_status, mir2::common::MessageCodecStatus::kOk);
    fake_ptr->EmitPacket(MakePacket(MsgId::kAttackRsp, std::move(attack_payload)));

    flatbuffers::FlatBufferBuilder notice_builder;
    const auto message = notice_builder.CreateString("Maintenance");
    const auto notice = mir2::proto::CreateServerNotice(notice_builder, 1, message, 101u);
    notice_builder.Finish(notice);
    fake_ptr->EmitPacket(MakePacket(MsgId::kServerNotice, BuildPayload(notice_builder)));

    EXPECT_EQ(login_calls, 1);
    EXPECT_EQ(role_list_calls, 1);
    EXPECT_EQ(move_calls, 1);
    EXPECT_EQ(attack_calls, 1);
    EXPECT_EQ(notice_calls, 1);
}

TEST(GameClientMessageFlow, EndToEndLoginToCombatDispatchesInOrder) {
    auto fake = std::make_unique<FakeNetworkClient>();
    FakeNetworkClient* fake_ptr = fake.get();
    NetworkManager manager(std::move(fake));

    std::vector<std::string> events;
    std::vector<mir2::common::CharacterData> character_list;
    mir2::common::CharacterData existing;
    existing.id = 55u;
    existing.gender = mir2::common::Gender::FEMALE;
    character_list.push_back(existing);

    LoginHandler::Callbacks login_callbacks;
    login_callbacks.on_login_success = [&](uint64_t, const std::string&) {
        events.push_back("login_success");
    };
    login_callbacks.request_character_list = [&]() { events.push_back("request_role_list"); };

    CharacterHandler::Callbacks character_callbacks;
    character_callbacks.get_account_id = []() { return 99u; };
    character_callbacks.get_character_list = [&]() -> const std::vector<mir2::common::CharacterData>& {
        return character_list;
    };
    character_callbacks.on_character_list = [&](std::vector<mir2::common::CharacterData>) {
        events.push_back("role_list");
    };
    character_callbacks.on_select_role_success = [&]() { events.push_back("select_ok"); };
    character_callbacks.on_enter_game_success = [&](const mir2::common::CharacterData&) {
        events.push_back("enter_game");
    };

    MovementHandler::Callbacks movement_callbacks;
    movement_callbacks.on_move_response = [&](int, int) { events.push_back("move"); };

    CombatHandler::Callbacks combat_callbacks;
    combat_callbacks.on_attack_response = [&](mir2::proto::ErrorCode,
                                              uint64_t,
                                              uint64_t,
                                              int,
                                              int,
                                              bool) {
        events.push_back("attack");
    };

    SystemHandler::Callbacks system_callbacks;
    system_callbacks.on_server_notice = [&](uint16_t, const std::string&, uint32_t) {
        events.push_back("notice");
    };

    [[maybe_unused]] LoginHandler login_handler(login_callbacks);
    [[maybe_unused]] CharacterHandler character_handler(character_callbacks);
    [[maybe_unused]] MovementHandler movement_handler(movement_callbacks);
    [[maybe_unused]] CombatHandler combat_handler(combat_callbacks);
    [[maybe_unused]] SystemHandler system_handler(system_callbacks);

    HandlerRegistry::RegisterHandlers(manager);

    mir2::common::LoginResponse login_response;
    login_response.code = mir2::proto::ErrorCode::ERR_OK;
    login_response.account_id = 42u;
    login_response.session_token = "token";
    mir2::common::MessageCodecStatus login_status = mir2::common::MessageCodecStatus::kOk;
    auto login_payload = mir2::common::EncodeLoginResponse(login_response, &login_status);
    ASSERT_EQ(login_status, mir2::common::MessageCodecStatus::kOk);
    fake_ptr->EmitPacket(MakePacket(MsgId::kLoginRsp, std::move(login_payload)));

    flatbuffers::FlatBufferBuilder list_builder;
    const auto name = list_builder.CreateString("Hero");
    const auto role = mir2::proto::CreateCharacterInfo(
        list_builder, 55u, name, mir2::proto::Profession::WARRIOR,
        mir2::proto::Gender::FEMALE, 10, 1, 100, 100, 50u);
    std::vector<flatbuffers::Offset<mir2::proto::CharacterInfo>> roles = {role};
    const auto roles_vec = list_builder.CreateVector(roles);
    const auto list_rsp = mir2::proto::CreateRoleListRsp(
        list_builder, mir2::proto::ErrorCode::ERR_OK, roles_vec);
    list_builder.Finish(list_rsp);
    fake_ptr->EmitPacket(MakePacket(MsgId::kRoleListRsp, BuildPayload(list_builder)));

    flatbuffers::FlatBufferBuilder select_builder;
    const auto select_rsp = mir2::proto::CreateSelectRoleRsp(
        select_builder, mir2::proto::ErrorCode::ERR_OK, 55u);
    select_builder.Finish(select_rsp);
    fake_ptr->EmitPacket(MakePacket(MsgId::kSelectRoleRsp, BuildPayload(select_builder)));

    flatbuffers::FlatBufferBuilder enter_builder;
    const auto player_name = enter_builder.CreateString("Hero");
    const auto player = mir2::proto::CreatePlayerInfo(
        enter_builder, 55u, player_name, mir2::proto::Profession::WARRIOR,
        12, 120, 200, 40, 80, 3u, 11, 22, 500u);
    const auto enter_rsp = mir2::proto::CreateEnterGameRsp(
        enter_builder, mir2::proto::ErrorCode::ERR_OK, player);
    enter_builder.Finish(enter_rsp);
    fake_ptr->EmitPacket(MakePacket(MsgId::kEnterGameRsp, BuildPayload(enter_builder)));

    mir2::common::MoveResponse move_response;
    move_response.code = mir2::proto::ErrorCode::ERR_OK;
    move_response.x = 7;
    move_response.y = 8;
    mir2::common::MessageCodecStatus move_status = mir2::common::MessageCodecStatus::kOk;
    auto move_payload = mir2::common::EncodeMoveResponse(move_response, &move_status);
    ASSERT_EQ(move_status, mir2::common::MessageCodecStatus::kOk);
    fake_ptr->EmitPacket(MakePacket(MsgId::kMoveRsp, std::move(move_payload)));

    mir2::common::AttackResponse attack_response;
    attack_response.code = mir2::proto::ErrorCode::ERR_OK;
    attack_response.attacker_id = 77u;
    attack_response.target_id = 88u;
    attack_response.damage = 9;
    attack_response.target_hp = 31;
    attack_response.target_dead = false;
    mir2::common::MessageCodecStatus attack_status = mir2::common::MessageCodecStatus::kOk;
    auto attack_payload = mir2::common::EncodeAttackResponse(attack_response, &attack_status);
    ASSERT_EQ(attack_status, mir2::common::MessageCodecStatus::kOk);
    fake_ptr->EmitPacket(MakePacket(MsgId::kAttackRsp, std::move(attack_payload)));

    flatbuffers::FlatBufferBuilder notice_builder;
    const auto message = notice_builder.CreateString("Welcome");
    const auto notice = mir2::proto::CreateServerNotice(notice_builder, 1, message, 123u);
    notice_builder.Finish(notice);
    fake_ptr->EmitPacket(MakePacket(MsgId::kServerNotice, BuildPayload(notice_builder)));

    const std::vector<std::string> expected = {
        "login_success",
        "request_role_list",
        "role_list",
        "select_ok",
        "enter_game",
        "move",
        "attack",
        "notice"
    };

    EXPECT_EQ(events, expected);
}

TEST(HandlerRegistry, UnregisteredMessagesUseDefaultHandlerAndReportErrors) {
    auto fake = std::make_unique<FakeNetworkClient>();
    FakeNetworkClient* fake_ptr = fake.get();
    NetworkManager manager(std::move(fake));

    int parse_errors = 0;
    int default_calls = 0;

    MovementHandler::Callbacks movement_callbacks;
    movement_callbacks.on_parse_error = [&](const std::string&) { ++parse_errors; };
    [[maybe_unused]] MovementHandler movement_handler(movement_callbacks);

    LoginHandler::Callbacks login_callbacks;
    [[maybe_unused]] LoginHandler login_handler(login_callbacks);
    CharacterHandler::Callbacks character_callbacks;
    [[maybe_unused]] CharacterHandler character_handler(character_callbacks);
    CombatHandler::Callbacks combat_callbacks;
    [[maybe_unused]] CombatHandler combat_handler(combat_callbacks);
    SystemHandler::Callbacks system_callbacks;
    [[maybe_unused]] SystemHandler system_handler(system_callbacks);

    HandlerRegistry::RegisterHandlers(manager);

    manager.set_default_handler([&](const NetworkPacket& packet) {
        if (packet.msg_id == static_cast<uint16_t>(MsgId::kChatReq)) {
            ++default_calls;
        }
    });

    std::vector<uint8_t> bad_payload = {0x1, 0x2, 0x3};
    fake_ptr->EmitPacket(MakePacket(MsgId::kMoveRsp, std::move(bad_payload)));
    fake_ptr->EmitPacket(MakePacket(MsgId::kChatReq, {}));

    EXPECT_EQ(parse_errors, 1);
    EXPECT_EQ(default_calls, 1);
}
