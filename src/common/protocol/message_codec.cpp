#include "common/protocol/message_codec.h"

#include <utility>

#include "common/protocol/typed_message_bindings.h"

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

template <typename Binding, typename Native, typename ValidateFn>
std::vector<uint8_t> EncodeTypedMessage(const Native& native,
                                        MessageCodecStatus* out_status,
                                        ValidateFn&& validate_fn) {
    const auto status = validate_fn(native);
    if (status != MessageCodecStatus::kOk) {
        SetStatus(status, out_status);
        return {};
    }
    SetStatus(MessageCodecStatus::kOk, out_status);
    return protocol::EncodeTypedPayload<Binding>(native);
}

template <typename Binding, typename Native, typename ValidateFn>
MessageCodecStatus DecodeTypedMessage(uint16_t msg_id,
                                      uint16_t expected_msg_id,
                                      const uint8_t* data,
                                      size_t size,
                                      Native* out_native,
                                      ValidateFn&& validate_fn,
                                      MessageCodecStatus decode_error =
                                          MessageCodecStatus::kMissingField) {
    if (!out_native) {
        return MessageCodecStatus::kInvalidPayload;
    }
    auto status = ValidateMsgId(msg_id, expected_msg_id);
    if (status != MessageCodecStatus::kOk) {
        return status;
    }
    if (!protocol::ValidateTypedPayload<Binding>(data, size)) {
        return MessageCodecStatus::kInvalidPayload;
    }

    Native decoded{};
    if (!protocol::DecodeTypedPayload<Binding>(data, size, &decoded)) {
        return decode_error;
    }
    *out_native = std::move(decoded);
    return validate_fn(*out_native);
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
    if (request.target_id == 0) {
        return MessageCodecStatus::kMissingField;
    }
    return MessageCodecStatus::kOk;
}

MessageCodecStatus ValidateSkillResponse(const SkillResponse& response) {
    auto status = ValidateEnumRange(static_cast<uint32_t>(response.result),
                                    static_cast<uint32_t>(mir2::proto::SkillResult::IMMUNE));
    if (status != MessageCodecStatus::kOk) {
        return status;
    }
    if (response.code == mir2::proto::ErrorCode::ERR_OK) {
        if (response.caster_id == 0 || response.skill_id == 0) {
            return MessageCodecStatus::kMissingField;
        }
    }
    return MessageCodecStatus::kOk;
}

MessageCodecStatus ValidateUseItemRequest(const UseItemRequest& request) {
    if (request.item_id == 0) {
        return MessageCodecStatus::kMissingField;
    }
    return MessageCodecStatus::kOk;
}

MessageCodecStatus ValidateUseItemResponse(const UseItemResponse& response) {
    if (response.code == mir2::proto::ErrorCode::ERR_OK && response.item_id == 0) {
        return MessageCodecStatus::kMissingField;
    }
    return MessageCodecStatus::kOk;
}

MessageCodecStatus ValidatePickupItemRequest(const PickupItemRequest& request) {
    if (request.item_id == 0) {
        return MessageCodecStatus::kMissingField;
    }
    return MessageCodecStatus::kOk;
}

MessageCodecStatus ValidatePickupItemResponse(const PickupItemResponse& response) {
    if (response.code == mir2::proto::ErrorCode::ERR_OK && response.item_id == 0) {
        return MessageCodecStatus::kMissingField;
    }
    return MessageCodecStatus::kOk;
}

MessageCodecStatus ValidateDropItemRequest(const DropItemRequest& request) {
    if (request.item_id == 0 || request.count == 0) {
        return MessageCodecStatus::kMissingField;
    }
    return MessageCodecStatus::kOk;
}

MessageCodecStatus ValidateDropItemResponse(const DropItemResponse& response) {
    if (response.code == mir2::proto::ErrorCode::ERR_OK &&
        (response.item_id == 0 || response.count == 0)) {
        return MessageCodecStatus::kMissingField;
    }
    return MessageCodecStatus::kOk;
}

MessageCodecStatus ValidateEquipRequest(const EquipRequest& request) {
    if (request.item_id == 0) {
        return MessageCodecStatus::kMissingField;
    }
    return MessageCodecStatus::kOk;
}

MessageCodecStatus ValidateEquipResponse(const EquipResponse& response) {
    if (response.code == mir2::proto::ErrorCode::ERR_OK && response.item_id == 0) {
        return MessageCodecStatus::kMissingField;
    }
    return MessageCodecStatus::kOk;
}

MessageCodecStatus ValidateUnequipRequest(const UnequipRequest&) {
    return MessageCodecStatus::kOk;
}

MessageCodecStatus ValidateUnequipResponse(const UnequipResponse& response) {
    if (response.code == mir2::proto::ErrorCode::ERR_OK && response.item_id == 0) {
        return MessageCodecStatus::kMissingField;
    }
    return MessageCodecStatus::kOk;
}

MessageCodecStatus ValidateChatRequest(const ChatRequest& request) {
    auto status = ValidateEnumRange(static_cast<uint32_t>(request.channel),
                                    static_cast<uint32_t>(mir2::proto::ChatChannel::AREA));
    if (status != MessageCodecStatus::kOk) {
        return status;
    }
    return ValidateRequiredString(request.content, kMaxChatContentLength);
}

MessageCodecStatus ValidateChatResponse(const ChatResponse&) {
    return MessageCodecStatus::kOk;
}

MessageCodecStatus ValidateGuildCreateRequest(const GuildCreateRequest& request) {
    if (request.guild_name.empty()) {
        return MessageCodecStatus::kMissingField;
    }
    return MessageCodecStatus::kOk;
}

MessageCodecStatus ValidateGuildCreateResponse(const GuildCreateResponse& response) {
    if (response.success && response.guild_id == 0) {
        return MessageCodecStatus::kMissingField;
    }
    return MessageCodecStatus::kOk;
}

MessageCodecStatus ValidateGuildJoinRequest(const GuildJoinRequest& request) {
    if (request.guild_id == 0) {
        return MessageCodecStatus::kMissingField;
    }
    return MessageCodecStatus::kOk;
}

MessageCodecStatus ValidateGuildJoinResponse(const GuildJoinResponse&) {
    return MessageCodecStatus::kOk;
}

MessageCodecStatus ValidateGuildLeaveRequest(const GuildLeaveRequest&) {
    return MessageCodecStatus::kOk;
}

MessageCodecStatus ValidateGuildLeaveResponse(const GuildLeaveResponse&) {
    return MessageCodecStatus::kOk;
}

MessageCodecStatus ValidateGuildKickRequest(const GuildKickRequest& request) {
    if (request.target_character_id == 0) {
        return MessageCodecStatus::kMissingField;
    }
    return MessageCodecStatus::kOk;
}

MessageCodecStatus ValidateGuildKickResponse(const GuildKickResponse&) {
    return MessageCodecStatus::kOk;
}

MessageCodecStatus ValidateGuildDeclareWarRequest(const GuildDeclareWarRequest& request) {
    if (request.target_guild_id == 0) {
        return MessageCodecStatus::kMissingField;
    }
    return MessageCodecStatus::kOk;
}

MessageCodecStatus ValidateGuildDeclareWarResponse(const GuildDeclareWarResponse&) {
    return MessageCodecStatus::kOk;
}

MessageCodecStatus ValidateGuildCancelWarRequest(const GuildCancelWarRequest& request) {
    if (request.target_guild_id == 0) {
        return MessageCodecStatus::kMissingField;
    }
    return MessageCodecStatus::kOk;
}

MessageCodecStatus ValidateGuildCancelWarResponse(const GuildCancelWarResponse&) {
    return MessageCodecStatus::kOk;
}

MessageCodecStatus ValidateGuildMakeAllyRequest(const GuildMakeAllyRequest& request) {
    if (request.target_guild_id == 0) {
        return MessageCodecStatus::kMissingField;
    }
    return MessageCodecStatus::kOk;
}

MessageCodecStatus ValidateGuildMakeAllyResponse(const GuildMakeAllyResponse&) {
    return MessageCodecStatus::kOk;
}

MessageCodecStatus ValidateGuildBreakAllyRequest(const GuildBreakAllyRequest& request) {
    if (request.target_guild_id == 0) {
        return MessageCodecStatus::kMissingField;
    }
    return MessageCodecStatus::kOk;
}

MessageCodecStatus ValidateGuildBreakAllyResponse(const GuildBreakAllyResponse&) {
    return MessageCodecStatus::kOk;
}

std::vector<uint8_t> EncodeLoginRequest(const LoginRequest& request,
                                        MessageCodecStatus* out_status) {
    return EncodeTypedMessage<protocol::bindings::LoginReqBinding>(
        request, out_status, ValidateLoginRequest);
}

std::vector<uint8_t> EncodeLoginResponse(const LoginResponse& response,
                                         MessageCodecStatus* out_status) {
    return EncodeTypedMessage<protocol::bindings::LoginRspBinding>(
        response, out_status, ValidateLoginResponse);
}

std::vector<uint8_t> EncodeCreateCharacterRequest(const CreateCharacterRequest& request,
                                                  MessageCodecStatus* out_status) {
    return EncodeTypedMessage<protocol::bindings::CreateRoleReqBinding>(
        request, out_status, ValidateCreateCharacterRequest);
}

std::vector<uint8_t> EncodeCreateCharacterResponse(const CreateCharacterResponse& response,
                                                   MessageCodecStatus* out_status) {
    return EncodeTypedMessage<protocol::bindings::CreateRoleRspBinding>(
        response, out_status, ValidateCreateCharacterResponse);
}

std::vector<uint8_t> EncodeMoveRequest(const MoveRequest& request,
                                       MessageCodecStatus* out_status) {
    return EncodeTypedMessage<protocol::bindings::MoveReqBinding>(
        request, out_status, ValidateMoveRequest);
}

std::vector<uint8_t> EncodeMoveResponse(const MoveResponse& response,
                                        MessageCodecStatus* out_status) {
    return EncodeTypedMessage<protocol::bindings::MoveRspBinding>(
        response, out_status, ValidateMoveResponse);
}

std::vector<uint8_t> EncodeAttackRequest(const AttackRequest& request,
                                         MessageCodecStatus* out_status) {
    return EncodeTypedMessage<protocol::bindings::AttackReqBinding>(
        request, out_status, ValidateAttackRequest);
}

std::vector<uint8_t> EncodeAttackResponse(const AttackResponse& response,
                                          MessageCodecStatus* out_status) {
    return EncodeTypedMessage<protocol::bindings::AttackRspBinding>(
        response, out_status, ValidateAttackResponse);
}

std::vector<uint8_t> EncodeSkillRequest(const SkillRequest& request,
                                        MessageCodecStatus* out_status) {
    return EncodeTypedMessage<protocol::bindings::SkillReqBinding>(
        request, out_status, ValidateSkillRequest);
}

std::vector<uint8_t> EncodeSkillResponse(const SkillResponse& response,
                                         MessageCodecStatus* out_status) {
    return EncodeTypedMessage<protocol::bindings::SkillRspBinding>(
        response, out_status, ValidateSkillResponse);
}

std::vector<uint8_t> EncodeUseItemRequest(const UseItemRequest& request,
                                          MessageCodecStatus* out_status) {
    return EncodeTypedMessage<protocol::bindings::UseItemReqBinding>(
        request, out_status, ValidateUseItemRequest);
}

std::vector<uint8_t> EncodeUseItemResponse(const UseItemResponse& response,
                                           MessageCodecStatus* out_status) {
    return EncodeTypedMessage<protocol::bindings::UseItemRspBinding>(
        response, out_status, ValidateUseItemResponse);
}

std::vector<uint8_t> EncodePickupItemRequest(const PickupItemRequest& request,
                                             MessageCodecStatus* out_status) {
    return EncodeTypedMessage<protocol::bindings::PickupItemReqBinding>(
        request, out_status, ValidatePickupItemRequest);
}

std::vector<uint8_t> EncodePickupItemResponse(const PickupItemResponse& response,
                                              MessageCodecStatus* out_status) {
    return EncodeTypedMessage<protocol::bindings::PickupItemRspBinding>(
        response, out_status, ValidatePickupItemResponse);
}

std::vector<uint8_t> EncodeDropItemRequest(const DropItemRequest& request,
                                           MessageCodecStatus* out_status) {
    return EncodeTypedMessage<protocol::bindings::DropItemReqBinding>(
        request, out_status, ValidateDropItemRequest);
}

std::vector<uint8_t> EncodeDropItemResponse(const DropItemResponse& response,
                                            MessageCodecStatus* out_status) {
    return EncodeTypedMessage<protocol::bindings::DropItemRspBinding>(
        response, out_status, ValidateDropItemResponse);
}

std::vector<uint8_t> EncodeEquipRequest(const EquipRequest& request,
                                        MessageCodecStatus* out_status) {
    return EncodeTypedMessage<protocol::bindings::EquipReqBinding>(
        request, out_status, ValidateEquipRequest);
}

std::vector<uint8_t> EncodeEquipResponse(const EquipResponse& response,
                                         MessageCodecStatus* out_status) {
    return EncodeTypedMessage<protocol::bindings::EquipRspBinding>(
        response, out_status, ValidateEquipResponse);
}

std::vector<uint8_t> EncodeUnequipRequest(const UnequipRequest& request,
                                          MessageCodecStatus* out_status) {
    return EncodeTypedMessage<protocol::bindings::UnequipReqBinding>(
        request, out_status, ValidateUnequipRequest);
}

std::vector<uint8_t> EncodeUnequipResponse(const UnequipResponse& response,
                                           MessageCodecStatus* out_status) {
    return EncodeTypedMessage<protocol::bindings::UnequipRspBinding>(
        response, out_status, ValidateUnequipResponse);
}

std::vector<uint8_t> EncodeChatRequest(const ChatRequest& request,
                                       MessageCodecStatus* out_status) {
    return EncodeTypedMessage<protocol::bindings::ChatReqBinding>(
        request, out_status, ValidateChatRequest);
}

std::vector<uint8_t> EncodeChatResponse(const ChatResponse& response,
                                        MessageCodecStatus* out_status) {
    return EncodeTypedMessage<protocol::bindings::ChatRspBinding>(
        response, out_status, ValidateChatResponse);
}

std::vector<uint8_t> EncodeGuildCreateRequest(const GuildCreateRequest& request,
                                              MessageCodecStatus* out_status) {
    return EncodeTypedMessage<protocol::bindings::GuildCreateReqBinding>(
        request, out_status, ValidateGuildCreateRequest);
}

std::vector<uint8_t> EncodeGuildCreateResponse(const GuildCreateResponse& response,
                                               MessageCodecStatus* out_status) {
    return EncodeTypedMessage<protocol::bindings::GuildCreateRspBinding>(
        response, out_status, ValidateGuildCreateResponse);
}

std::vector<uint8_t> EncodeGuildJoinRequest(const GuildJoinRequest& request,
                                            MessageCodecStatus* out_status) {
    return EncodeTypedMessage<protocol::bindings::GuildJoinReqBinding>(
        request, out_status, ValidateGuildJoinRequest);
}

std::vector<uint8_t> EncodeGuildJoinResponse(const GuildJoinResponse& response,
                                             MessageCodecStatus* out_status) {
    return EncodeTypedMessage<protocol::bindings::GuildJoinRspBinding>(
        response, out_status, ValidateGuildJoinResponse);
}

std::vector<uint8_t> EncodeGuildLeaveRequest(const GuildLeaveRequest& request,
                                             MessageCodecStatus* out_status) {
    return EncodeTypedMessage<protocol::bindings::GuildLeaveReqBinding>(
        request, out_status, ValidateGuildLeaveRequest);
}

std::vector<uint8_t> EncodeGuildLeaveResponse(const GuildLeaveResponse& response,
                                              MessageCodecStatus* out_status) {
    return EncodeTypedMessage<protocol::bindings::GuildLeaveRspBinding>(
        response, out_status, ValidateGuildLeaveResponse);
}

std::vector<uint8_t> EncodeGuildKickRequest(const GuildKickRequest& request,
                                            MessageCodecStatus* out_status) {
    return EncodeTypedMessage<protocol::bindings::GuildKickReqBinding>(
        request, out_status, ValidateGuildKickRequest);
}

std::vector<uint8_t> EncodeGuildKickResponse(const GuildKickResponse& response,
                                             MessageCodecStatus* out_status) {
    return EncodeTypedMessage<protocol::bindings::GuildKickRspBinding>(
        response, out_status, ValidateGuildKickResponse);
}

std::vector<uint8_t> EncodeGuildDeclareWarRequest(const GuildDeclareWarRequest& request,
                                                  MessageCodecStatus* out_status) {
    return EncodeTypedMessage<protocol::bindings::GuildDeclareWarReqBinding>(
        request, out_status, ValidateGuildDeclareWarRequest);
}

std::vector<uint8_t> EncodeGuildDeclareWarResponse(const GuildDeclareWarResponse& response,
                                                   MessageCodecStatus* out_status) {
    return EncodeTypedMessage<protocol::bindings::GuildDeclareWarRspBinding>(
        response, out_status, ValidateGuildDeclareWarResponse);
}

std::vector<uint8_t> EncodeGuildCancelWarRequest(const GuildCancelWarRequest& request,
                                                 MessageCodecStatus* out_status) {
    return EncodeTypedMessage<protocol::bindings::GuildCancelWarReqBinding>(
        request, out_status, ValidateGuildCancelWarRequest);
}

std::vector<uint8_t> EncodeGuildCancelWarResponse(const GuildCancelWarResponse& response,
                                                  MessageCodecStatus* out_status) {
    return EncodeTypedMessage<protocol::bindings::GuildCancelWarRspBinding>(
        response, out_status, ValidateGuildCancelWarResponse);
}

std::vector<uint8_t> EncodeGuildMakeAllyRequest(const GuildMakeAllyRequest& request,
                                                MessageCodecStatus* out_status) {
    return EncodeTypedMessage<protocol::bindings::GuildMakeAllyReqBinding>(
        request, out_status, ValidateGuildMakeAllyRequest);
}

std::vector<uint8_t> EncodeGuildMakeAllyResponse(const GuildMakeAllyResponse& response,
                                                 MessageCodecStatus* out_status) {
    return EncodeTypedMessage<protocol::bindings::GuildMakeAllyRspBinding>(
        response, out_status, ValidateGuildMakeAllyResponse);
}

std::vector<uint8_t> EncodeGuildBreakAllyRequest(const GuildBreakAllyRequest& request,
                                                 MessageCodecStatus* out_status) {
    return EncodeTypedMessage<protocol::bindings::GuildBreakAllyReqBinding>(
        request, out_status, ValidateGuildBreakAllyRequest);
}

std::vector<uint8_t> EncodeGuildBreakAllyResponse(const GuildBreakAllyResponse& response,
                                                  MessageCodecStatus* out_status) {
    return EncodeTypedMessage<protocol::bindings::GuildBreakAllyRspBinding>(
        response, out_status, ValidateGuildBreakAllyResponse);
}

MessageCodecStatus DecodeLoginRequest(uint16_t msg_id,
                                      const uint8_t* data,
                                      size_t size,
                                      LoginRequest* out_request) {
    return DecodeTypedMessage<protocol::bindings::LoginReqBinding>(
        msg_id, kLoginRequestMsgId, data, size, out_request, ValidateLoginRequest,
        MessageCodecStatus::kMissingField);
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
    return DecodeTypedMessage<protocol::bindings::LoginRspBinding>(
        msg_id, kLoginResponseMsgId, data, size, out_response, ValidateLoginResponse,
        MessageCodecStatus::kInvalidPayload);
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
    return DecodeTypedMessage<protocol::bindings::CreateRoleReqBinding>(
        msg_id, kCreateCharacterRequestMsgId, data, size, out_request,
        ValidateCreateCharacterRequest, MessageCodecStatus::kMissingField);
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
    return DecodeTypedMessage<protocol::bindings::CreateRoleRspBinding>(
        msg_id, kCreateCharacterResponseMsgId, data, size, out_response,
        ValidateCreateCharacterResponse, MessageCodecStatus::kInvalidPayload);
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
    return DecodeTypedMessage<protocol::bindings::MoveReqBinding>(
        msg_id, kMoveRequestMsgId, data, size, out_request, ValidateMoveRequest,
        MessageCodecStatus::kInvalidPayload);
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
    return DecodeTypedMessage<protocol::bindings::MoveRspBinding>(
        msg_id, kMoveResponseMsgId, data, size, out_response, ValidateMoveResponse,
        MessageCodecStatus::kInvalidPayload);
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
    return DecodeTypedMessage<protocol::bindings::AttackReqBinding>(
        msg_id, kAttackRequestMsgId, data, size, out_request, ValidateAttackRequest,
        MessageCodecStatus::kInvalidPayload);
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
    return DecodeTypedMessage<protocol::bindings::AttackRspBinding>(
        msg_id, kAttackResponseMsgId, data, size, out_response, ValidateAttackResponse,
        MessageCodecStatus::kInvalidPayload);
}

MessageCodecStatus DecodeAttackResponse(uint16_t msg_id,
                                        const std::vector<uint8_t>& payload,
                                        AttackResponse* out_response) {
    return DecodeAttackResponse(msg_id, payload.data(), payload.size(), out_response);
}

MessageCodecStatus DecodeSkillRequest(uint16_t msg_id,
                                      const uint8_t* data,
                                      size_t size,
                                      SkillRequest* out_request) {
    return DecodeTypedMessage<protocol::bindings::SkillReqBinding>(
        msg_id, kSkillRequestMsgId, data, size, out_request, ValidateSkillRequest,
        MessageCodecStatus::kInvalidPayload);
}

MessageCodecStatus DecodeSkillRequest(uint16_t msg_id,
                                      const std::vector<uint8_t>& payload,
                                      SkillRequest* out_request) {
    return DecodeSkillRequest(msg_id, payload.data(), payload.size(), out_request);
}

MessageCodecStatus DecodeSkillResponse(uint16_t msg_id,
                                       const uint8_t* data,
                                       size_t size,
                                       SkillResponse* out_response) {
    return DecodeTypedMessage<protocol::bindings::SkillRspBinding>(
        msg_id, kSkillResponseMsgId, data, size, out_response, ValidateSkillResponse,
        MessageCodecStatus::kInvalidPayload);
}

MessageCodecStatus DecodeSkillResponse(uint16_t msg_id,
                                       const std::vector<uint8_t>& payload,
                                       SkillResponse* out_response) {
    return DecodeSkillResponse(msg_id, payload.data(), payload.size(), out_response);
}

MessageCodecStatus DecodeUseItemRequest(uint16_t msg_id,
                                        const uint8_t* data,
                                        size_t size,
                                        UseItemRequest* out_request) {
    return DecodeTypedMessage<protocol::bindings::UseItemReqBinding>(
        msg_id, kUseItemRequestMsgId, data, size, out_request, ValidateUseItemRequest,
        MessageCodecStatus::kInvalidPayload);
}

MessageCodecStatus DecodeUseItemRequest(uint16_t msg_id,
                                        const std::vector<uint8_t>& payload,
                                        UseItemRequest* out_request) {
    return DecodeUseItemRequest(msg_id, payload.data(), payload.size(), out_request);
}

MessageCodecStatus DecodeUseItemResponse(uint16_t msg_id,
                                         const uint8_t* data,
                                         size_t size,
                                         UseItemResponse* out_response) {
    return DecodeTypedMessage<protocol::bindings::UseItemRspBinding>(
        msg_id, kUseItemResponseMsgId, data, size, out_response, ValidateUseItemResponse,
        MessageCodecStatus::kInvalidPayload);
}

MessageCodecStatus DecodeUseItemResponse(uint16_t msg_id,
                                         const std::vector<uint8_t>& payload,
                                         UseItemResponse* out_response) {
    return DecodeUseItemResponse(msg_id, payload.data(), payload.size(), out_response);
}

MessageCodecStatus DecodePickupItemRequest(uint16_t msg_id,
                                           const uint8_t* data,
                                           size_t size,
                                           PickupItemRequest* out_request) {
    return DecodeTypedMessage<protocol::bindings::PickupItemReqBinding>(
        msg_id, kPickupItemRequestMsgId, data, size, out_request, ValidatePickupItemRequest,
        MessageCodecStatus::kInvalidPayload);
}

MessageCodecStatus DecodePickupItemRequest(uint16_t msg_id,
                                           const std::vector<uint8_t>& payload,
                                           PickupItemRequest* out_request) {
    return DecodePickupItemRequest(msg_id, payload.data(), payload.size(), out_request);
}

MessageCodecStatus DecodePickupItemResponse(uint16_t msg_id,
                                            const uint8_t* data,
                                            size_t size,
                                            PickupItemResponse* out_response) {
    return DecodeTypedMessage<protocol::bindings::PickupItemRspBinding>(
        msg_id, kPickupItemResponseMsgId, data, size, out_response, ValidatePickupItemResponse,
        MessageCodecStatus::kInvalidPayload);
}

MessageCodecStatus DecodePickupItemResponse(uint16_t msg_id,
                                            const std::vector<uint8_t>& payload,
                                            PickupItemResponse* out_response) {
    return DecodePickupItemResponse(msg_id, payload.data(), payload.size(), out_response);
}

MessageCodecStatus DecodeDropItemRequest(uint16_t msg_id,
                                         const uint8_t* data,
                                         size_t size,
                                         DropItemRequest* out_request) {
    return DecodeTypedMessage<protocol::bindings::DropItemReqBinding>(
        msg_id, kDropItemRequestMsgId, data, size, out_request, ValidateDropItemRequest,
        MessageCodecStatus::kInvalidPayload);
}

MessageCodecStatus DecodeDropItemRequest(uint16_t msg_id,
                                         const std::vector<uint8_t>& payload,
                                         DropItemRequest* out_request) {
    return DecodeDropItemRequest(msg_id, payload.data(), payload.size(), out_request);
}

MessageCodecStatus DecodeDropItemResponse(uint16_t msg_id,
                                          const uint8_t* data,
                                          size_t size,
                                          DropItemResponse* out_response) {
    return DecodeTypedMessage<protocol::bindings::DropItemRspBinding>(
        msg_id, kDropItemResponseMsgId, data, size, out_response, ValidateDropItemResponse,
        MessageCodecStatus::kInvalidPayload);
}

MessageCodecStatus DecodeDropItemResponse(uint16_t msg_id,
                                          const std::vector<uint8_t>& payload,
                                          DropItemResponse* out_response) {
    return DecodeDropItemResponse(msg_id, payload.data(), payload.size(), out_response);
}

MessageCodecStatus DecodeEquipRequest(uint16_t msg_id,
                                      const uint8_t* data,
                                      size_t size,
                                      EquipRequest* out_request) {
    return DecodeTypedMessage<protocol::bindings::EquipReqBinding>(
        msg_id, kEquipRequestMsgId, data, size, out_request, ValidateEquipRequest,
        MessageCodecStatus::kInvalidPayload);
}

MessageCodecStatus DecodeEquipRequest(uint16_t msg_id,
                                      const std::vector<uint8_t>& payload,
                                      EquipRequest* out_request) {
    return DecodeEquipRequest(msg_id, payload.data(), payload.size(), out_request);
}

MessageCodecStatus DecodeEquipResponse(uint16_t msg_id,
                                       const uint8_t* data,
                                       size_t size,
                                       EquipResponse* out_response) {
    return DecodeTypedMessage<protocol::bindings::EquipRspBinding>(
        msg_id, kEquipResponseMsgId, data, size, out_response, ValidateEquipResponse,
        MessageCodecStatus::kInvalidPayload);
}

MessageCodecStatus DecodeEquipResponse(uint16_t msg_id,
                                       const std::vector<uint8_t>& payload,
                                       EquipResponse* out_response) {
    return DecodeEquipResponse(msg_id, payload.data(), payload.size(), out_response);
}

MessageCodecStatus DecodeUnequipRequest(uint16_t msg_id,
                                        const uint8_t* data,
                                        size_t size,
                                        UnequipRequest* out_request) {
    return DecodeTypedMessage<protocol::bindings::UnequipReqBinding>(
        msg_id, kUnequipRequestMsgId, data, size, out_request, ValidateUnequipRequest,
        MessageCodecStatus::kInvalidPayload);
}

MessageCodecStatus DecodeUnequipRequest(uint16_t msg_id,
                                        const std::vector<uint8_t>& payload,
                                        UnequipRequest* out_request) {
    return DecodeUnequipRequest(msg_id, payload.data(), payload.size(), out_request);
}

MessageCodecStatus DecodeUnequipResponse(uint16_t msg_id,
                                         const uint8_t* data,
                                         size_t size,
                                         UnequipResponse* out_response) {
    return DecodeTypedMessage<protocol::bindings::UnequipRspBinding>(
        msg_id, kUnequipResponseMsgId, data, size, out_response, ValidateUnequipResponse,
        MessageCodecStatus::kInvalidPayload);
}

MessageCodecStatus DecodeUnequipResponse(uint16_t msg_id,
                                         const std::vector<uint8_t>& payload,
                                         UnequipResponse* out_response) {
    return DecodeUnequipResponse(msg_id, payload.data(), payload.size(), out_response);
}

MessageCodecStatus DecodeChatRequest(uint16_t msg_id,
                                     const uint8_t* data,
                                     size_t size,
                                     ChatRequest* out_request) {
    return DecodeTypedMessage<protocol::bindings::ChatReqBinding>(
        msg_id, kChatRequestMsgId, data, size, out_request, ValidateChatRequest,
        MessageCodecStatus::kMissingField);
}

MessageCodecStatus DecodeChatRequest(uint16_t msg_id,
                                     const std::vector<uint8_t>& payload,
                                     ChatRequest* out_request) {
    return DecodeChatRequest(msg_id, payload.data(), payload.size(), out_request);
}

MessageCodecStatus DecodeChatResponse(uint16_t msg_id,
                                      const uint8_t* data,
                                      size_t size,
                                      ChatResponse* out_response) {
    return DecodeTypedMessage<protocol::bindings::ChatRspBinding>(
        msg_id, kChatResponseMsgId, data, size, out_response, ValidateChatResponse,
        MessageCodecStatus::kInvalidPayload);
}

MessageCodecStatus DecodeChatResponse(uint16_t msg_id,
                                      const std::vector<uint8_t>& payload,
                                      ChatResponse* out_response) {
    return DecodeChatResponse(msg_id, payload.data(), payload.size(), out_response);
}

MessageCodecStatus DecodeGuildCreateRequest(uint16_t msg_id,
                                            const uint8_t* data,
                                            size_t size,
                                            GuildCreateRequest* out_request) {
    return DecodeTypedMessage<protocol::bindings::GuildCreateReqBinding>(
        msg_id, kGuildCreateRequestMsgId, data, size, out_request, ValidateGuildCreateRequest,
        MessageCodecStatus::kMissingField);
}

MessageCodecStatus DecodeGuildCreateRequest(uint16_t msg_id,
                                            const std::vector<uint8_t>& payload,
                                            GuildCreateRequest* out_request) {
    return DecodeGuildCreateRequest(msg_id, payload.data(), payload.size(), out_request);
}

MessageCodecStatus DecodeGuildCreateResponse(uint16_t msg_id,
                                             const uint8_t* data,
                                             size_t size,
                                             GuildCreateResponse* out_response) {
    return DecodeTypedMessage<protocol::bindings::GuildCreateRspBinding>(
        msg_id, kGuildCreateResponseMsgId, data, size, out_response,
        ValidateGuildCreateResponse, MessageCodecStatus::kInvalidPayload);
}

MessageCodecStatus DecodeGuildCreateResponse(uint16_t msg_id,
                                             const std::vector<uint8_t>& payload,
                                             GuildCreateResponse* out_response) {
    return DecodeGuildCreateResponse(msg_id, payload.data(), payload.size(), out_response);
}

MessageCodecStatus DecodeGuildJoinRequest(uint16_t msg_id,
                                          const uint8_t* data,
                                          size_t size,
                                          GuildJoinRequest* out_request) {
    return DecodeTypedMessage<protocol::bindings::GuildJoinReqBinding>(
        msg_id, kGuildJoinRequestMsgId, data, size, out_request, ValidateGuildJoinRequest,
        MessageCodecStatus::kInvalidPayload);
}

MessageCodecStatus DecodeGuildJoinRequest(uint16_t msg_id,
                                          const std::vector<uint8_t>& payload,
                                          GuildJoinRequest* out_request) {
    return DecodeGuildJoinRequest(msg_id, payload.data(), payload.size(), out_request);
}

MessageCodecStatus DecodeGuildJoinResponse(uint16_t msg_id,
                                           const uint8_t* data,
                                           size_t size,
                                           GuildJoinResponse* out_response) {
    return DecodeTypedMessage<protocol::bindings::GuildJoinRspBinding>(
        msg_id, kGuildJoinResponseMsgId, data, size, out_response, ValidateGuildJoinResponse,
        MessageCodecStatus::kInvalidPayload);
}

MessageCodecStatus DecodeGuildJoinResponse(uint16_t msg_id,
                                           const std::vector<uint8_t>& payload,
                                           GuildJoinResponse* out_response) {
    return DecodeGuildJoinResponse(msg_id, payload.data(), payload.size(), out_response);
}

MessageCodecStatus DecodeGuildLeaveRequest(uint16_t msg_id,
                                           const uint8_t* data,
                                           size_t size,
                                           GuildLeaveRequest* out_request) {
    return DecodeTypedMessage<protocol::bindings::GuildLeaveReqBinding>(
        msg_id, kGuildLeaveRequestMsgId, data, size, out_request, ValidateGuildLeaveRequest,
        MessageCodecStatus::kInvalidPayload);
}

MessageCodecStatus DecodeGuildLeaveRequest(uint16_t msg_id,
                                           const std::vector<uint8_t>& payload,
                                           GuildLeaveRequest* out_request) {
    return DecodeGuildLeaveRequest(msg_id, payload.data(), payload.size(), out_request);
}

MessageCodecStatus DecodeGuildLeaveResponse(uint16_t msg_id,
                                            const uint8_t* data,
                                            size_t size,
                                            GuildLeaveResponse* out_response) {
    return DecodeTypedMessage<protocol::bindings::GuildLeaveRspBinding>(
        msg_id, kGuildLeaveResponseMsgId, data, size, out_response, ValidateGuildLeaveResponse,
        MessageCodecStatus::kInvalidPayload);
}

MessageCodecStatus DecodeGuildLeaveResponse(uint16_t msg_id,
                                            const std::vector<uint8_t>& payload,
                                            GuildLeaveResponse* out_response) {
    return DecodeGuildLeaveResponse(msg_id, payload.data(), payload.size(), out_response);
}

MessageCodecStatus DecodeGuildKickRequest(uint16_t msg_id,
                                          const uint8_t* data,
                                          size_t size,
                                          GuildKickRequest* out_request) {
    return DecodeTypedMessage<protocol::bindings::GuildKickReqBinding>(
        msg_id, kGuildKickRequestMsgId, data, size, out_request, ValidateGuildKickRequest,
        MessageCodecStatus::kInvalidPayload);
}

MessageCodecStatus DecodeGuildKickRequest(uint16_t msg_id,
                                          const std::vector<uint8_t>& payload,
                                          GuildKickRequest* out_request) {
    return DecodeGuildKickRequest(msg_id, payload.data(), payload.size(), out_request);
}

MessageCodecStatus DecodeGuildKickResponse(uint16_t msg_id,
                                           const uint8_t* data,
                                           size_t size,
                                           GuildKickResponse* out_response) {
    return DecodeTypedMessage<protocol::bindings::GuildKickRspBinding>(
        msg_id, kGuildKickResponseMsgId, data, size, out_response, ValidateGuildKickResponse,
        MessageCodecStatus::kInvalidPayload);
}

MessageCodecStatus DecodeGuildKickResponse(uint16_t msg_id,
                                           const std::vector<uint8_t>& payload,
                                           GuildKickResponse* out_response) {
    return DecodeGuildKickResponse(msg_id, payload.data(), payload.size(), out_response);
}

MessageCodecStatus DecodeGuildDeclareWarRequest(uint16_t msg_id,
                                                const uint8_t* data,
                                                size_t size,
                                                GuildDeclareWarRequest* out_request) {
    return DecodeTypedMessage<protocol::bindings::GuildDeclareWarReqBinding>(
        msg_id, kGuildDeclareWarRequestMsgId, data, size, out_request,
        ValidateGuildDeclareWarRequest, MessageCodecStatus::kInvalidPayload);
}

MessageCodecStatus DecodeGuildDeclareWarRequest(uint16_t msg_id,
                                                const std::vector<uint8_t>& payload,
                                                GuildDeclareWarRequest* out_request) {
    return DecodeGuildDeclareWarRequest(msg_id, payload.data(), payload.size(), out_request);
}

MessageCodecStatus DecodeGuildDeclareWarResponse(uint16_t msg_id,
                                                 const uint8_t* data,
                                                 size_t size,
                                                 GuildDeclareWarResponse* out_response) {
    return DecodeTypedMessage<protocol::bindings::GuildDeclareWarRspBinding>(
        msg_id, kGuildDeclareWarResponseMsgId, data, size, out_response,
        ValidateGuildDeclareWarResponse, MessageCodecStatus::kInvalidPayload);
}

MessageCodecStatus DecodeGuildDeclareWarResponse(uint16_t msg_id,
                                                 const std::vector<uint8_t>& payload,
                                                 GuildDeclareWarResponse* out_response) {
    return DecodeGuildDeclareWarResponse(msg_id, payload.data(), payload.size(), out_response);
}

MessageCodecStatus DecodeGuildCancelWarRequest(uint16_t msg_id,
                                               const uint8_t* data,
                                               size_t size,
                                               GuildCancelWarRequest* out_request) {
    return DecodeTypedMessage<protocol::bindings::GuildCancelWarReqBinding>(
        msg_id, kGuildCancelWarRequestMsgId, data, size, out_request,
        ValidateGuildCancelWarRequest, MessageCodecStatus::kInvalidPayload);
}

MessageCodecStatus DecodeGuildCancelWarRequest(uint16_t msg_id,
                                               const std::vector<uint8_t>& payload,
                                               GuildCancelWarRequest* out_request) {
    return DecodeGuildCancelWarRequest(msg_id, payload.data(), payload.size(), out_request);
}

MessageCodecStatus DecodeGuildCancelWarResponse(uint16_t msg_id,
                                                const uint8_t* data,
                                                size_t size,
                                                GuildCancelWarResponse* out_response) {
    return DecodeTypedMessage<protocol::bindings::GuildCancelWarRspBinding>(
        msg_id, kGuildCancelWarResponseMsgId, data, size, out_response,
        ValidateGuildCancelWarResponse, MessageCodecStatus::kInvalidPayload);
}

MessageCodecStatus DecodeGuildCancelWarResponse(uint16_t msg_id,
                                                const std::vector<uint8_t>& payload,
                                                GuildCancelWarResponse* out_response) {
    return DecodeGuildCancelWarResponse(msg_id, payload.data(), payload.size(), out_response);
}

MessageCodecStatus DecodeGuildMakeAllyRequest(uint16_t msg_id,
                                              const uint8_t* data,
                                              size_t size,
                                              GuildMakeAllyRequest* out_request) {
    return DecodeTypedMessage<protocol::bindings::GuildMakeAllyReqBinding>(
        msg_id, kGuildMakeAllyRequestMsgId, data, size, out_request,
        ValidateGuildMakeAllyRequest, MessageCodecStatus::kInvalidPayload);
}

MessageCodecStatus DecodeGuildMakeAllyRequest(uint16_t msg_id,
                                              const std::vector<uint8_t>& payload,
                                              GuildMakeAllyRequest* out_request) {
    return DecodeGuildMakeAllyRequest(msg_id, payload.data(), payload.size(), out_request);
}

MessageCodecStatus DecodeGuildMakeAllyResponse(uint16_t msg_id,
                                               const uint8_t* data,
                                               size_t size,
                                               GuildMakeAllyResponse* out_response) {
    return DecodeTypedMessage<protocol::bindings::GuildMakeAllyRspBinding>(
        msg_id, kGuildMakeAllyResponseMsgId, data, size, out_response,
        ValidateGuildMakeAllyResponse, MessageCodecStatus::kInvalidPayload);
}

MessageCodecStatus DecodeGuildMakeAllyResponse(uint16_t msg_id,
                                               const std::vector<uint8_t>& payload,
                                               GuildMakeAllyResponse* out_response) {
    return DecodeGuildMakeAllyResponse(msg_id, payload.data(), payload.size(), out_response);
}

MessageCodecStatus DecodeGuildBreakAllyRequest(uint16_t msg_id,
                                               const uint8_t* data,
                                               size_t size,
                                               GuildBreakAllyRequest* out_request) {
    return DecodeTypedMessage<protocol::bindings::GuildBreakAllyReqBinding>(
        msg_id, kGuildBreakAllyRequestMsgId, data, size, out_request,
        ValidateGuildBreakAllyRequest, MessageCodecStatus::kInvalidPayload);
}

MessageCodecStatus DecodeGuildBreakAllyRequest(uint16_t msg_id,
                                               const std::vector<uint8_t>& payload,
                                               GuildBreakAllyRequest* out_request) {
    return DecodeGuildBreakAllyRequest(msg_id, payload.data(), payload.size(), out_request);
}

MessageCodecStatus DecodeGuildBreakAllyResponse(uint16_t msg_id,
                                                const uint8_t* data,
                                                size_t size,
                                                GuildBreakAllyResponse* out_response) {
    return DecodeTypedMessage<protocol::bindings::GuildBreakAllyRspBinding>(
        msg_id, kGuildBreakAllyResponseMsgId, data, size, out_response,
        ValidateGuildBreakAllyResponse, MessageCodecStatus::kInvalidPayload);
}

MessageCodecStatus DecodeGuildBreakAllyResponse(uint16_t msg_id,
                                                const std::vector<uint8_t>& payload,
                                                GuildBreakAllyResponse* out_response) {
    return DecodeGuildBreakAllyResponse(msg_id, payload.data(), payload.size(), out_response);
}

MessageCodecStatus ValidateTradeRequest(const TradeRequest& request) {
    if (request.target_character_id == 0) {
        return MessageCodecStatus::kMissingField;
    }
    return MessageCodecStatus::kOk;
}

MessageCodecStatus ValidateTradeResponse(const TradeResponse& response) {
    if (response.success && response.trade_id == 0) {
        return MessageCodecStatus::kMissingField;
    }
    return MessageCodecStatus::kOk;
}

MessageCodecStatus ValidateTradeAddItemRequest(const TradeAddItemRequest& request) {
    if (request.trade_id == 0 || request.item_id == 0 || request.count == 0) {
        return MessageCodecStatus::kMissingField;
    }
    return MessageCodecStatus::kOk;
}

MessageCodecStatus ValidateTradeAddItemResponse(const TradeAddItemResponse&) {
    return MessageCodecStatus::kOk;
}

MessageCodecStatus ValidateTradeSetGoldRequest(const TradeSetGoldRequest& request) {
    if (request.trade_id == 0) {
        return MessageCodecStatus::kMissingField;
    }
    return MessageCodecStatus::kOk;
}

MessageCodecStatus ValidateTradeSetGoldResponse(const TradeSetGoldResponse&) {
    return MessageCodecStatus::kOk;
}

MessageCodecStatus ValidateTradeConfirmRequest(const TradeConfirmRequest& request) {
    if (request.trade_id == 0) {
        return MessageCodecStatus::kMissingField;
    }
    return MessageCodecStatus::kOk;
}

MessageCodecStatus ValidateTradeConfirmResponse(const TradeConfirmResponse&) {
    return MessageCodecStatus::kOk;
}

MessageCodecStatus ValidateTradeCancelRequest(const TradeCancelRequest& request) {
    if (request.trade_id == 0) {
        return MessageCodecStatus::kMissingField;
    }
    return MessageCodecStatus::kOk;
}

MessageCodecStatus ValidateTradeCancelResponse(const TradeCancelResponse&) {
    return MessageCodecStatus::kOk;
}

MessageCodecStatus ValidateTradeUpdateMessage(const TradeUpdateMessage& message) {
    if (message.trade_id == 0 || message.left_character_id == 0 || message.right_character_id == 0) {
        return MessageCodecStatus::kMissingField;
    }

    for (const auto& item : message.left_items) {
        if (item.item_id == 0 || item.count == 0) {
            return MessageCodecStatus::kMissingField;
        }
    }
    for (const auto& item : message.right_items) {
        if (item.item_id == 0 || item.count == 0) {
            return MessageCodecStatus::kMissingField;
        }
    }
    return MessageCodecStatus::kOk;
}

MessageCodecStatus ValidateTradeCompleteMessage(const TradeCompleteMessage& message) {
    if (message.trade_id == 0) {
        return MessageCodecStatus::kMissingField;
    }
    return MessageCodecStatus::kOk;
}

MessageCodecStatus ValidatePartyInviteRequest(const PartyInviteRequest& request) {
    if (request.target_character_id == 0) {
        return MessageCodecStatus::kMissingField;
    }
    return MessageCodecStatus::kOk;
}

MessageCodecStatus ValidatePartyInviteResponse(const PartyInviteResponse&) {
    return MessageCodecStatus::kOk;
}

MessageCodecStatus ValidatePartyJoinRequest(const PartyJoinRequest& request) {
    if (request.party_id == 0) {
        return MessageCodecStatus::kMissingField;
    }
    return MessageCodecStatus::kOk;
}

MessageCodecStatus ValidatePartyJoinResponse(const PartyJoinResponse&) {
    return MessageCodecStatus::kOk;
}

MessageCodecStatus ValidatePartyLeaveRequest(const PartyLeaveRequest&) {
    return MessageCodecStatus::kOk;
}

MessageCodecStatus ValidatePartyLeaveResponse(const PartyLeaveResponse&) {
    return MessageCodecStatus::kOk;
}

MessageCodecStatus ValidatePartyKickRequest(const PartyKickRequest& request) {
    if (request.target_character_id == 0) {
        return MessageCodecStatus::kMissingField;
    }
    return MessageCodecStatus::kOk;
}

MessageCodecStatus ValidatePartyKickResponse(const PartyKickResponse&) {
    return MessageCodecStatus::kOk;
}

MessageCodecStatus ValidatePartyUpdateMessage(const PartyUpdateMessage& message) {
    if (message.party_id == 0) {
        if (message.leader_character_id != 0 || !message.members.empty()) {
            return MessageCodecStatus::kValueOutOfRange;
        }
        return MessageCodecStatus::kOk;
    }

    if (message.leader_character_id == 0) {
        return MessageCodecStatus::kMissingField;
    }
    for (const auto& member : message.members) {
        if (member.character_id == 0) {
            return MessageCodecStatus::kMissingField;
        }
    }
    return MessageCodecStatus::kOk;
}

std::vector<uint8_t> EncodeTradeRequest(const TradeRequest& request,
                                        MessageCodecStatus* out_status) {
    return EncodeTypedMessage<protocol::bindings::TradeReqBinding>(
        request, out_status, ValidateTradeRequest);
}

std::vector<uint8_t> EncodeTradeResponse(const TradeResponse& response,
                                         MessageCodecStatus* out_status) {
    return EncodeTypedMessage<protocol::bindings::TradeRspBinding>(
        response, out_status, ValidateTradeResponse);
}

std::vector<uint8_t> EncodeTradeAddItemRequest(const TradeAddItemRequest& request,
                                               MessageCodecStatus* out_status) {
    return EncodeTypedMessage<protocol::bindings::TradeAddItemReqBinding>(
        request, out_status, ValidateTradeAddItemRequest);
}

std::vector<uint8_t> EncodeTradeAddItemResponse(const TradeAddItemResponse& response,
                                                MessageCodecStatus* out_status) {
    return EncodeTypedMessage<protocol::bindings::TradeAddItemRspBinding>(
        response, out_status, ValidateTradeAddItemResponse);
}

std::vector<uint8_t> EncodeTradeSetGoldRequest(const TradeSetGoldRequest& request,
                                               MessageCodecStatus* out_status) {
    return EncodeTypedMessage<protocol::bindings::TradeSetGoldReqBinding>(
        request, out_status, ValidateTradeSetGoldRequest);
}

std::vector<uint8_t> EncodeTradeSetGoldResponse(const TradeSetGoldResponse& response,
                                                MessageCodecStatus* out_status) {
    return EncodeTypedMessage<protocol::bindings::TradeSetGoldRspBinding>(
        response, out_status, ValidateTradeSetGoldResponse);
}

std::vector<uint8_t> EncodeTradeConfirmRequest(const TradeConfirmRequest& request,
                                               MessageCodecStatus* out_status) {
    return EncodeTypedMessage<protocol::bindings::TradeConfirmReqBinding>(
        request, out_status, ValidateTradeConfirmRequest);
}

std::vector<uint8_t> EncodeTradeConfirmResponse(const TradeConfirmResponse& response,
                                                MessageCodecStatus* out_status) {
    return EncodeTypedMessage<protocol::bindings::TradeConfirmRspBinding>(
        response, out_status, ValidateTradeConfirmResponse);
}

std::vector<uint8_t> EncodeTradeCancelRequest(const TradeCancelRequest& request,
                                              MessageCodecStatus* out_status) {
    return EncodeTypedMessage<protocol::bindings::TradeCancelReqBinding>(
        request, out_status, ValidateTradeCancelRequest);
}

std::vector<uint8_t> EncodeTradeCancelResponse(const TradeCancelResponse& response,
                                               MessageCodecStatus* out_status) {
    return EncodeTypedMessage<protocol::bindings::TradeCancelRspBinding>(
        response, out_status, ValidateTradeCancelResponse);
}

std::vector<uint8_t> EncodeTradeUpdateMessage(const TradeUpdateMessage& message,
                                              MessageCodecStatus* out_status) {
    return EncodeTypedMessage<protocol::bindings::TradeUpdateBinding>(
        message, out_status, ValidateTradeUpdateMessage);
}

std::vector<uint8_t> EncodeTradeCompleteMessage(const TradeCompleteMessage& message,
                                                MessageCodecStatus* out_status) {
    return EncodeTypedMessage<protocol::bindings::TradeCompleteBinding>(
        message, out_status, ValidateTradeCompleteMessage);
}

std::vector<uint8_t> EncodePartyInviteRequest(const PartyInviteRequest& request,
                                              MessageCodecStatus* out_status) {
    return EncodeTypedMessage<protocol::bindings::PartyInviteReqBinding>(
        request, out_status, ValidatePartyInviteRequest);
}

std::vector<uint8_t> EncodePartyInviteResponse(const PartyInviteResponse& response,
                                               MessageCodecStatus* out_status) {
    return EncodeTypedMessage<protocol::bindings::PartyInviteRspBinding>(
        response, out_status, ValidatePartyInviteResponse);
}

std::vector<uint8_t> EncodePartyJoinRequest(const PartyJoinRequest& request,
                                            MessageCodecStatus* out_status) {
    return EncodeTypedMessage<protocol::bindings::PartyJoinReqBinding>(
        request, out_status, ValidatePartyJoinRequest);
}

std::vector<uint8_t> EncodePartyJoinResponse(const PartyJoinResponse& response,
                                             MessageCodecStatus* out_status) {
    return EncodeTypedMessage<protocol::bindings::PartyJoinRspBinding>(
        response, out_status, ValidatePartyJoinResponse);
}

std::vector<uint8_t> EncodePartyLeaveRequest(const PartyLeaveRequest& request,
                                             MessageCodecStatus* out_status) {
    return EncodeTypedMessage<protocol::bindings::PartyLeaveReqBinding>(
        request, out_status, ValidatePartyLeaveRequest);
}

std::vector<uint8_t> EncodePartyLeaveResponse(const PartyLeaveResponse& response,
                                              MessageCodecStatus* out_status) {
    return EncodeTypedMessage<protocol::bindings::PartyLeaveRspBinding>(
        response, out_status, ValidatePartyLeaveResponse);
}

std::vector<uint8_t> EncodePartyKickRequest(const PartyKickRequest& request,
                                            MessageCodecStatus* out_status) {
    return EncodeTypedMessage<protocol::bindings::PartyKickReqBinding>(
        request, out_status, ValidatePartyKickRequest);
}

std::vector<uint8_t> EncodePartyKickResponse(const PartyKickResponse& response,
                                             MessageCodecStatus* out_status) {
    return EncodeTypedMessage<protocol::bindings::PartyKickRspBinding>(
        response, out_status, ValidatePartyKickResponse);
}

std::vector<uint8_t> EncodePartyUpdateMessage(const PartyUpdateMessage& message,
                                              MessageCodecStatus* out_status) {
    return EncodeTypedMessage<protocol::bindings::PartyUpdateBinding>(
        message, out_status, ValidatePartyUpdateMessage);
}

MessageCodecStatus DecodeTradeRequest(uint16_t msg_id,
                                      const uint8_t* data,
                                      size_t size,
                                      TradeRequest* out_request) {
    return DecodeTypedMessage<protocol::bindings::TradeReqBinding>(
        msg_id, kTradeRequestMsgId, data, size, out_request, ValidateTradeRequest,
        MessageCodecStatus::kInvalidPayload);
}

MessageCodecStatus DecodeTradeRequest(uint16_t msg_id,
                                      const std::vector<uint8_t>& payload,
                                      TradeRequest* out_request) {
    return DecodeTradeRequest(msg_id, payload.data(), payload.size(), out_request);
}

MessageCodecStatus DecodeTradeResponse(uint16_t msg_id,
                                       const uint8_t* data,
                                       size_t size,
                                       TradeResponse* out_response) {
    return DecodeTypedMessage<protocol::bindings::TradeRspBinding>(
        msg_id, kTradeResponseMsgId, data, size, out_response, ValidateTradeResponse,
        MessageCodecStatus::kInvalidPayload);
}

MessageCodecStatus DecodeTradeResponse(uint16_t msg_id,
                                       const std::vector<uint8_t>& payload,
                                       TradeResponse* out_response) {
    return DecodeTradeResponse(msg_id, payload.data(), payload.size(), out_response);
}

MessageCodecStatus DecodeTradeAddItemRequest(uint16_t msg_id,
                                             const uint8_t* data,
                                             size_t size,
                                             TradeAddItemRequest* out_request) {
    return DecodeTypedMessage<protocol::bindings::TradeAddItemReqBinding>(
        msg_id, kTradeAddItemRequestMsgId, data, size, out_request, ValidateTradeAddItemRequest,
        MessageCodecStatus::kInvalidPayload);
}

MessageCodecStatus DecodeTradeAddItemRequest(uint16_t msg_id,
                                             const std::vector<uint8_t>& payload,
                                             TradeAddItemRequest* out_request) {
    return DecodeTradeAddItemRequest(msg_id, payload.data(), payload.size(), out_request);
}

MessageCodecStatus DecodeTradeAddItemResponse(uint16_t msg_id,
                                              const uint8_t* data,
                                              size_t size,
                                              TradeAddItemResponse* out_response) {
    return DecodeTypedMessage<protocol::bindings::TradeAddItemRspBinding>(
        msg_id, kTradeAddItemResponseMsgId, data, size, out_response, ValidateTradeAddItemResponse,
        MessageCodecStatus::kInvalidPayload);
}

MessageCodecStatus DecodeTradeAddItemResponse(uint16_t msg_id,
                                              const std::vector<uint8_t>& payload,
                                              TradeAddItemResponse* out_response) {
    return DecodeTradeAddItemResponse(msg_id, payload.data(), payload.size(), out_response);
}

MessageCodecStatus DecodeTradeSetGoldRequest(uint16_t msg_id,
                                             const uint8_t* data,
                                             size_t size,
                                             TradeSetGoldRequest* out_request) {
    return DecodeTypedMessage<protocol::bindings::TradeSetGoldReqBinding>(
        msg_id, kTradeSetGoldRequestMsgId, data, size, out_request, ValidateTradeSetGoldRequest,
        MessageCodecStatus::kInvalidPayload);
}

MessageCodecStatus DecodeTradeSetGoldRequest(uint16_t msg_id,
                                             const std::vector<uint8_t>& payload,
                                             TradeSetGoldRequest* out_request) {
    return DecodeTradeSetGoldRequest(msg_id, payload.data(), payload.size(), out_request);
}

MessageCodecStatus DecodeTradeSetGoldResponse(uint16_t msg_id,
                                              const uint8_t* data,
                                              size_t size,
                                              TradeSetGoldResponse* out_response) {
    return DecodeTypedMessage<protocol::bindings::TradeSetGoldRspBinding>(
        msg_id, kTradeSetGoldResponseMsgId, data, size, out_response, ValidateTradeSetGoldResponse,
        MessageCodecStatus::kInvalidPayload);
}

MessageCodecStatus DecodeTradeSetGoldResponse(uint16_t msg_id,
                                              const std::vector<uint8_t>& payload,
                                              TradeSetGoldResponse* out_response) {
    return DecodeTradeSetGoldResponse(msg_id, payload.data(), payload.size(), out_response);
}

MessageCodecStatus DecodeTradeConfirmRequest(uint16_t msg_id,
                                             const uint8_t* data,
                                             size_t size,
                                             TradeConfirmRequest* out_request) {
    return DecodeTypedMessage<protocol::bindings::TradeConfirmReqBinding>(
        msg_id, kTradeConfirmRequestMsgId, data, size, out_request, ValidateTradeConfirmRequest,
        MessageCodecStatus::kInvalidPayload);
}

MessageCodecStatus DecodeTradeConfirmRequest(uint16_t msg_id,
                                             const std::vector<uint8_t>& payload,
                                             TradeConfirmRequest* out_request) {
    return DecodeTradeConfirmRequest(msg_id, payload.data(), payload.size(), out_request);
}

MessageCodecStatus DecodeTradeConfirmResponse(uint16_t msg_id,
                                              const uint8_t* data,
                                              size_t size,
                                              TradeConfirmResponse* out_response) {
    return DecodeTypedMessage<protocol::bindings::TradeConfirmRspBinding>(
        msg_id, kTradeConfirmResponseMsgId, data, size, out_response, ValidateTradeConfirmResponse,
        MessageCodecStatus::kInvalidPayload);
}

MessageCodecStatus DecodeTradeConfirmResponse(uint16_t msg_id,
                                              const std::vector<uint8_t>& payload,
                                              TradeConfirmResponse* out_response) {
    return DecodeTradeConfirmResponse(msg_id, payload.data(), payload.size(), out_response);
}

MessageCodecStatus DecodeTradeCancelRequest(uint16_t msg_id,
                                            const uint8_t* data,
                                            size_t size,
                                            TradeCancelRequest* out_request) {
    return DecodeTypedMessage<protocol::bindings::TradeCancelReqBinding>(
        msg_id, kTradeCancelRequestMsgId, data, size, out_request, ValidateTradeCancelRequest,
        MessageCodecStatus::kInvalidPayload);
}

MessageCodecStatus DecodeTradeCancelRequest(uint16_t msg_id,
                                            const std::vector<uint8_t>& payload,
                                            TradeCancelRequest* out_request) {
    return DecodeTradeCancelRequest(msg_id, payload.data(), payload.size(), out_request);
}

MessageCodecStatus DecodeTradeCancelResponse(uint16_t msg_id,
                                             const uint8_t* data,
                                             size_t size,
                                             TradeCancelResponse* out_response) {
    return DecodeTypedMessage<protocol::bindings::TradeCancelRspBinding>(
        msg_id, kTradeCancelResponseMsgId, data, size, out_response, ValidateTradeCancelResponse,
        MessageCodecStatus::kInvalidPayload);
}

MessageCodecStatus DecodeTradeCancelResponse(uint16_t msg_id,
                                             const std::vector<uint8_t>& payload,
                                             TradeCancelResponse* out_response) {
    return DecodeTradeCancelResponse(msg_id, payload.data(), payload.size(), out_response);
}

MessageCodecStatus DecodeTradeUpdateMessage(uint16_t msg_id,
                                            const uint8_t* data,
                                            size_t size,
                                            TradeUpdateMessage* out_message) {
    return DecodeTypedMessage<protocol::bindings::TradeUpdateBinding>(
        msg_id, kTradeUpdateMsgId, data, size, out_message, ValidateTradeUpdateMessage,
        MessageCodecStatus::kInvalidPayload);
}

MessageCodecStatus DecodeTradeUpdateMessage(uint16_t msg_id,
                                            const std::vector<uint8_t>& payload,
                                            TradeUpdateMessage* out_message) {
    return DecodeTradeUpdateMessage(msg_id, payload.data(), payload.size(), out_message);
}

MessageCodecStatus DecodeTradeCompleteMessage(uint16_t msg_id,
                                              const uint8_t* data,
                                              size_t size,
                                              TradeCompleteMessage* out_message) {
    return DecodeTypedMessage<protocol::bindings::TradeCompleteBinding>(
        msg_id, kTradeCompleteMsgId, data, size, out_message, ValidateTradeCompleteMessage,
        MessageCodecStatus::kInvalidPayload);
}

MessageCodecStatus DecodeTradeCompleteMessage(uint16_t msg_id,
                                              const std::vector<uint8_t>& payload,
                                              TradeCompleteMessage* out_message) {
    return DecodeTradeCompleteMessage(msg_id, payload.data(), payload.size(), out_message);
}

MessageCodecStatus DecodePartyInviteRequest(uint16_t msg_id,
                                            const uint8_t* data,
                                            size_t size,
                                            PartyInviteRequest* out_request) {
    return DecodeTypedMessage<protocol::bindings::PartyInviteReqBinding>(
        msg_id, kPartyInviteRequestMsgId, data, size, out_request, ValidatePartyInviteRequest,
        MessageCodecStatus::kInvalidPayload);
}

MessageCodecStatus DecodePartyInviteRequest(uint16_t msg_id,
                                            const std::vector<uint8_t>& payload,
                                            PartyInviteRequest* out_request) {
    return DecodePartyInviteRequest(msg_id, payload.data(), payload.size(), out_request);
}

MessageCodecStatus DecodePartyInviteResponse(uint16_t msg_id,
                                             const uint8_t* data,
                                             size_t size,
                                             PartyInviteResponse* out_response) {
    return DecodeTypedMessage<protocol::bindings::PartyInviteRspBinding>(
        msg_id, kPartyInviteResponseMsgId, data, size, out_response, ValidatePartyInviteResponse,
        MessageCodecStatus::kInvalidPayload);
}

MessageCodecStatus DecodePartyInviteResponse(uint16_t msg_id,
                                             const std::vector<uint8_t>& payload,
                                             PartyInviteResponse* out_response) {
    return DecodePartyInviteResponse(msg_id, payload.data(), payload.size(), out_response);
}

MessageCodecStatus DecodePartyJoinRequest(uint16_t msg_id,
                                          const uint8_t* data,
                                          size_t size,
                                          PartyJoinRequest* out_request) {
    return DecodeTypedMessage<protocol::bindings::PartyJoinReqBinding>(
        msg_id, kPartyJoinRequestMsgId, data, size, out_request, ValidatePartyJoinRequest,
        MessageCodecStatus::kInvalidPayload);
}

MessageCodecStatus DecodePartyJoinRequest(uint16_t msg_id,
                                          const std::vector<uint8_t>& payload,
                                          PartyJoinRequest* out_request) {
    return DecodePartyJoinRequest(msg_id, payload.data(), payload.size(), out_request);
}

MessageCodecStatus DecodePartyJoinResponse(uint16_t msg_id,
                                           const uint8_t* data,
                                           size_t size,
                                           PartyJoinResponse* out_response) {
    return DecodeTypedMessage<protocol::bindings::PartyJoinRspBinding>(
        msg_id, kPartyJoinResponseMsgId, data, size, out_response, ValidatePartyJoinResponse,
        MessageCodecStatus::kInvalidPayload);
}

MessageCodecStatus DecodePartyJoinResponse(uint16_t msg_id,
                                           const std::vector<uint8_t>& payload,
                                           PartyJoinResponse* out_response) {
    return DecodePartyJoinResponse(msg_id, payload.data(), payload.size(), out_response);
}

MessageCodecStatus DecodePartyLeaveRequest(uint16_t msg_id,
                                           const uint8_t* data,
                                           size_t size,
                                           PartyLeaveRequest* out_request) {
    return DecodeTypedMessage<protocol::bindings::PartyLeaveReqBinding>(
        msg_id, kPartyLeaveRequestMsgId, data, size, out_request, ValidatePartyLeaveRequest,
        MessageCodecStatus::kInvalidPayload);
}

MessageCodecStatus DecodePartyLeaveRequest(uint16_t msg_id,
                                           const std::vector<uint8_t>& payload,
                                           PartyLeaveRequest* out_request) {
    return DecodePartyLeaveRequest(msg_id, payload.data(), payload.size(), out_request);
}

MessageCodecStatus DecodePartyLeaveResponse(uint16_t msg_id,
                                            const uint8_t* data,
                                            size_t size,
                                            PartyLeaveResponse* out_response) {
    return DecodeTypedMessage<protocol::bindings::PartyLeaveRspBinding>(
        msg_id, kPartyLeaveResponseMsgId, data, size, out_response, ValidatePartyLeaveResponse,
        MessageCodecStatus::kInvalidPayload);
}

MessageCodecStatus DecodePartyLeaveResponse(uint16_t msg_id,
                                            const std::vector<uint8_t>& payload,
                                            PartyLeaveResponse* out_response) {
    return DecodePartyLeaveResponse(msg_id, payload.data(), payload.size(), out_response);
}

MessageCodecStatus DecodePartyKickRequest(uint16_t msg_id,
                                          const uint8_t* data,
                                          size_t size,
                                          PartyKickRequest* out_request) {
    return DecodeTypedMessage<protocol::bindings::PartyKickReqBinding>(
        msg_id, kPartyKickRequestMsgId, data, size, out_request, ValidatePartyKickRequest,
        MessageCodecStatus::kInvalidPayload);
}

MessageCodecStatus DecodePartyKickRequest(uint16_t msg_id,
                                          const std::vector<uint8_t>& payload,
                                          PartyKickRequest* out_request) {
    return DecodePartyKickRequest(msg_id, payload.data(), payload.size(), out_request);
}

MessageCodecStatus DecodePartyKickResponse(uint16_t msg_id,
                                           const uint8_t* data,
                                           size_t size,
                                           PartyKickResponse* out_response) {
    return DecodeTypedMessage<protocol::bindings::PartyKickRspBinding>(
        msg_id, kPartyKickResponseMsgId, data, size, out_response, ValidatePartyKickResponse,
        MessageCodecStatus::kInvalidPayload);
}

MessageCodecStatus DecodePartyKickResponse(uint16_t msg_id,
                                           const std::vector<uint8_t>& payload,
                                           PartyKickResponse* out_response) {
    return DecodePartyKickResponse(msg_id, payload.data(), payload.size(), out_response);
}

MessageCodecStatus DecodePartyUpdateMessage(uint16_t msg_id,
                                            const uint8_t* data,
                                            size_t size,
                                            PartyUpdateMessage* out_message) {
    return DecodeTypedMessage<protocol::bindings::PartyUpdateBinding>(
        msg_id, kPartyUpdateMsgId, data, size, out_message, ValidatePartyUpdateMessage,
        MessageCodecStatus::kInvalidPayload);
}

MessageCodecStatus DecodePartyUpdateMessage(uint16_t msg_id,
                                            const std::vector<uint8_t>& payload,
                                            PartyUpdateMessage* out_message) {
    return DecodePartyUpdateMessage(msg_id, payload.data(), payload.size(), out_message);
}

MessageCodecStatus ValidateGuildUpdateNoticeRequest(const GuildUpdateNoticeRequest& request) {
    for (const auto& line : request.notice_lines) {
        const auto status = ValidateOptionalString(line, kMaxChatContentLength);
        if (status != MessageCodecStatus::kOk) {
            return status;
        }
    }
    return MessageCodecStatus::kOk;
}

MessageCodecStatus ValidateGuildUpdateNoticeResponse(const GuildUpdateNoticeResponse&) {
    return MessageCodecStatus::kOk;
}

MessageCodecStatus ValidateGuildUpdateRankRequest(const GuildUpdateRankRequest& request) {
    for (const auto& member : request.members) {
        if (member.character_id == 0) {
            return MessageCodecStatus::kMissingField;
        }
    }
    return MessageCodecStatus::kOk;
}

MessageCodecStatus ValidateGuildUpdateRankResponse(const GuildUpdateRankResponse&) {
    return MessageCodecStatus::kOk;
}

MessageCodecStatus ValidateGuildInfoSyncMessage(const GuildInfoSyncMessage& message) {
    if (!message.has_guild) {
        return MessageCodecStatus::kOk;
    }
    if (message.guild_id == 0) {
        return MessageCodecStatus::kMissingField;
    }
    return MessageCodecStatus::kOk;
}

MessageCodecStatus ValidateRankingRequest(const RankingRequest& request) {
    auto status = ValidateEnumRange(static_cast<uint32_t>(request.ranking_type),
                                    static_cast<uint32_t>(mir2::proto::RankingType::GOLD));
    if (status != MessageCodecStatus::kOk) {
        return status;
    }
    if (request.page == 0 || request.page_size == 0) {
        return MessageCodecStatus::kValueOutOfRange;
    }
    return MessageCodecStatus::kOk;
}

MessageCodecStatus ValidateRankingResponse(const RankingResponse& response) {
    return ValidateEnumRange(static_cast<uint32_t>(response.ranking_type),
                             static_cast<uint32_t>(mir2::proto::RankingType::GOLD));
}

MessageCodecStatus ValidateRankingMyRankRequest(const RankingMyRankRequest& request) {
    return ValidateEnumRange(static_cast<uint32_t>(request.ranking_type),
                             static_cast<uint32_t>(mir2::proto::RankingType::GOLD));
}

MessageCodecStatus ValidateRankingMyRankResponse(const RankingMyRankResponse& response) {
    auto status = ValidateEnumRange(static_cast<uint32_t>(response.ranking_type),
                                    static_cast<uint32_t>(mir2::proto::RankingType::GOLD));
    if (status != MessageCodecStatus::kOk) {
        return status;
    }
    if (response.success && response.rank == 0) {
        return MessageCodecStatus::kMissingField;
    }
    return MessageCodecStatus::kOk;
}

MessageCodecStatus ValidateMailSendRequest(const MailSendRequest& request) {
    if (request.target_character_id == 0) {
        return MessageCodecStatus::kMissingField;
    }
    auto status = ValidateRequiredString(request.subject, 128);
    if (status != MessageCodecStatus::kOk) {
        return status;
    }
    status = ValidateRequiredString(request.content, 2048);
    if (status != MessageCodecStatus::kOk) {
        return status;
    }
    for (const auto& item : request.items) {
        if (item.item_id == 0 || item.count == 0) {
            return MessageCodecStatus::kMissingField;
        }
    }
    return MessageCodecStatus::kOk;
}

MessageCodecStatus ValidateMailSendResponse(const MailSendResponse& response) {
    if (response.success && response.mail_id == 0) {
        return MessageCodecStatus::kMissingField;
    }
    return MessageCodecStatus::kOk;
}

MessageCodecStatus ValidateMailListRequest(const MailListRequest&) {
    return MessageCodecStatus::kOk;
}

MessageCodecStatus ValidateMailListResponse(const MailListResponse& response) {
    for (const auto& mail : response.mails) {
        if (mail.mail_id == 0) {
            return MessageCodecStatus::kMissingField;
        }
    }
    return MessageCodecStatus::kOk;
}

MessageCodecStatus ValidateMailReadRequest(const MailReadRequest& request) {
    if (request.mail_id == 0) {
        return MessageCodecStatus::kMissingField;
    }
    return MessageCodecStatus::kOk;
}

MessageCodecStatus ValidateMailReadResponse(const MailReadResponse& response) {
    if (response.success) {
        if (!response.has_mail || response.mail.mail_id == 0) {
            return MessageCodecStatus::kMissingField;
        }
    }
    return MessageCodecStatus::kOk;
}

MessageCodecStatus ValidateMailDeleteRequest(const MailDeleteRequest& request) {
    if (request.mail_id == 0) {
        return MessageCodecStatus::kMissingField;
    }
    return MessageCodecStatus::kOk;
}

MessageCodecStatus ValidateMailDeleteResponse(const MailDeleteResponse& response) {
    if (response.success && response.mail_id == 0) {
        return MessageCodecStatus::kMissingField;
    }
    return MessageCodecStatus::kOk;
}

MessageCodecStatus ValidateMailClaimRequest(const MailClaimRequest& request) {
    if (request.mail_id == 0) {
        return MessageCodecStatus::kMissingField;
    }
    return MessageCodecStatus::kOk;
}

MessageCodecStatus ValidateMailClaimResponse(const MailClaimResponse& response) {
    if (response.success && response.mail_id == 0) {
        return MessageCodecStatus::kMissingField;
    }
    return MessageCodecStatus::kOk;
}

MessageCodecStatus ValidateMailNotifyMessage(const MailNotifyMessage& message) {
    if (message.has_mail && message.mail.mail_id == 0) {
        return MessageCodecStatus::kMissingField;
    }
    return MessageCodecStatus::kOk;
}

MessageCodecStatus ValidateAchievementListRequest(const AchievementListRequest&) {
    return MessageCodecStatus::kOk;
}

MessageCodecStatus ValidateAchievementListResponse(const AchievementListResponse& response) {
    for (const auto& item : response.achievements) {
        if (item.achievement_id == 0) {
            return MessageCodecStatus::kMissingField;
        }
    }
    return MessageCodecStatus::kOk;
}

MessageCodecStatus ValidateAchievementClaimRequest(const AchievementClaimRequest& request) {
    if (request.achievement_id == 0) {
        return MessageCodecStatus::kMissingField;
    }
    return MessageCodecStatus::kOk;
}

MessageCodecStatus ValidateAchievementClaimResponse(const AchievementClaimResponse& response) {
    if (response.success && response.achievement_id == 0) {
        return MessageCodecStatus::kMissingField;
    }
    return MessageCodecStatus::kOk;
}

MessageCodecStatus ValidateAchievementUpdateMessage(const AchievementUpdateMessage& message) {
    if (message.has_achievement && message.achievement.achievement_id == 0) {
        return MessageCodecStatus::kMissingField;
    }
    return MessageCodecStatus::kOk;
}

MessageCodecStatus ValidateAuctionListRequest(const AuctionListRequest& request) {
    if (request.page == 0 || request.page_size == 0) {
        return MessageCodecStatus::kValueOutOfRange;
    }
    return MessageCodecStatus::kOk;
}

MessageCodecStatus ValidateAuctionListResponse(const AuctionListResponse& response) {
    for (const auto& listing : response.listings) {
        if (listing.listing_id == 0) {
            return MessageCodecStatus::kMissingField;
        }
    }
    return MessageCodecStatus::kOk;
}

MessageCodecStatus ValidateAuctionSellRequest(const AuctionSellRequest& request) {
    if (request.item_id == 0 || request.count == 0 || request.unit_price == 0) {
        return MessageCodecStatus::kMissingField;
    }
    if (request.duration_sec == 0) {
        return MessageCodecStatus::kValueOutOfRange;
    }
    return MessageCodecStatus::kOk;
}

MessageCodecStatus ValidateAuctionSellResponse(const AuctionSellResponse& response) {
    if (response.success && response.listing_id == 0) {
        return MessageCodecStatus::kMissingField;
    }
    return MessageCodecStatus::kOk;
}

MessageCodecStatus ValidateAuctionBuyRequest(const AuctionBuyRequest& request) {
    if (request.listing_id == 0) {
        return MessageCodecStatus::kMissingField;
    }
    return MessageCodecStatus::kOk;
}

MessageCodecStatus ValidateAuctionBuyResponse(const AuctionBuyResponse& response) {
    if (response.success && response.listing_id == 0) {
        return MessageCodecStatus::kMissingField;
    }
    return MessageCodecStatus::kOk;
}

MessageCodecStatus ValidateAuctionCancelRequest(const AuctionCancelRequest& request) {
    if (request.listing_id == 0) {
        return MessageCodecStatus::kMissingField;
    }
    return MessageCodecStatus::kOk;
}

MessageCodecStatus ValidateAuctionCancelResponse(const AuctionCancelResponse& response) {
    if (response.success && response.listing_id == 0) {
        return MessageCodecStatus::kMissingField;
    }
    return MessageCodecStatus::kOk;
}

MessageCodecStatus ValidateAuctionNotifyMessage(const AuctionNotifyMessage& message) {
    auto status = ValidateEnumRange(static_cast<uint32_t>(message.notify_type),
                                    static_cast<uint32_t>(mir2::proto::AuctionNotifyType::BOUGHT));
    if (status != MessageCodecStatus::kOk) {
        return status;
    }
    if (message.has_listing && message.listing.listing_id == 0) {
        return MessageCodecStatus::kMissingField;
    }
    return MessageCodecStatus::kOk;
}

std::vector<uint8_t> EncodeGuildUpdateNoticeRequest(const GuildUpdateNoticeRequest& request,
                                                    MessageCodecStatus* out_status) {
    return EncodeTypedMessage<protocol::bindings::GuildUpdateNoticeReqBinding>(
        request, out_status, ValidateGuildUpdateNoticeRequest);
}

std::vector<uint8_t> EncodeGuildUpdateNoticeResponse(const GuildUpdateNoticeResponse& response,
                                                     MessageCodecStatus* out_status) {
    return EncodeTypedMessage<protocol::bindings::GuildUpdateNoticeRspBinding>(
        response, out_status, ValidateGuildUpdateNoticeResponse);
}

std::vector<uint8_t> EncodeGuildUpdateRankRequest(const GuildUpdateRankRequest& request,
                                                  MessageCodecStatus* out_status) {
    return EncodeTypedMessage<protocol::bindings::GuildUpdateRankReqBinding>(
        request, out_status, ValidateGuildUpdateRankRequest);
}

std::vector<uint8_t> EncodeGuildUpdateRankResponse(const GuildUpdateRankResponse& response,
                                                   MessageCodecStatus* out_status) {
    return EncodeTypedMessage<protocol::bindings::GuildUpdateRankRspBinding>(
        response, out_status, ValidateGuildUpdateRankResponse);
}

std::vector<uint8_t> EncodeGuildInfoSyncMessage(const GuildInfoSyncMessage& message,
                                                MessageCodecStatus* out_status) {
    return EncodeTypedMessage<protocol::bindings::GuildInfoSyncBinding>(
        message, out_status, ValidateGuildInfoSyncMessage);
}

std::vector<uint8_t> EncodeRankingRequest(const RankingRequest& request,
                                          MessageCodecStatus* out_status) {
    return EncodeTypedMessage<protocol::bindings::RankingReqBinding>(
        request, out_status, ValidateRankingRequest);
}

std::vector<uint8_t> EncodeRankingResponse(const RankingResponse& response,
                                           MessageCodecStatus* out_status) {
    return EncodeTypedMessage<protocol::bindings::RankingRspBinding>(
        response, out_status, ValidateRankingResponse);
}

std::vector<uint8_t> EncodeRankingMyRankRequest(const RankingMyRankRequest& request,
                                                MessageCodecStatus* out_status) {
    return EncodeTypedMessage<protocol::bindings::RankingMyRankReqBinding>(
        request, out_status, ValidateRankingMyRankRequest);
}

std::vector<uint8_t> EncodeRankingMyRankResponse(const RankingMyRankResponse& response,
                                                 MessageCodecStatus* out_status) {
    return EncodeTypedMessage<protocol::bindings::RankingMyRankRspBinding>(
        response, out_status, ValidateRankingMyRankResponse);
}

std::vector<uint8_t> EncodeMailSendRequest(const MailSendRequest& request,
                                           MessageCodecStatus* out_status) {
    return EncodeTypedMessage<protocol::bindings::MailSendReqBinding>(
        request, out_status, ValidateMailSendRequest);
}

std::vector<uint8_t> EncodeMailSendResponse(const MailSendResponse& response,
                                            MessageCodecStatus* out_status) {
    return EncodeTypedMessage<protocol::bindings::MailSendRspBinding>(
        response, out_status, ValidateMailSendResponse);
}

std::vector<uint8_t> EncodeMailListRequest(const MailListRequest& request,
                                           MessageCodecStatus* out_status) {
    return EncodeTypedMessage<protocol::bindings::MailListReqBinding>(
        request, out_status, ValidateMailListRequest);
}

std::vector<uint8_t> EncodeMailListResponse(const MailListResponse& response,
                                            MessageCodecStatus* out_status) {
    return EncodeTypedMessage<protocol::bindings::MailListRspBinding>(
        response, out_status, ValidateMailListResponse);
}

std::vector<uint8_t> EncodeMailReadRequest(const MailReadRequest& request,
                                           MessageCodecStatus* out_status) {
    return EncodeTypedMessage<protocol::bindings::MailReadReqBinding>(
        request, out_status, ValidateMailReadRequest);
}

std::vector<uint8_t> EncodeMailReadResponse(const MailReadResponse& response,
                                            MessageCodecStatus* out_status) {
    return EncodeTypedMessage<protocol::bindings::MailReadRspBinding>(
        response, out_status, ValidateMailReadResponse);
}

std::vector<uint8_t> EncodeMailDeleteRequest(const MailDeleteRequest& request,
                                             MessageCodecStatus* out_status) {
    return EncodeTypedMessage<protocol::bindings::MailDeleteReqBinding>(
        request, out_status, ValidateMailDeleteRequest);
}

std::vector<uint8_t> EncodeMailDeleteResponse(const MailDeleteResponse& response,
                                              MessageCodecStatus* out_status) {
    return EncodeTypedMessage<protocol::bindings::MailDeleteRspBinding>(
        response, out_status, ValidateMailDeleteResponse);
}

std::vector<uint8_t> EncodeMailClaimRequest(const MailClaimRequest& request,
                                            MessageCodecStatus* out_status) {
    return EncodeTypedMessage<protocol::bindings::MailClaimReqBinding>(
        request, out_status, ValidateMailClaimRequest);
}

std::vector<uint8_t> EncodeMailClaimResponse(const MailClaimResponse& response,
                                             MessageCodecStatus* out_status) {
    return EncodeTypedMessage<protocol::bindings::MailClaimRspBinding>(
        response, out_status, ValidateMailClaimResponse);
}

std::vector<uint8_t> EncodeMailNotifyMessage(const MailNotifyMessage& message,
                                             MessageCodecStatus* out_status) {
    return EncodeTypedMessage<protocol::bindings::MailNotifyBinding>(
        message, out_status, ValidateMailNotifyMessage);
}

std::vector<uint8_t> EncodeAchievementListRequest(const AchievementListRequest& request,
                                                  MessageCodecStatus* out_status) {
    return EncodeTypedMessage<protocol::bindings::AchievementListReqBinding>(
        request, out_status, ValidateAchievementListRequest);
}

std::vector<uint8_t> EncodeAchievementListResponse(const AchievementListResponse& response,
                                                   MessageCodecStatus* out_status) {
    return EncodeTypedMessage<protocol::bindings::AchievementListRspBinding>(
        response, out_status, ValidateAchievementListResponse);
}

std::vector<uint8_t> EncodeAchievementClaimRequest(const AchievementClaimRequest& request,
                                                   MessageCodecStatus* out_status) {
    return EncodeTypedMessage<protocol::bindings::AchievementClaimReqBinding>(
        request, out_status, ValidateAchievementClaimRequest);
}

std::vector<uint8_t> EncodeAchievementClaimResponse(const AchievementClaimResponse& response,
                                                    MessageCodecStatus* out_status) {
    return EncodeTypedMessage<protocol::bindings::AchievementClaimRspBinding>(
        response, out_status, ValidateAchievementClaimResponse);
}

std::vector<uint8_t> EncodeAchievementUpdateMessage(const AchievementUpdateMessage& message,
                                                    MessageCodecStatus* out_status) {
    return EncodeTypedMessage<protocol::bindings::AchievementUpdateBinding>(
        message, out_status, ValidateAchievementUpdateMessage);
}

std::vector<uint8_t> EncodeAuctionListRequest(const AuctionListRequest& request,
                                              MessageCodecStatus* out_status) {
    return EncodeTypedMessage<protocol::bindings::AuctionListReqBinding>(
        request, out_status, ValidateAuctionListRequest);
}

std::vector<uint8_t> EncodeAuctionListResponse(const AuctionListResponse& response,
                                               MessageCodecStatus* out_status) {
    return EncodeTypedMessage<protocol::bindings::AuctionListRspBinding>(
        response, out_status, ValidateAuctionListResponse);
}

std::vector<uint8_t> EncodeAuctionSellRequest(const AuctionSellRequest& request,
                                              MessageCodecStatus* out_status) {
    return EncodeTypedMessage<protocol::bindings::AuctionSellReqBinding>(
        request, out_status, ValidateAuctionSellRequest);
}

std::vector<uint8_t> EncodeAuctionSellResponse(const AuctionSellResponse& response,
                                               MessageCodecStatus* out_status) {
    return EncodeTypedMessage<protocol::bindings::AuctionSellRspBinding>(
        response, out_status, ValidateAuctionSellResponse);
}

std::vector<uint8_t> EncodeAuctionBuyRequest(const AuctionBuyRequest& request,
                                             MessageCodecStatus* out_status) {
    return EncodeTypedMessage<protocol::bindings::AuctionBuyReqBinding>(
        request, out_status, ValidateAuctionBuyRequest);
}

std::vector<uint8_t> EncodeAuctionBuyResponse(const AuctionBuyResponse& response,
                                              MessageCodecStatus* out_status) {
    return EncodeTypedMessage<protocol::bindings::AuctionBuyRspBinding>(
        response, out_status, ValidateAuctionBuyResponse);
}

std::vector<uint8_t> EncodeAuctionCancelRequest(const AuctionCancelRequest& request,
                                                MessageCodecStatus* out_status) {
    return EncodeTypedMessage<protocol::bindings::AuctionCancelReqBinding>(
        request, out_status, ValidateAuctionCancelRequest);
}

std::vector<uint8_t> EncodeAuctionCancelResponse(const AuctionCancelResponse& response,
                                                 MessageCodecStatus* out_status) {
    return EncodeTypedMessage<protocol::bindings::AuctionCancelRspBinding>(
        response, out_status, ValidateAuctionCancelResponse);
}

std::vector<uint8_t> EncodeAuctionNotifyMessage(const AuctionNotifyMessage& message,
                                                MessageCodecStatus* out_status) {
    return EncodeTypedMessage<protocol::bindings::AuctionNotifyBinding>(
        message, out_status, ValidateAuctionNotifyMessage);
}

MessageCodecStatus DecodeGuildUpdateNoticeRequest(uint16_t msg_id,
                                                  const uint8_t* data,
                                                  size_t size,
                                                  GuildUpdateNoticeRequest* out_request) {
    return DecodeTypedMessage<protocol::bindings::GuildUpdateNoticeReqBinding>(
        msg_id, kGuildUpdateNoticeRequestMsgId, data, size, out_request,
        ValidateGuildUpdateNoticeRequest, MessageCodecStatus::kInvalidPayload);
}

MessageCodecStatus DecodeGuildUpdateNoticeRequest(uint16_t msg_id,
                                                  const std::vector<uint8_t>& payload,
                                                  GuildUpdateNoticeRequest* out_request) {
    return DecodeGuildUpdateNoticeRequest(msg_id, payload.data(), payload.size(), out_request);
}

MessageCodecStatus DecodeGuildUpdateNoticeResponse(uint16_t msg_id,
                                                   const uint8_t* data,
                                                   size_t size,
                                                   GuildUpdateNoticeResponse* out_response) {
    return DecodeTypedMessage<protocol::bindings::GuildUpdateNoticeRspBinding>(
        msg_id, kGuildUpdateNoticeResponseMsgId, data, size, out_response,
        ValidateGuildUpdateNoticeResponse, MessageCodecStatus::kInvalidPayload);
}

MessageCodecStatus DecodeGuildUpdateNoticeResponse(uint16_t msg_id,
                                                   const std::vector<uint8_t>& payload,
                                                   GuildUpdateNoticeResponse* out_response) {
    return DecodeGuildUpdateNoticeResponse(msg_id, payload.data(), payload.size(), out_response);
}

MessageCodecStatus DecodeGuildUpdateRankRequest(uint16_t msg_id,
                                                const uint8_t* data,
                                                size_t size,
                                                GuildUpdateRankRequest* out_request) {
    return DecodeTypedMessage<protocol::bindings::GuildUpdateRankReqBinding>(
        msg_id, kGuildUpdateRankRequestMsgId, data, size, out_request,
        ValidateGuildUpdateRankRequest, MessageCodecStatus::kInvalidPayload);
}

MessageCodecStatus DecodeGuildUpdateRankRequest(uint16_t msg_id,
                                                const std::vector<uint8_t>& payload,
                                                GuildUpdateRankRequest* out_request) {
    return DecodeGuildUpdateRankRequest(msg_id, payload.data(), payload.size(), out_request);
}

MessageCodecStatus DecodeGuildUpdateRankResponse(uint16_t msg_id,
                                                 const uint8_t* data,
                                                 size_t size,
                                                 GuildUpdateRankResponse* out_response) {
    return DecodeTypedMessage<protocol::bindings::GuildUpdateRankRspBinding>(
        msg_id, kGuildUpdateRankResponseMsgId, data, size, out_response,
        ValidateGuildUpdateRankResponse, MessageCodecStatus::kInvalidPayload);
}

MessageCodecStatus DecodeGuildUpdateRankResponse(uint16_t msg_id,
                                                 const std::vector<uint8_t>& payload,
                                                 GuildUpdateRankResponse* out_response) {
    return DecodeGuildUpdateRankResponse(msg_id, payload.data(), payload.size(), out_response);
}

MessageCodecStatus DecodeGuildInfoSyncMessage(uint16_t msg_id,
                                              const uint8_t* data,
                                              size_t size,
                                              GuildInfoSyncMessage* out_message) {
    return DecodeTypedMessage<protocol::bindings::GuildInfoSyncBinding>(
        msg_id, kGuildInfoSyncMsgId, data, size, out_message, ValidateGuildInfoSyncMessage,
        MessageCodecStatus::kInvalidPayload);
}

MessageCodecStatus DecodeGuildInfoSyncMessage(uint16_t msg_id,
                                              const std::vector<uint8_t>& payload,
                                              GuildInfoSyncMessage* out_message) {
    return DecodeGuildInfoSyncMessage(msg_id, payload.data(), payload.size(), out_message);
}

MessageCodecStatus DecodeRankingRequest(uint16_t msg_id,
                                        const uint8_t* data,
                                        size_t size,
                                        RankingRequest* out_request) {
    return DecodeTypedMessage<protocol::bindings::RankingReqBinding>(
        msg_id, kRankingRequestMsgId, data, size, out_request, ValidateRankingRequest,
        MessageCodecStatus::kInvalidPayload);
}

MessageCodecStatus DecodeRankingRequest(uint16_t msg_id,
                                        const std::vector<uint8_t>& payload,
                                        RankingRequest* out_request) {
    return DecodeRankingRequest(msg_id, payload.data(), payload.size(), out_request);
}

MessageCodecStatus DecodeRankingResponse(uint16_t msg_id,
                                         const uint8_t* data,
                                         size_t size,
                                         RankingResponse* out_response) {
    return DecodeTypedMessage<protocol::bindings::RankingRspBinding>(
        msg_id, kRankingResponseMsgId, data, size, out_response, ValidateRankingResponse,
        MessageCodecStatus::kInvalidPayload);
}

MessageCodecStatus DecodeRankingResponse(uint16_t msg_id,
                                         const std::vector<uint8_t>& payload,
                                         RankingResponse* out_response) {
    return DecodeRankingResponse(msg_id, payload.data(), payload.size(), out_response);
}

MessageCodecStatus DecodeRankingMyRankRequest(uint16_t msg_id,
                                              const uint8_t* data,
                                              size_t size,
                                              RankingMyRankRequest* out_request) {
    return DecodeTypedMessage<protocol::bindings::RankingMyRankReqBinding>(
        msg_id, kRankingMyRankRequestMsgId, data, size, out_request, ValidateRankingMyRankRequest,
        MessageCodecStatus::kInvalidPayload);
}

MessageCodecStatus DecodeRankingMyRankRequest(uint16_t msg_id,
                                              const std::vector<uint8_t>& payload,
                                              RankingMyRankRequest* out_request) {
    return DecodeRankingMyRankRequest(msg_id, payload.data(), payload.size(), out_request);
}

MessageCodecStatus DecodeRankingMyRankResponse(uint16_t msg_id,
                                               const uint8_t* data,
                                               size_t size,
                                               RankingMyRankResponse* out_response) {
    return DecodeTypedMessage<protocol::bindings::RankingMyRankRspBinding>(
        msg_id, kRankingMyRankResponseMsgId, data, size, out_response,
        ValidateRankingMyRankResponse, MessageCodecStatus::kInvalidPayload);
}

MessageCodecStatus DecodeRankingMyRankResponse(uint16_t msg_id,
                                               const std::vector<uint8_t>& payload,
                                               RankingMyRankResponse* out_response) {
    return DecodeRankingMyRankResponse(msg_id, payload.data(), payload.size(), out_response);
}

MessageCodecStatus DecodeMailSendRequest(uint16_t msg_id,
                                         const uint8_t* data,
                                         size_t size,
                                         MailSendRequest* out_request) {
    return DecodeTypedMessage<protocol::bindings::MailSendReqBinding>(
        msg_id, kMailSendRequestMsgId, data, size, out_request, ValidateMailSendRequest,
        MessageCodecStatus::kInvalidPayload);
}

MessageCodecStatus DecodeMailSendRequest(uint16_t msg_id,
                                         const std::vector<uint8_t>& payload,
                                         MailSendRequest* out_request) {
    return DecodeMailSendRequest(msg_id, payload.data(), payload.size(), out_request);
}

MessageCodecStatus DecodeMailSendResponse(uint16_t msg_id,
                                          const uint8_t* data,
                                          size_t size,
                                          MailSendResponse* out_response) {
    return DecodeTypedMessage<protocol::bindings::MailSendRspBinding>(
        msg_id, kMailSendResponseMsgId, data, size, out_response, ValidateMailSendResponse,
        MessageCodecStatus::kInvalidPayload);
}

MessageCodecStatus DecodeMailSendResponse(uint16_t msg_id,
                                          const std::vector<uint8_t>& payload,
                                          MailSendResponse* out_response) {
    return DecodeMailSendResponse(msg_id, payload.data(), payload.size(), out_response);
}

MessageCodecStatus DecodeMailListRequest(uint16_t msg_id,
                                         const uint8_t* data,
                                         size_t size,
                                         MailListRequest* out_request) {
    return DecodeTypedMessage<protocol::bindings::MailListReqBinding>(
        msg_id, kMailListRequestMsgId, data, size, out_request, ValidateMailListRequest,
        MessageCodecStatus::kInvalidPayload);
}

MessageCodecStatus DecodeMailListRequest(uint16_t msg_id,
                                         const std::vector<uint8_t>& payload,
                                         MailListRequest* out_request) {
    return DecodeMailListRequest(msg_id, payload.data(), payload.size(), out_request);
}

MessageCodecStatus DecodeMailListResponse(uint16_t msg_id,
                                          const uint8_t* data,
                                          size_t size,
                                          MailListResponse* out_response) {
    return DecodeTypedMessage<protocol::bindings::MailListRspBinding>(
        msg_id, kMailListResponseMsgId, data, size, out_response, ValidateMailListResponse,
        MessageCodecStatus::kInvalidPayload);
}

MessageCodecStatus DecodeMailListResponse(uint16_t msg_id,
                                          const std::vector<uint8_t>& payload,
                                          MailListResponse* out_response) {
    return DecodeMailListResponse(msg_id, payload.data(), payload.size(), out_response);
}

MessageCodecStatus DecodeMailReadRequest(uint16_t msg_id,
                                         const uint8_t* data,
                                         size_t size,
                                         MailReadRequest* out_request) {
    return DecodeTypedMessage<protocol::bindings::MailReadReqBinding>(
        msg_id, kMailReadRequestMsgId, data, size, out_request, ValidateMailReadRequest,
        MessageCodecStatus::kInvalidPayload);
}

MessageCodecStatus DecodeMailReadRequest(uint16_t msg_id,
                                         const std::vector<uint8_t>& payload,
                                         MailReadRequest* out_request) {
    return DecodeMailReadRequest(msg_id, payload.data(), payload.size(), out_request);
}

MessageCodecStatus DecodeMailReadResponse(uint16_t msg_id,
                                          const uint8_t* data,
                                          size_t size,
                                          MailReadResponse* out_response) {
    return DecodeTypedMessage<protocol::bindings::MailReadRspBinding>(
        msg_id, kMailReadResponseMsgId, data, size, out_response, ValidateMailReadResponse,
        MessageCodecStatus::kInvalidPayload);
}

MessageCodecStatus DecodeMailReadResponse(uint16_t msg_id,
                                          const std::vector<uint8_t>& payload,
                                          MailReadResponse* out_response) {
    return DecodeMailReadResponse(msg_id, payload.data(), payload.size(), out_response);
}

MessageCodecStatus DecodeMailDeleteRequest(uint16_t msg_id,
                                           const uint8_t* data,
                                           size_t size,
                                           MailDeleteRequest* out_request) {
    return DecodeTypedMessage<protocol::bindings::MailDeleteReqBinding>(
        msg_id, kMailDeleteRequestMsgId, data, size, out_request, ValidateMailDeleteRequest,
        MessageCodecStatus::kInvalidPayload);
}

MessageCodecStatus DecodeMailDeleteRequest(uint16_t msg_id,
                                           const std::vector<uint8_t>& payload,
                                           MailDeleteRequest* out_request) {
    return DecodeMailDeleteRequest(msg_id, payload.data(), payload.size(), out_request);
}

MessageCodecStatus DecodeMailDeleteResponse(uint16_t msg_id,
                                            const uint8_t* data,
                                            size_t size,
                                            MailDeleteResponse* out_response) {
    return DecodeTypedMessage<protocol::bindings::MailDeleteRspBinding>(
        msg_id, kMailDeleteResponseMsgId, data, size, out_response, ValidateMailDeleteResponse,
        MessageCodecStatus::kInvalidPayload);
}

MessageCodecStatus DecodeMailDeleteResponse(uint16_t msg_id,
                                            const std::vector<uint8_t>& payload,
                                            MailDeleteResponse* out_response) {
    return DecodeMailDeleteResponse(msg_id, payload.data(), payload.size(), out_response);
}

MessageCodecStatus DecodeMailClaimRequest(uint16_t msg_id,
                                          const uint8_t* data,
                                          size_t size,
                                          MailClaimRequest* out_request) {
    return DecodeTypedMessage<protocol::bindings::MailClaimReqBinding>(
        msg_id, kMailClaimRequestMsgId, data, size, out_request, ValidateMailClaimRequest,
        MessageCodecStatus::kInvalidPayload);
}

MessageCodecStatus DecodeMailClaimRequest(uint16_t msg_id,
                                          const std::vector<uint8_t>& payload,
                                          MailClaimRequest* out_request) {
    return DecodeMailClaimRequest(msg_id, payload.data(), payload.size(), out_request);
}

MessageCodecStatus DecodeMailClaimResponse(uint16_t msg_id,
                                           const uint8_t* data,
                                           size_t size,
                                           MailClaimResponse* out_response) {
    return DecodeTypedMessage<protocol::bindings::MailClaimRspBinding>(
        msg_id, kMailClaimResponseMsgId, data, size, out_response, ValidateMailClaimResponse,
        MessageCodecStatus::kInvalidPayload);
}

MessageCodecStatus DecodeMailClaimResponse(uint16_t msg_id,
                                           const std::vector<uint8_t>& payload,
                                           MailClaimResponse* out_response) {
    return DecodeMailClaimResponse(msg_id, payload.data(), payload.size(), out_response);
}

MessageCodecStatus DecodeMailNotifyMessage(uint16_t msg_id,
                                           const uint8_t* data,
                                           size_t size,
                                           MailNotifyMessage* out_message) {
    return DecodeTypedMessage<protocol::bindings::MailNotifyBinding>(
        msg_id, kMailNotifyMsgId, data, size, out_message, ValidateMailNotifyMessage,
        MessageCodecStatus::kInvalidPayload);
}

MessageCodecStatus DecodeMailNotifyMessage(uint16_t msg_id,
                                           const std::vector<uint8_t>& payload,
                                           MailNotifyMessage* out_message) {
    return DecodeMailNotifyMessage(msg_id, payload.data(), payload.size(), out_message);
}

MessageCodecStatus DecodeAchievementListRequest(uint16_t msg_id,
                                                const uint8_t* data,
                                                size_t size,
                                                AchievementListRequest* out_request) {
    return DecodeTypedMessage<protocol::bindings::AchievementListReqBinding>(
        msg_id, kAchievementListRequestMsgId, data, size, out_request,
        ValidateAchievementListRequest, MessageCodecStatus::kInvalidPayload);
}

MessageCodecStatus DecodeAchievementListRequest(uint16_t msg_id,
                                                const std::vector<uint8_t>& payload,
                                                AchievementListRequest* out_request) {
    return DecodeAchievementListRequest(msg_id, payload.data(), payload.size(), out_request);
}

MessageCodecStatus DecodeAchievementListResponse(uint16_t msg_id,
                                                 const uint8_t* data,
                                                 size_t size,
                                                 AchievementListResponse* out_response) {
    return DecodeTypedMessage<protocol::bindings::AchievementListRspBinding>(
        msg_id, kAchievementListResponseMsgId, data, size, out_response,
        ValidateAchievementListResponse, MessageCodecStatus::kInvalidPayload);
}

MessageCodecStatus DecodeAchievementListResponse(uint16_t msg_id,
                                                 const std::vector<uint8_t>& payload,
                                                 AchievementListResponse* out_response) {
    return DecodeAchievementListResponse(msg_id, payload.data(), payload.size(), out_response);
}

MessageCodecStatus DecodeAchievementClaimRequest(uint16_t msg_id,
                                                 const uint8_t* data,
                                                 size_t size,
                                                 AchievementClaimRequest* out_request) {
    return DecodeTypedMessage<protocol::bindings::AchievementClaimReqBinding>(
        msg_id, kAchievementClaimRequestMsgId, data, size, out_request,
        ValidateAchievementClaimRequest, MessageCodecStatus::kInvalidPayload);
}

MessageCodecStatus DecodeAchievementClaimRequest(uint16_t msg_id,
                                                 const std::vector<uint8_t>& payload,
                                                 AchievementClaimRequest* out_request) {
    return DecodeAchievementClaimRequest(msg_id, payload.data(), payload.size(), out_request);
}

MessageCodecStatus DecodeAchievementClaimResponse(uint16_t msg_id,
                                                  const uint8_t* data,
                                                  size_t size,
                                                  AchievementClaimResponse* out_response) {
    return DecodeTypedMessage<protocol::bindings::AchievementClaimRspBinding>(
        msg_id, kAchievementClaimResponseMsgId, data, size, out_response,
        ValidateAchievementClaimResponse, MessageCodecStatus::kInvalidPayload);
}

MessageCodecStatus DecodeAchievementClaimResponse(uint16_t msg_id,
                                                  const std::vector<uint8_t>& payload,
                                                  AchievementClaimResponse* out_response) {
    return DecodeAchievementClaimResponse(msg_id, payload.data(), payload.size(), out_response);
}

MessageCodecStatus DecodeAchievementUpdateMessage(uint16_t msg_id,
                                                  const uint8_t* data,
                                                  size_t size,
                                                  AchievementUpdateMessage* out_message) {
    return DecodeTypedMessage<protocol::bindings::AchievementUpdateBinding>(
        msg_id, kAchievementUpdateMsgId, data, size, out_message, ValidateAchievementUpdateMessage,
        MessageCodecStatus::kInvalidPayload);
}

MessageCodecStatus DecodeAchievementUpdateMessage(uint16_t msg_id,
                                                  const std::vector<uint8_t>& payload,
                                                  AchievementUpdateMessage* out_message) {
    return DecodeAchievementUpdateMessage(msg_id, payload.data(), payload.size(), out_message);
}

MessageCodecStatus DecodeAuctionListRequest(uint16_t msg_id,
                                            const uint8_t* data,
                                            size_t size,
                                            AuctionListRequest* out_request) {
    return DecodeTypedMessage<protocol::bindings::AuctionListReqBinding>(
        msg_id, kAuctionListRequestMsgId, data, size, out_request, ValidateAuctionListRequest,
        MessageCodecStatus::kInvalidPayload);
}

MessageCodecStatus DecodeAuctionListRequest(uint16_t msg_id,
                                            const std::vector<uint8_t>& payload,
                                            AuctionListRequest* out_request) {
    return DecodeAuctionListRequest(msg_id, payload.data(), payload.size(), out_request);
}

MessageCodecStatus DecodeAuctionListResponse(uint16_t msg_id,
                                             const uint8_t* data,
                                             size_t size,
                                             AuctionListResponse* out_response) {
    return DecodeTypedMessage<protocol::bindings::AuctionListRspBinding>(
        msg_id, kAuctionListResponseMsgId, data, size, out_response, ValidateAuctionListResponse,
        MessageCodecStatus::kInvalidPayload);
}

MessageCodecStatus DecodeAuctionListResponse(uint16_t msg_id,
                                             const std::vector<uint8_t>& payload,
                                             AuctionListResponse* out_response) {
    return DecodeAuctionListResponse(msg_id, payload.data(), payload.size(), out_response);
}

MessageCodecStatus DecodeAuctionSellRequest(uint16_t msg_id,
                                            const uint8_t* data,
                                            size_t size,
                                            AuctionSellRequest* out_request) {
    return DecodeTypedMessage<protocol::bindings::AuctionSellReqBinding>(
        msg_id, kAuctionSellRequestMsgId, data, size, out_request, ValidateAuctionSellRequest,
        MessageCodecStatus::kInvalidPayload);
}

MessageCodecStatus DecodeAuctionSellRequest(uint16_t msg_id,
                                            const std::vector<uint8_t>& payload,
                                            AuctionSellRequest* out_request) {
    return DecodeAuctionSellRequest(msg_id, payload.data(), payload.size(), out_request);
}

MessageCodecStatus DecodeAuctionSellResponse(uint16_t msg_id,
                                             const uint8_t* data,
                                             size_t size,
                                             AuctionSellResponse* out_response) {
    return DecodeTypedMessage<protocol::bindings::AuctionSellRspBinding>(
        msg_id, kAuctionSellResponseMsgId, data, size, out_response, ValidateAuctionSellResponse,
        MessageCodecStatus::kInvalidPayload);
}

MessageCodecStatus DecodeAuctionSellResponse(uint16_t msg_id,
                                             const std::vector<uint8_t>& payload,
                                             AuctionSellResponse* out_response) {
    return DecodeAuctionSellResponse(msg_id, payload.data(), payload.size(), out_response);
}

MessageCodecStatus DecodeAuctionBuyRequest(uint16_t msg_id,
                                           const uint8_t* data,
                                           size_t size,
                                           AuctionBuyRequest* out_request) {
    return DecodeTypedMessage<protocol::bindings::AuctionBuyReqBinding>(
        msg_id, kAuctionBuyRequestMsgId, data, size, out_request, ValidateAuctionBuyRequest,
        MessageCodecStatus::kInvalidPayload);
}

MessageCodecStatus DecodeAuctionBuyRequest(uint16_t msg_id,
                                           const std::vector<uint8_t>& payload,
                                           AuctionBuyRequest* out_request) {
    return DecodeAuctionBuyRequest(msg_id, payload.data(), payload.size(), out_request);
}

MessageCodecStatus DecodeAuctionBuyResponse(uint16_t msg_id,
                                            const uint8_t* data,
                                            size_t size,
                                            AuctionBuyResponse* out_response) {
    return DecodeTypedMessage<protocol::bindings::AuctionBuyRspBinding>(
        msg_id, kAuctionBuyResponseMsgId, data, size, out_response, ValidateAuctionBuyResponse,
        MessageCodecStatus::kInvalidPayload);
}

MessageCodecStatus DecodeAuctionBuyResponse(uint16_t msg_id,
                                            const std::vector<uint8_t>& payload,
                                            AuctionBuyResponse* out_response) {
    return DecodeAuctionBuyResponse(msg_id, payload.data(), payload.size(), out_response);
}

MessageCodecStatus DecodeAuctionCancelRequest(uint16_t msg_id,
                                              const uint8_t* data,
                                              size_t size,
                                              AuctionCancelRequest* out_request) {
    return DecodeTypedMessage<protocol::bindings::AuctionCancelReqBinding>(
        msg_id, kAuctionCancelRequestMsgId, data, size, out_request, ValidateAuctionCancelRequest,
        MessageCodecStatus::kInvalidPayload);
}

MessageCodecStatus DecodeAuctionCancelRequest(uint16_t msg_id,
                                              const std::vector<uint8_t>& payload,
                                              AuctionCancelRequest* out_request) {
    return DecodeAuctionCancelRequest(msg_id, payload.data(), payload.size(), out_request);
}

MessageCodecStatus DecodeAuctionCancelResponse(uint16_t msg_id,
                                               const uint8_t* data,
                                               size_t size,
                                               AuctionCancelResponse* out_response) {
    return DecodeTypedMessage<protocol::bindings::AuctionCancelRspBinding>(
        msg_id, kAuctionCancelResponseMsgId, data, size, out_response,
        ValidateAuctionCancelResponse, MessageCodecStatus::kInvalidPayload);
}

MessageCodecStatus DecodeAuctionCancelResponse(uint16_t msg_id,
                                               const std::vector<uint8_t>& payload,
                                               AuctionCancelResponse* out_response) {
    return DecodeAuctionCancelResponse(msg_id, payload.data(), payload.size(), out_response);
}

MessageCodecStatus DecodeAuctionNotifyMessage(uint16_t msg_id,
                                              const uint8_t* data,
                                              size_t size,
                                              AuctionNotifyMessage* out_message) {
    return DecodeTypedMessage<protocol::bindings::AuctionNotifyBinding>(
        msg_id, kAuctionNotifyMsgId, data, size, out_message, ValidateAuctionNotifyMessage,
        MessageCodecStatus::kInvalidPayload);
}

MessageCodecStatus DecodeAuctionNotifyMessage(uint16_t msg_id,
                                              const std::vector<uint8_t>& payload,
                                              AuctionNotifyMessage* out_message) {
    return DecodeAuctionNotifyMessage(msg_id, payload.data(), payload.size(), out_message);
}

}  // namespace mir2::common
