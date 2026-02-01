#include "common/protocol/message_codec.h"

#include <flatbuffers/flatbuffers.h>

namespace mir2::common {

namespace {

MessageCodecStatus SetStatus(MessageCodecStatus status, MessageCodecStatus* out_status) {
    if (out_status) {
        *out_status = status;
    }
    return status;
}

MessageCodecStatus ValidateRequiredString(const std::string& value, size_t max_length) {
    if (value.empty()) {
        return MessageCodecStatus::kMissingField;
    }
    if (value.size() > max_length) {
        return MessageCodecStatus::kStringTooLong;
    }
    return MessageCodecStatus::kOk;
}

MessageCodecStatus ValidateOptionalString(const std::string& value, size_t max_length) {
    if (value.size() > max_length) {
        return MessageCodecStatus::kStringTooLong;
    }
    return MessageCodecStatus::kOk;
}

MessageCodecStatus ValidateNonNegativeInt(int32_t value) {
    return value < 0 ? MessageCodecStatus::kValueOutOfRange : MessageCodecStatus::kOk;
}

MessageCodecStatus ValidateEnumRange(uint32_t value, uint32_t max_value) {
    return value > max_value ? MessageCodecStatus::kValueOutOfRange : MessageCodecStatus::kOk;
}

MessageCodecStatus ValidateMsgId(uint16_t msg_id, uint16_t expected) {
    return msg_id == expected ? MessageCodecStatus::kOk : MessageCodecStatus::kInvalidMsgId;
}

template <typename T>
MessageCodecStatus VerifyPayload(const uint8_t* data, size_t size) {
    if (!data || size == 0) {
        return MessageCodecStatus::kInvalidPayload;
    }
    flatbuffers::Verifier verifier(data, size);
    return verifier.VerifyBuffer<T>(nullptr) ? MessageCodecStatus::kOk
                                             : MessageCodecStatus::kInvalidPayload;
}

template <typename T>
std::vector<uint8_t> BuildPayload(const T& offset, flatbuffers::FlatBufferBuilder* builder) {
    builder->Finish(offset);
    const uint8_t* data = builder->GetBufferPointer();
    return std::vector<uint8_t>(data, data + builder->GetSize());
}

}  // namespace

MessageCodecStatus ValidateLoginRequest(const LoginRequest& request) {
    auto status = ValidateRequiredString(request.username, kMaxLoginUsernameLength);
    if (status != MessageCodecStatus::kOk) {
        return status;
    }
    status = ValidateRequiredString(request.password, kMaxLoginPasswordLength);
    if (status != MessageCodecStatus::kOk) {
        return status;
    }
    return ValidateOptionalString(request.version, kMaxLoginVersionLength);
}

MessageCodecStatus ValidateLoginResponse(const LoginResponse& response) {
    if (response.code == mir2::proto::ErrorCode::ERR_OK) {
        if (response.account_id == 0 || response.session_token.empty()) {
            return MessageCodecStatus::kMissingField;
        }
    }
    return MessageCodecStatus::kOk;
}

MessageCodecStatus ValidateCreateCharacterRequest(const CreateCharacterRequest& request) {
    auto status = ValidateRequiredString(request.name, kMaxCharacterNameLength);
    if (status != MessageCodecStatus::kOk) {
        return status;
    }
    if (request.name.size() < 2) {
        return MessageCodecStatus::kValueOutOfRange;
    }
    status = ValidateEnumRange(static_cast<uint32_t>(request.profession),
                               static_cast<uint32_t>(mir2::proto::Profession::TAOIST));
    if (status != MessageCodecStatus::kOk) {
        return status;
    }
    return ValidateEnumRange(static_cast<uint32_t>(request.gender),
                             static_cast<uint32_t>(mir2::proto::Gender::FEMALE));
}

MessageCodecStatus ValidateCreateCharacterResponse(const CreateCharacterResponse& response) {
    if (response.code == mir2::proto::ErrorCode::ERR_OK && response.player_id == 0) {
        return MessageCodecStatus::kMissingField;
    }
    return MessageCodecStatus::kOk;
}

MessageCodecStatus ValidateMoveRequest(const MoveRequest& request) {
    auto status = ValidateNonNegativeInt(request.target_x);
    if (status != MessageCodecStatus::kOk) {
        return status;
    }
    return ValidateNonNegativeInt(request.target_y);
}

MessageCodecStatus ValidateMoveResponse(const MoveResponse& response) {
    if (response.code == mir2::proto::ErrorCode::ERR_OK) {
        auto status = ValidateNonNegativeInt(response.x);
        if (status != MessageCodecStatus::kOk) {
            return status;
        }
        return ValidateNonNegativeInt(response.y);
    }
    return MessageCodecStatus::kOk;
}

MessageCodecStatus ValidateAttackRequest(const AttackRequest& request) {
    if (request.target_id == 0) {
        return MessageCodecStatus::kMissingField;
    }
    if (request.target_type == mir2::proto::EntityType::NONE) {
        return MessageCodecStatus::kMissingField;
    }
    return ValidateEnumRange(static_cast<uint32_t>(request.target_type),
                             static_cast<uint32_t>(mir2::proto::EntityType::ITEM));
}

MessageCodecStatus ValidateAttackResponse(const AttackResponse& response) {
    if (response.code == mir2::proto::ErrorCode::ERR_OK &&
        (response.attacker_id == 0 || response.target_id == 0)) {
        return MessageCodecStatus::kMissingField;
    }
    return MessageCodecStatus::kOk;
}

MessageCodecStatus ValidateSkillRequest(const SkillRequest& request) {
    if (request.skill_id == 0) {
        return MessageCodecStatus::kMissingField;
    }
    if (request.target_type == mir2::proto::EntityType::NONE) {
        return MessageCodecStatus::kMissingField;
    }
    return ValidateEnumRange(static_cast<uint32_t>(request.target_type),
                             static_cast<uint32_t>(mir2::proto::EntityType::ITEM));
}

std::vector<uint8_t> EncodeLoginRequest(const LoginRequest& request,
                                        MessageCodecStatus* out_status) {
    auto status = ValidateLoginRequest(request);
    if (status != MessageCodecStatus::kOk) {
        SetStatus(status, out_status);
        return {};
    }

    flatbuffers::FlatBufferBuilder builder;
    const auto username_offset = builder.CreateString(request.username);
    const auto password_offset = builder.CreateString(request.password);
    const auto version_offset = request.version.empty() ? 0 : builder.CreateString(request.version);
    const auto req = mir2::proto::CreateLoginReq(builder, username_offset, password_offset, version_offset);

    SetStatus(MessageCodecStatus::kOk, out_status);
    return BuildPayload(req, &builder);
}

std::vector<uint8_t> EncodeLoginResponse(const LoginResponse& response,
                                         MessageCodecStatus* out_status) {
    auto status = ValidateLoginResponse(response);
    if (status != MessageCodecStatus::kOk) {
        SetStatus(status, out_status);
        return {};
    }

    flatbuffers::FlatBufferBuilder builder;
    const auto token_offset =
        response.session_token.empty() ? 0 : builder.CreateString(response.session_token);
    const auto rsp = mir2::proto::CreateLoginRsp(builder, response.code, response.account_id,
                                                 token_offset);

    SetStatus(MessageCodecStatus::kOk, out_status);
    return BuildPayload(rsp, &builder);
}

std::vector<uint8_t> EncodeCreateCharacterRequest(const CreateCharacterRequest& request,
                                                  MessageCodecStatus* out_status) {
    auto status = ValidateCreateCharacterRequest(request);
    if (status != MessageCodecStatus::kOk) {
        SetStatus(status, out_status);
        return {};
    }

    flatbuffers::FlatBufferBuilder builder;
    const auto name_offset = builder.CreateString(request.name);
    const auto req = mir2::proto::CreateCreateRoleReq(
        builder, name_offset, request.profession, request.gender);

    SetStatus(MessageCodecStatus::kOk, out_status);
    return BuildPayload(req, &builder);
}

std::vector<uint8_t> EncodeCreateCharacterResponse(const CreateCharacterResponse& response,
                                                   MessageCodecStatus* out_status) {
    auto status = ValidateCreateCharacterResponse(response);
    if (status != MessageCodecStatus::kOk) {
        SetStatus(status, out_status);
        return {};
    }

    flatbuffers::FlatBufferBuilder builder;
    const auto rsp = mir2::proto::CreateCreateRoleRsp(builder, response.code, response.player_id);

    SetStatus(MessageCodecStatus::kOk, out_status);
    return BuildPayload(rsp, &builder);
}

std::vector<uint8_t> EncodeMoveRequest(const MoveRequest& request,
                                       MessageCodecStatus* out_status) {
    auto status = ValidateMoveRequest(request);
    if (status != MessageCodecStatus::kOk) {
        SetStatus(status, out_status);
        return {};
    }

    flatbuffers::FlatBufferBuilder builder;
    const auto req = mir2::proto::CreateMoveReq(builder, request.target_x, request.target_y);

    SetStatus(MessageCodecStatus::kOk, out_status);
    return BuildPayload(req, &builder);
}

std::vector<uint8_t> EncodeMoveResponse(const MoveResponse& response,
                                        MessageCodecStatus* out_status) {
    auto status = ValidateMoveResponse(response);
    if (status != MessageCodecStatus::kOk) {
        SetStatus(status, out_status);
        return {};
    }

    flatbuffers::FlatBufferBuilder builder;
    const auto rsp = mir2::proto::CreateMoveRsp(builder, response.code, response.x, response.y);

    SetStatus(MessageCodecStatus::kOk, out_status);
    return BuildPayload(rsp, &builder);
}

std::vector<uint8_t> EncodeAttackRequest(const AttackRequest& request,
                                         MessageCodecStatus* out_status) {
    auto status = ValidateAttackRequest(request);
    if (status != MessageCodecStatus::kOk) {
        SetStatus(status, out_status);
        return {};
    }

    flatbuffers::FlatBufferBuilder builder;
    const auto req = mir2::proto::CreateAttackReq(builder, request.target_id, request.target_type);

    SetStatus(MessageCodecStatus::kOk, out_status);
    return BuildPayload(req, &builder);
}

std::vector<uint8_t> EncodeAttackResponse(const AttackResponse& response,
                                          MessageCodecStatus* out_status) {
    auto status = ValidateAttackResponse(response);
    if (status != MessageCodecStatus::kOk) {
        SetStatus(status, out_status);
        return {};
    }

    flatbuffers::FlatBufferBuilder builder;
    const auto rsp = mir2::proto::CreateAttackRsp(
        builder, response.code, response.attacker_id, response.target_id,
        response.damage, response.target_hp, response.target_dead);

    SetStatus(MessageCodecStatus::kOk, out_status);
    return BuildPayload(rsp, &builder);
}

std::vector<uint8_t> EncodeSkillRequest(const SkillRequest& request,
                                        MessageCodecStatus* out_status) {
    auto status = ValidateSkillRequest(request);
    if (status != MessageCodecStatus::kOk) {
        SetStatus(status, out_status);
        return {};
    }

    flatbuffers::FlatBufferBuilder builder;
    const auto req = mir2::proto::CreateSkillReq(builder, request.skill_id,
                                                  request.target_id);

    SetStatus(MessageCodecStatus::kOk, out_status);
    return BuildPayload(req, &builder);
}

MessageCodecStatus DecodeLoginRequest(uint16_t msg_id,
                                      const uint8_t* data,
                                      size_t size,
                                      LoginRequest* out_request) {
    if (!out_request) {
        return MessageCodecStatus::kInvalidPayload;
    }
    auto status = ValidateMsgId(msg_id, kLoginRequestMsgId);
    if (status != MessageCodecStatus::kOk) {
        return status;
    }
    status = VerifyPayload<mir2::proto::LoginReq>(data, size);
    if (status != MessageCodecStatus::kOk) {
        return status;
    }
    const auto* req = flatbuffers::GetRoot<mir2::proto::LoginReq>(data);
    if (!req || !req->username() || !req->password()) {
        return MessageCodecStatus::kMissingField;
    }

    out_request->username = req->username()->str();
    out_request->password = req->password()->str();
    out_request->version = req->version() ? req->version()->str() : "";
    return ValidateLoginRequest(*out_request);
}

MessageCodecStatus DecodeLoginRequest(uint16_t msg_id,
                                      const std::vector<uint8_t>& payload,
                                      LoginRequest* out_request) {
    return DecodeLoginRequest(msg_id, payload.data(), payload.size(), out_request);
}

MessageCodecStatus DecodeLoginResponse(uint16_t msg_id,
                                       const uint8_t* data,
                                       size_t size,
                                       LoginResponse* out_response) {
    if (!out_response) {
        return MessageCodecStatus::kInvalidPayload;
    }
    auto status = ValidateMsgId(msg_id, kLoginResponseMsgId);
    if (status != MessageCodecStatus::kOk) {
        return status;
    }
    status = VerifyPayload<mir2::proto::LoginRsp>(data, size);
    if (status != MessageCodecStatus::kOk) {
        return status;
    }
    const auto* rsp = flatbuffers::GetRoot<mir2::proto::LoginRsp>(data);
    if (!rsp) {
        return MessageCodecStatus::kInvalidPayload;
    }

    out_response->code = rsp->code();
    out_response->account_id = rsp->account_id();
    out_response->session_token = rsp->session_token() ? rsp->session_token()->str() : "";
    return ValidateLoginResponse(*out_response);
}

MessageCodecStatus DecodeLoginResponse(uint16_t msg_id,
                                       const std::vector<uint8_t>& payload,
                                       LoginResponse* out_response) {
    return DecodeLoginResponse(msg_id, payload.data(), payload.size(), out_response);
}

MessageCodecStatus DecodeCreateCharacterRequest(uint16_t msg_id,
                                                const uint8_t* data,
                                                size_t size,
                                                CreateCharacterRequest* out_request) {
    if (!out_request) {
        return MessageCodecStatus::kInvalidPayload;
    }
    auto status = ValidateMsgId(msg_id, kCreateCharacterRequestMsgId);
    if (status != MessageCodecStatus::kOk) {
        return status;
    }
    status = VerifyPayload<mir2::proto::CreateRoleReq>(data, size);
    if (status != MessageCodecStatus::kOk) {
        return status;
    }
    const auto* req = flatbuffers::GetRoot<mir2::proto::CreateRoleReq>(data);
    if (!req || !req->name()) {
        return MessageCodecStatus::kMissingField;
    }

    out_request->name = req->name()->str();
    out_request->profession = req->profession();
    out_request->gender = req->gender();
    return ValidateCreateCharacterRequest(*out_request);
}

MessageCodecStatus DecodeCreateCharacterRequest(uint16_t msg_id,
                                                const std::vector<uint8_t>& payload,
                                                CreateCharacterRequest* out_request) {
    return DecodeCreateCharacterRequest(msg_id, payload.data(), payload.size(), out_request);
}

MessageCodecStatus DecodeCreateCharacterResponse(uint16_t msg_id,
                                                 const uint8_t* data,
                                                 size_t size,
                                                 CreateCharacterResponse* out_response) {
    if (!out_response) {
        return MessageCodecStatus::kInvalidPayload;
    }
    auto status = ValidateMsgId(msg_id, kCreateCharacterResponseMsgId);
    if (status != MessageCodecStatus::kOk) {
        return status;
    }
    status = VerifyPayload<mir2::proto::CreateRoleRsp>(data, size);
    if (status != MessageCodecStatus::kOk) {
        return status;
    }
    const auto* rsp = flatbuffers::GetRoot<mir2::proto::CreateRoleRsp>(data);
    if (!rsp) {
        return MessageCodecStatus::kInvalidPayload;
    }

    out_response->code = rsp->code();
    out_response->player_id = rsp->player_id();
    return ValidateCreateCharacterResponse(*out_response);
}

MessageCodecStatus DecodeCreateCharacterResponse(uint16_t msg_id,
                                                 const std::vector<uint8_t>& payload,
                                                 CreateCharacterResponse* out_response) {
    return DecodeCreateCharacterResponse(msg_id, payload.data(), payload.size(), out_response);
}

MessageCodecStatus DecodeMoveRequest(uint16_t msg_id,
                                     const uint8_t* data,
                                     size_t size,
                                     MoveRequest* out_request) {
    if (!out_request) {
        return MessageCodecStatus::kInvalidPayload;
    }
    auto status = ValidateMsgId(msg_id, kMoveRequestMsgId);
    if (status != MessageCodecStatus::kOk) {
        return status;
    }
    status = VerifyPayload<mir2::proto::MoveReq>(data, size);
    if (status != MessageCodecStatus::kOk) {
        return status;
    }
    const auto* req = flatbuffers::GetRoot<mir2::proto::MoveReq>(data);
    if (!req) {
        return MessageCodecStatus::kInvalidPayload;
    }

    out_request->target_x = req->target_x();
    out_request->target_y = req->target_y();
    return ValidateMoveRequest(*out_request);
}

MessageCodecStatus DecodeMoveRequest(uint16_t msg_id,
                                     const std::vector<uint8_t>& payload,
                                     MoveRequest* out_request) {
    return DecodeMoveRequest(msg_id, payload.data(), payload.size(), out_request);
}

MessageCodecStatus DecodeMoveResponse(uint16_t msg_id,
                                      const uint8_t* data,
                                      size_t size,
                                      MoveResponse* out_response) {
    if (!out_response) {
        return MessageCodecStatus::kInvalidPayload;
    }
    auto status = ValidateMsgId(msg_id, kMoveResponseMsgId);
    if (status != MessageCodecStatus::kOk) {
        return status;
    }
    status = VerifyPayload<mir2::proto::MoveRsp>(data, size);
    if (status != MessageCodecStatus::kOk) {
        return status;
    }
    const auto* rsp = flatbuffers::GetRoot<mir2::proto::MoveRsp>(data);
    if (!rsp) {
        return MessageCodecStatus::kInvalidPayload;
    }

    out_response->code = rsp->code();
    out_response->x = rsp->x();
    out_response->y = rsp->y();
    return ValidateMoveResponse(*out_response);
}

MessageCodecStatus DecodeMoveResponse(uint16_t msg_id,
                                      const std::vector<uint8_t>& payload,
                                      MoveResponse* out_response) {
    return DecodeMoveResponse(msg_id, payload.data(), payload.size(), out_response);
}

MessageCodecStatus DecodeAttackRequest(uint16_t msg_id,
                                       const uint8_t* data,
                                       size_t size,
                                       AttackRequest* out_request) {
    if (!out_request) {
        return MessageCodecStatus::kInvalidPayload;
    }
    auto status = ValidateMsgId(msg_id, kAttackRequestMsgId);
    if (status != MessageCodecStatus::kOk) {
        return status;
    }
    status = VerifyPayload<mir2::proto::AttackReq>(data, size);
    if (status != MessageCodecStatus::kOk) {
        return status;
    }
    const auto* req = flatbuffers::GetRoot<mir2::proto::AttackReq>(data);
    if (!req) {
        return MessageCodecStatus::kInvalidPayload;
    }

    out_request->target_id = req->target_id();
    out_request->target_type = req->target_type();
    return ValidateAttackRequest(*out_request);
}

MessageCodecStatus DecodeAttackRequest(uint16_t msg_id,
                                       const std::vector<uint8_t>& payload,
                                       AttackRequest* out_request) {
    return DecodeAttackRequest(msg_id, payload.data(), payload.size(), out_request);
}

MessageCodecStatus DecodeAttackResponse(uint16_t msg_id,
                                        const uint8_t* data,
                                        size_t size,
                                        AttackResponse* out_response) {
    if (!out_response) {
        return MessageCodecStatus::kInvalidPayload;
    }
    auto status = ValidateMsgId(msg_id, kAttackResponseMsgId);
    if (status != MessageCodecStatus::kOk) {
        return status;
    }
    status = VerifyPayload<mir2::proto::AttackRsp>(data, size);
    if (status != MessageCodecStatus::kOk) {
        return status;
    }
    const auto* rsp = flatbuffers::GetRoot<mir2::proto::AttackRsp>(data);
    if (!rsp) {
        return MessageCodecStatus::kInvalidPayload;
    }

    out_response->code = rsp->code();
    out_response->attacker_id = rsp->attacker_id();
    out_response->target_id = rsp->target_id();
    out_response->damage = rsp->damage();
    out_response->target_hp = rsp->target_hp();
    out_response->target_dead = rsp->target_dead();
    return ValidateAttackResponse(*out_response);
}

MessageCodecStatus DecodeAttackResponse(uint16_t msg_id,
                                        const std::vector<uint8_t>& payload,
                                        AttackResponse* out_response) {
    return DecodeAttackResponse(msg_id, payload.data(), payload.size(), out_response);
}

}  // namespace mir2::common
