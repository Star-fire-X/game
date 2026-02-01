#include "handlers/character/character_handler.h"

#include <flatbuffers/flatbuffers.h>

#include "common/character_data.h"
#include "common/enums.h"
#include "common/protocol/message_codec.h"
#include "game_generated.h"
#include "handlers/handler_utils.h"
#include "login_generated.h"

namespace legend2::handlers {

namespace {

std::vector<uint8_t> BuildRoleListRsp(mir2::common::ErrorCode code,
                                      const std::vector<mir2::world::RoleRecord>& roles) {
    flatbuffers::FlatBufferBuilder builder;
    std::vector<flatbuffers::Offset<mir2::proto::CharacterInfo>> role_offsets;
    role_offsets.reserve(roles.size());
    for (const auto& role : roles) {
        const auto name_offset = builder.CreateString(role.name);
        const auto info = mir2::proto::CreateCharacterInfo(
            builder, role.player_id, name_offset,
            static_cast<mir2::proto::Profession>(role.profession),
            static_cast<mir2::proto::Gender>(role.gender),
            role.level, role.map_id, role.x, role.y, role.gold);
        role_offsets.push_back(info);
    }
    const auto roles_vec = builder.CreateVector(role_offsets);
    const auto rsp = mir2::proto::CreateRoleListRsp(builder, ToProtoError(code), roles_vec);
    builder.Finish(rsp);
    const uint8_t* data = builder.GetBufferPointer();
    return std::vector<uint8_t>(data, data + builder.GetSize());
}

std::vector<uint8_t> BuildCreateRoleRsp(mir2::common::ErrorCode code, uint64_t player_id) {
    mir2::common::CreateCharacterResponse response;
    response.code = ToProtoError(code);
    response.player_id = player_id;

    mir2::common::MessageCodecStatus status = mir2::common::MessageCodecStatus::kOk;
    auto payload = mir2::common::EncodeCreateCharacterResponse(response, &status);
    if (status == mir2::common::MessageCodecStatus::kOk) {
        return payload;
    }

    response.code = mir2::proto::ErrorCode::ERR_UNKNOWN;
    response.player_id = 0;
    return mir2::common::EncodeCreateCharacterResponse(response, nullptr);
}

std::vector<uint8_t> BuildSelectRoleRsp(uint64_t player_id, mir2::common::ErrorCode code) {
    flatbuffers::FlatBufferBuilder builder;
    const auto rsp = mir2::proto::CreateSelectRoleRsp(builder, ToProtoError(code), player_id);
    builder.Finish(rsp);
    const uint8_t* data = builder.GetBufferPointer();
    return std::vector<uint8_t>(data, data + builder.GetSize());
}

std::vector<uint8_t> BuildEnterGameRsp(mir2::common::ErrorCode code,
                                       const mir2::world::RoleRecord* role) {
    flatbuffers::FlatBufferBuilder builder;
    flatbuffers::Offset<mir2::proto::PlayerInfo> player_offset = 0;
    if (role) {
        const auto name_offset = builder.CreateString(role->name);
        const int hp = 100;
        const int max_hp = 100;
        const int mp = 50;
        const int max_mp = 50;
        player_offset = mir2::proto::CreatePlayerInfo(
            builder, role->player_id, name_offset,
            static_cast<mir2::proto::Profession>(role->profession),
            role->level, hp, max_hp, mp, max_mp, role->map_id, role->x, role->y,
            role->gold);
    }
    const auto rsp = mir2::proto::CreateEnterGameRsp(builder, ToProtoError(code), player_offset);
    builder.Finish(rsp);
    const uint8_t* data = builder.GetBufferPointer();
    return std::vector<uint8_t>(data, data + builder.GetSize());
}

mir2::common::CharacterClass ToCharacterClass(mir2::proto::Profession profession) {
    switch (profession) {
        case mir2::proto::Profession::WARRIOR:
            return mir2::common::CharacterClass::WARRIOR;
        case mir2::proto::Profession::WIZARD:
            return mir2::common::CharacterClass::MAGE;
        case mir2::proto::Profession::TAOIST:
            return mir2::common::CharacterClass::TAOIST;
        default:
            return mir2::common::CharacterClass::WARRIOR;
    }
}

mir2::common::Gender ToCharacterGender(mir2::proto::Gender gender) {
    switch (gender) {
        case mir2::proto::Gender::FEMALE:
            return mir2::common::Gender::FEMALE;
        case mir2::proto::Gender::MALE:
        default:
            return mir2::common::Gender::MALE;
    }
}

mir2::common::CharacterCreateRequest BuildCreateRequest(uint64_t account_id,
                                                        const std::string& name,
                                                        mir2::proto::Profession profession,
                                                        mir2::proto::Gender gender) {
    mir2::common::CharacterCreateRequest request;
    request.account_id = std::to_string(account_id);
    request.name = name;
    request.char_class = ToCharacterClass(profession);
    request.gender = ToCharacterGender(gender);
    return request;
}

}  // namespace

CharacterHandler::CharacterHandler(mir2::ecs::CharacterEntityManager& entity_manager,
                                   mir2::world::RoleStore& role_store)
    : BaseHandler(mir2::log::LogCategory::kWorld),
      entity_manager_(entity_manager),
      role_store_(role_store) {}

void CharacterHandler::DoHandle(const HandlerContext& context,
                                uint16_t msg_id,
                                const std::vector<uint8_t>& payload,
                                ResponseCallback callback) {
    if (msg_id == static_cast<uint16_t>(mir2::common::MsgId::kRoleListReq)) {
        HandleRoleList(context, payload, std::move(callback));
        return;
    }
    if (msg_id == static_cast<uint16_t>(mir2::common::MsgId::kCreateRoleReq)) {
        HandleCreateRole(context, payload, std::move(callback));
        return;
    }
    if (msg_id == static_cast<uint16_t>(mir2::common::MsgId::kSelectRoleReq)) {
        HandleSelectRole(context, payload, std::move(callback));
        return;
    }
    if (msg_id == static_cast<uint16_t>(mir2::common::MsgId::kLogout)) {
        HandleLogout(context, payload, std::move(callback));
        return;
    }

    OnError(context, msg_id, mir2::common::ErrorCode::kInvalidAction, std::move(callback));
}

void CharacterHandler::OnError(const HandlerContext& context,
                               uint16_t msg_id,
                               mir2::common::ErrorCode error_code,
                               ResponseCallback callback) {
    BaseHandler::OnError(context, msg_id, error_code, callback);

    ResponseList responses;
    if (msg_id == static_cast<uint16_t>(mir2::common::MsgId::kRoleListReq)) {
        responses.push_back({context.client_id,
                             static_cast<uint16_t>(mir2::common::MsgId::kRoleListRsp),
                             BuildRoleListRsp(error_code, {})});
    } else if (msg_id == static_cast<uint16_t>(mir2::common::MsgId::kCreateRoleReq)) {
        responses.push_back({context.client_id,
                             static_cast<uint16_t>(mir2::common::MsgId::kCreateRoleRsp),
                             BuildCreateRoleRsp(error_code, 0)});
    } else if (msg_id == static_cast<uint16_t>(mir2::common::MsgId::kSelectRoleReq)) {
        responses.push_back({context.client_id,
                             static_cast<uint16_t>(mir2::common::MsgId::kSelectRoleRsp),
                             BuildSelectRoleRsp(0, error_code)});
        responses.push_back({context.client_id,
                             static_cast<uint16_t>(mir2::common::MsgId::kEnterGameRsp),
                             BuildEnterGameRsp(error_code, nullptr)});
    }

    if (callback) {
        callback(responses);
    }
}

void CharacterHandler::HandleRoleList(const HandlerContext& context,
                                      const std::vector<uint8_t>& payload,
                                      ResponseCallback callback) {
    flatbuffers::Verifier verifier(payload.data(), payload.size());
    if (!verifier.VerifyBuffer<mir2::proto::RoleListReq>(nullptr)) {
        OnError(context, static_cast<uint16_t>(mir2::common::MsgId::kRoleListReq),
                mir2::common::ErrorCode::kInvalidAction, std::move(callback));
        return;
    }

    const auto* req = flatbuffers::GetRoot<mir2::proto::RoleListReq>(payload.data());
    const uint64_t account_id = req ? req->account_id() : 0;
    if (account_id > 0) {
        role_store_.BindClientAccount(context.client_id, account_id);
    }

    auto roles = role_store_.GetRoles(account_id);
    const auto code = account_id > 0 ? mir2::common::ErrorCode::kOk
                                     : mir2::common::ErrorCode::kAccountNotFound;
    ResponseList responses;
    responses.push_back({context.client_id,
                         static_cast<uint16_t>(mir2::common::MsgId::kRoleListRsp),
                         BuildRoleListRsp(code, roles)});
    if (callback) {
        callback(responses);
    }
}

void CharacterHandler::HandleCreateRole(const HandlerContext& context,
                                        const std::vector<uint8_t>& payload,
                                        ResponseCallback callback) {
    mir2::common::CreateCharacterRequest request;
    const auto status = mir2::common::DecodeCreateCharacterRequest(
        mir2::common::kCreateCharacterRequestMsgId, payload, &request);
    if (status != mir2::common::MessageCodecStatus::kOk) {
        OnError(context, mir2::common::kCreateCharacterRequestMsgId,
                ToCommonError(status), std::move(callback));
        return;
    }

    const std::string name = request.name;
    const auto account_id_opt = role_store_.GetAccountId(context.client_id);
    const uint64_t account_id = account_id_opt.value_or(0);

    mir2::world::RoleRecord record;
    auto code = role_store_.CreateRole(
        account_id, name,
        static_cast<uint8_t>(request.profession),
        static_cast<uint8_t>(request.gender),
        &record);

    if (code == mir2::common::ErrorCode::kOk) {
        const auto create_request = BuildCreateRequest(account_id, name,
                                                       request.profession,
                                                       request.gender);
        entt::entity entity = entity_manager_.CreateFromRequest(
            static_cast<uint32_t>(record.player_id), create_request);
        if (entity == entt::null) {
            role_store_.RemoveRole(account_id, record.player_id);
            code = mir2::common::ErrorCode::kUnknown;
            record.player_id = 0;
        }
    }

    ResponseList responses;
    responses.push_back({context.client_id,
                         static_cast<uint16_t>(mir2::common::MsgId::kCreateRoleRsp),
                         BuildCreateRoleRsp(code, record.player_id)});
    if (callback) {
        callback(responses);
    }
}

void CharacterHandler::HandleSelectRole(const HandlerContext& context,
                                        const std::vector<uint8_t>& payload,
                                        ResponseCallback callback) {
    flatbuffers::Verifier verifier(payload.data(), payload.size());
    if (!verifier.VerifyBuffer<mir2::proto::SelectRoleReq>(nullptr)) {
        OnError(context, static_cast<uint16_t>(mir2::common::MsgId::kSelectRoleReq),
                mir2::common::ErrorCode::kInvalidAction, std::move(callback));
        return;
    }

    const auto* req = flatbuffers::GetRoot<mir2::proto::SelectRoleReq>(payload.data());
    const uint64_t player_id = req ? req->player_id() : 0;

    const auto account_id_opt = role_store_.GetAccountId(context.client_id);
    const uint64_t account_id = account_id_opt.value_or(0);
    const auto role_opt = role_store_.FindRole(account_id, player_id);
    const bool found = role_opt.has_value();
    const auto code = found ? mir2::common::ErrorCode::kOk
                            : mir2::common::ErrorCode::kAccountNotFound;

    ResponseList responses;
    responses.push_back({context.client_id,
                         static_cast<uint16_t>(mir2::common::MsgId::kSelectRoleRsp),
                         BuildSelectRoleRsp(player_id, code)});
    if (found) {
        entity_manager_.OnLogin(static_cast<uint32_t>(player_id));
        role_store_.BindClientRole(context.client_id, player_id);
    }
    const mir2::world::RoleRecord* role_ptr = found ? &role_opt.value() : nullptr;
    responses.push_back({context.client_id,
                         static_cast<uint16_t>(mir2::common::MsgId::kEnterGameRsp),
                         BuildEnterGameRsp(code, role_ptr)});
    if (callback) {
        callback(responses);
    }
}

void CharacterHandler::HandleLogout(const HandlerContext& context,
                                    const std::vector<uint8_t>& /*payload*/,
                                    ResponseCallback callback) {
    const auto role_id_opt = role_store_.GetRoleId(context.client_id);
    if (role_id_opt) {
        entity_manager_.OnDisconnect(static_cast<uint32_t>(*role_id_opt));
    }
    role_store_.UnbindClient(context.client_id);
    if (callback) {
        callback(ResponseList{});
    }
}

}  // namespace legend2::handlers
