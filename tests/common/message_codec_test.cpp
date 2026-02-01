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
