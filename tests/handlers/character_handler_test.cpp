#include <gtest/gtest.h>

#include <entt/entt.hpp>

#include <flatbuffers/flatbuffers.h>

#include "server/common/error_codes.h"
#include "common/enums.h"
#include "common/protocol/message_codec.h"
#include "ecs/character_entity_manager.h"
#include "handlers/character/character_handler.h"
#include "login_generated.h"

namespace {

std::vector<uint8_t> BuildRoleListReq(uint64_t account_id) {
    flatbuffers::FlatBufferBuilder builder;
    const auto req = mir2::proto::CreateRoleListReq(builder, account_id, 0);
    builder.Finish(req);
    const uint8_t* data = builder.GetBufferPointer();
    return std::vector<uint8_t>(data, data + builder.GetSize());
}

std::vector<uint8_t> BuildCreateRoleReq(const std::string& name) {
    mir2::common::CreateCharacterRequest request;
    request.name = name;
    request.profession = mir2::proto::Profession::WARRIOR;
    request.gender = mir2::proto::Gender::MALE;
    mir2::common::MessageCodecStatus status = mir2::common::MessageCodecStatus::kOk;
    auto payload = mir2::common::EncodeCreateCharacterRequest(request, &status);
    if (status != mir2::common::MessageCodecStatus::kOk) {
        return {};
    }
    return payload;
}

std::vector<uint8_t> BuildRawCreateRoleReq(const std::string& name) {
    flatbuffers::FlatBufferBuilder builder;
    const auto name_offset = builder.CreateString(name);
    const auto req = mir2::proto::CreateCreateRoleReq(
        builder, name_offset, mir2::proto::Profession::WARRIOR, mir2::proto::Gender::MALE);
    builder.Finish(req);
    const uint8_t* data = builder.GetBufferPointer();
    return std::vector<uint8_t>(data, data + builder.GetSize());
}

std::vector<uint8_t> BuildSelectRoleReq(uint64_t player_id) {
    flatbuffers::FlatBufferBuilder builder;
    const auto req = mir2::proto::CreateSelectRoleReq(builder, player_id);
    builder.Finish(req);
    const uint8_t* data = builder.GetBufferPointer();
    return std::vector<uint8_t>(data, data + builder.GetSize());
}

}  // namespace

TEST(CharacterHandlerTest, RoleListReturnsRoles) {
    mir2::world::RoleStore store;
    mir2::world::RoleRecord record;
    ASSERT_EQ(store.CreateRole(1, "Alice", 1, 0, &record),
              mir2::common::ErrorCode::kOk);
    ASSERT_EQ(store.CreateRole(1, "Bob", 1, 0, &record),
              mir2::common::ErrorCode::kOk);

    entt::registry registry;
    mir2::ecs::CharacterEntityManager character_manager(registry);
    legend2::handlers::CharacterHandler handler(character_manager, store);
    legend2::handlers::HandlerContext context;
    context.client_id = 10;

    const auto payload = BuildRoleListReq(1);
    legend2::handlers::ResponseList responses;
    handler.Handle(context,
                   static_cast<uint16_t>(mir2::common::MsgId::kRoleListReq),
                   payload,
                   [&responses](const legend2::handlers::ResponseList& rsp) { responses = rsp; });

    ASSERT_EQ(responses.size(), 1u);
    EXPECT_EQ(responses[0].msg_id,
              static_cast<uint16_t>(mir2::common::MsgId::kRoleListRsp));

    flatbuffers::Verifier verifier(responses[0].payload.data(), responses[0].payload.size());
    ASSERT_TRUE(verifier.VerifyBuffer<mir2::proto::RoleListRsp>(nullptr));
    const auto* rsp = flatbuffers::GetRoot<mir2::proto::RoleListRsp>(responses[0].payload.data());
    ASSERT_NE(rsp, nullptr);
    EXPECT_EQ(static_cast<uint16_t>(rsp->code()),
              static_cast<uint16_t>(mir2::common::ErrorCode::kOk));
    ASSERT_NE(rsp->roles(), nullptr);
    EXPECT_EQ(rsp->roles()->size(), 2u);
}

TEST(CharacterHandlerTest, CreateRoleDuplicateNameReturnsError) {
    mir2::world::RoleStore store;
    mir2::world::RoleRecord record;
    ASSERT_EQ(store.CreateRole(1, "Alice", 1, 0, &record),
              mir2::common::ErrorCode::kOk);
    store.BindClientAccount(10, 1);

    entt::registry registry;
    mir2::ecs::CharacterEntityManager character_manager(registry);
    legend2::handlers::CharacterHandler handler(character_manager, store);
    legend2::handlers::HandlerContext context;
    context.client_id = 10;

    const auto payload = BuildCreateRoleReq("Alice");
    legend2::handlers::ResponseList responses;
    handler.Handle(context,
                   static_cast<uint16_t>(mir2::common::MsgId::kCreateRoleReq),
                   payload,
                   [&responses](const legend2::handlers::ResponseList& rsp) { responses = rsp; });

    ASSERT_EQ(responses.size(), 1u);
    mir2::common::CreateCharacterResponse response;
    const auto status = mir2::common::DecodeCreateCharacterResponse(
        mir2::common::kCreateCharacterResponseMsgId, responses[0].payload, &response);
    ASSERT_EQ(status, mir2::common::MessageCodecStatus::kOk);
    EXPECT_EQ(static_cast<uint16_t>(response.code),
              static_cast<uint16_t>(mir2::common::ErrorCode::kNameExists));
}

TEST(CharacterHandlerTest, CreateRoleShortNameReturnsError) {
    mir2::world::RoleStore store;
    store.BindClientAccount(10, 1);

    entt::registry registry;
    mir2::ecs::CharacterEntityManager character_manager(registry);
    legend2::handlers::CharacterHandler handler(character_manager, store);
    legend2::handlers::HandlerContext context;
    context.client_id = 10;

    const auto payload = BuildRawCreateRoleReq("A");
    legend2::handlers::ResponseList responses;
    handler.Handle(context,
                   static_cast<uint16_t>(mir2::common::MsgId::kCreateRoleReq),
                   payload,
                   [&responses](const legend2::handlers::ResponseList& rsp) { responses = rsp; });

    ASSERT_EQ(responses.size(), 1u);
    mir2::common::CreateCharacterResponse response;
    const auto status = mir2::common::DecodeCreateCharacterResponse(
        mir2::common::kCreateCharacterResponseMsgId, responses[0].payload, &response);
    ASSERT_EQ(status, mir2::common::MessageCodecStatus::kOk);
    EXPECT_EQ(static_cast<uint16_t>(response.code),
              static_cast<uint16_t>(mir2::common::ErrorCode::kInvalidAction));
}

TEST(CharacterHandlerTest, SelectRoleMissingAccountReturnsErrorAndEnterGame) {
    mir2::world::RoleStore store;
    entt::registry registry;
    mir2::ecs::CharacterEntityManager character_manager(registry);
    legend2::handlers::CharacterHandler handler(character_manager, store);
    legend2::handlers::HandlerContext context;
    context.client_id = 10;

    const auto payload = BuildSelectRoleReq(99);
    legend2::handlers::ResponseList responses;
    handler.Handle(context,
                   static_cast<uint16_t>(mir2::common::MsgId::kSelectRoleReq),
                   payload,
                   [&responses](const legend2::handlers::ResponseList& rsp) { responses = rsp; });

    ASSERT_EQ(responses.size(), 2u);
    EXPECT_EQ(responses[0].msg_id,
              static_cast<uint16_t>(mir2::common::MsgId::kSelectRoleRsp));
    EXPECT_EQ(responses[1].msg_id,
              static_cast<uint16_t>(mir2::common::MsgId::kEnterGameRsp));

    flatbuffers::Verifier verifier(responses[0].payload.data(), responses[0].payload.size());
    ASSERT_TRUE(verifier.VerifyBuffer<mir2::proto::SelectRoleRsp>(nullptr));
    const auto* rsp = flatbuffers::GetRoot<mir2::proto::SelectRoleRsp>(responses[0].payload.data());
    ASSERT_NE(rsp, nullptr);
    EXPECT_EQ(static_cast<uint16_t>(rsp->code()),
              static_cast<uint16_t>(mir2::common::ErrorCode::kAccountNotFound));
}
