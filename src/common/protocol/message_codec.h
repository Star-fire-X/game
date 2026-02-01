/**
 * @file message_codec.h
 * @brief Shared FlatBuffers message codec helpers.
 */

#ifndef LEGEND2_COMMON_PROTOCOL_MESSAGE_CODEC_H
#define LEGEND2_COMMON_PROTOCOL_MESSAGE_CODEC_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "common/enums.h"
#include "combat_generated.h"
#include "game_generated.h"
#include "login_generated.h"

namespace mir2::common {

/**
 * @brief Encode/decode status for message helpers.
 */
enum class MessageCodecStatus : uint8_t {
    kOk = 0,
    kInvalidMsgId,
    kInvalidPayload,
    kMissingField,
    kStringTooLong,
    kValueOutOfRange
};

constexpr uint16_t kLoginRequestMsgId = static_cast<uint16_t>(MsgId::kLoginReq);
constexpr uint16_t kLoginResponseMsgId = static_cast<uint16_t>(MsgId::kLoginRsp);
constexpr uint16_t kCreateCharacterRequestMsgId = static_cast<uint16_t>(MsgId::kCreateRoleReq);
constexpr uint16_t kCreateCharacterResponseMsgId = static_cast<uint16_t>(MsgId::kCreateRoleRsp);
constexpr uint16_t kMoveRequestMsgId = static_cast<uint16_t>(MsgId::kMoveReq);
constexpr uint16_t kMoveResponseMsgId = static_cast<uint16_t>(MsgId::kMoveRsp);
constexpr uint16_t kAttackRequestMsgId = static_cast<uint16_t>(MsgId::kAttackReq);
constexpr uint16_t kAttackResponseMsgId = static_cast<uint16_t>(MsgId::kAttackRsp);
constexpr uint16_t kSkillRequestMsgId = static_cast<uint16_t>(MsgId::kSkillReq);

constexpr size_t kMaxLoginUsernameLength = 20;
constexpr size_t kMaxLoginPasswordLength = 20;
constexpr size_t kMaxLoginVersionLength = 32;
constexpr size_t kMaxCharacterNameLength = 12;

struct LoginRequest {
    std::string username;
    std::string password;
    std::string version;
};

struct LoginResponse {
    mir2::proto::ErrorCode code = mir2::proto::ErrorCode::ERR_UNKNOWN;
    uint64_t account_id = 0;
    std::string session_token;
};

struct CreateCharacterRequest {
    std::string name;
    mir2::proto::Profession profession = mir2::proto::Profession::NONE;
    mir2::proto::Gender gender = mir2::proto::Gender::MALE;
};

struct CreateCharacterResponse {
    mir2::proto::ErrorCode code = mir2::proto::ErrorCode::ERR_UNKNOWN;
    uint64_t player_id = 0;
};

struct MoveRequest {
    int32_t target_x = 0;
    int32_t target_y = 0;
};

struct MoveResponse {
    mir2::proto::ErrorCode code = mir2::proto::ErrorCode::ERR_UNKNOWN;
    int32_t x = 0;
    int32_t y = 0;
};

struct AttackRequest {
    uint64_t target_id = 0;
    mir2::proto::EntityType target_type = mir2::proto::EntityType::NONE;
};

struct AttackResponse {
    mir2::proto::ErrorCode code = mir2::proto::ErrorCode::ERR_UNKNOWN;
    uint64_t attacker_id = 0;
    uint64_t target_id = 0;
    int32_t damage = 0;
    int32_t target_hp = 0;
    bool target_dead = false;
};

struct SkillRequest {
    uint32_t skill_id = 0;
    uint64_t target_id = 0;
    mir2::proto::EntityType target_type = mir2::proto::EntityType::NONE;
};

MessageCodecStatus ValidateLoginRequest(const LoginRequest& request);
MessageCodecStatus ValidateLoginResponse(const LoginResponse& response);
MessageCodecStatus ValidateCreateCharacterRequest(const CreateCharacterRequest& request);
MessageCodecStatus ValidateCreateCharacterResponse(const CreateCharacterResponse& response);
MessageCodecStatus ValidateMoveRequest(const MoveRequest& request);
MessageCodecStatus ValidateMoveResponse(const MoveResponse& response);
MessageCodecStatus ValidateAttackRequest(const AttackRequest& request);
MessageCodecStatus ValidateAttackResponse(const AttackResponse& response);
MessageCodecStatus ValidateSkillRequest(const SkillRequest& request);

std::vector<uint8_t> EncodeLoginRequest(const LoginRequest& request,
                                        MessageCodecStatus* out_status = nullptr);
std::vector<uint8_t> EncodeLoginResponse(const LoginResponse& response,
                                         MessageCodecStatus* out_status = nullptr);
std::vector<uint8_t> EncodeCreateCharacterRequest(const CreateCharacterRequest& request,
                                                  MessageCodecStatus* out_status = nullptr);
std::vector<uint8_t> EncodeCreateCharacterResponse(const CreateCharacterResponse& response,
                                                   MessageCodecStatus* out_status = nullptr);
std::vector<uint8_t> EncodeMoveRequest(const MoveRequest& request,
                                       MessageCodecStatus* out_status = nullptr);
std::vector<uint8_t> EncodeMoveResponse(const MoveResponse& response,
                                        MessageCodecStatus* out_status = nullptr);
std::vector<uint8_t> EncodeAttackRequest(const AttackRequest& request,
                                         MessageCodecStatus* out_status = nullptr);
std::vector<uint8_t> EncodeAttackResponse(const AttackResponse& response,
                                          MessageCodecStatus* out_status = nullptr);
std::vector<uint8_t> EncodeSkillRequest(const SkillRequest& request,
                                        MessageCodecStatus* out_status = nullptr);

MessageCodecStatus DecodeLoginRequest(uint16_t msg_id,
                                      const uint8_t* data,
                                      size_t size,
                                      LoginRequest* out_request);
MessageCodecStatus DecodeLoginRequest(uint16_t msg_id,
                                      const std::vector<uint8_t>& payload,
                                      LoginRequest* out_request);
MessageCodecStatus DecodeLoginResponse(uint16_t msg_id,
                                       const uint8_t* data,
                                       size_t size,
                                       LoginResponse* out_response);
MessageCodecStatus DecodeLoginResponse(uint16_t msg_id,
                                       const std::vector<uint8_t>& payload,
                                       LoginResponse* out_response);

MessageCodecStatus DecodeCreateCharacterRequest(uint16_t msg_id,
                                                const uint8_t* data,
                                                size_t size,
                                                CreateCharacterRequest* out_request);
MessageCodecStatus DecodeCreateCharacterRequest(uint16_t msg_id,
                                                const std::vector<uint8_t>& payload,
                                                CreateCharacterRequest* out_request);
MessageCodecStatus DecodeCreateCharacterResponse(uint16_t msg_id,
                                                 const uint8_t* data,
                                                 size_t size,
                                                 CreateCharacterResponse* out_response);
MessageCodecStatus DecodeCreateCharacterResponse(uint16_t msg_id,
                                                 const std::vector<uint8_t>& payload,
                                                 CreateCharacterResponse* out_response);

MessageCodecStatus DecodeMoveRequest(uint16_t msg_id,
                                     const uint8_t* data,
                                     size_t size,
                                     MoveRequest* out_request);
MessageCodecStatus DecodeMoveRequest(uint16_t msg_id,
                                     const std::vector<uint8_t>& payload,
                                     MoveRequest* out_request);
MessageCodecStatus DecodeMoveResponse(uint16_t msg_id,
                                      const uint8_t* data,
                                      size_t size,
                                      MoveResponse* out_response);
MessageCodecStatus DecodeMoveResponse(uint16_t msg_id,
                                      const std::vector<uint8_t>& payload,
                                      MoveResponse* out_response);

MessageCodecStatus DecodeAttackRequest(uint16_t msg_id,
                                       const uint8_t* data,
                                       size_t size,
                                       AttackRequest* out_request);
MessageCodecStatus DecodeAttackRequest(uint16_t msg_id,
                                       const std::vector<uint8_t>& payload,
                                       AttackRequest* out_request);
MessageCodecStatus DecodeAttackResponse(uint16_t msg_id,
                                        const uint8_t* data,
                                        size_t size,
                                        AttackResponse* out_response);
MessageCodecStatus DecodeAttackResponse(uint16_t msg_id,
                                        const std::vector<uint8_t>& payload,
                                        AttackResponse* out_response);

}  // namespace mir2::common

#endif  // LEGEND2_COMMON_PROTOCOL_MESSAGE_CODEC_H
