#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "client/handlers/login_handler.h"
#include "common/enums.h"
#include "common/protocol/message_codec.h"

namespace {

using mir2::common::MsgId;
using mir2::common::NetworkPacket;
using mir2::game::handlers::LoginHandler;

NetworkPacket MakePacket(MsgId msg_id, std::vector<uint8_t> payload) {
    NetworkPacket packet;
    packet.msg_id = static_cast<uint16_t>(msg_id);
    packet.payload = std::move(payload);
    return packet;
}

} // namespace

TEST(LoginHandlerTest, LoginSuccessRequestsCharacterList) {
    uint64_t captured_account_id = 0;
    std::string captured_token;
    int request_calls = 0;
    int failure_calls = 0;

    LoginHandler::Callbacks callbacks;
    callbacks.on_login_success = [&](uint64_t account_id, const std::string& token) {
        captured_account_id = account_id;
        captured_token = token;
    };
    callbacks.request_character_list = [&]() { ++request_calls; };
    callbacks.on_login_failure = [&](const std::string&) { ++failure_calls; };

    LoginHandler handler(callbacks);

    mir2::common::LoginResponse response;
    response.code = mir2::proto::ErrorCode::ERR_OK;
    response.account_id = 42u;
    response.session_token = "token";
    mir2::common::MessageCodecStatus status = mir2::common::MessageCodecStatus::kOk;
    auto payload = mir2::common::EncodeLoginResponse(response, &status);
    ASSERT_EQ(status, mir2::common::MessageCodecStatus::kOk);

    handler.HandleLoginResponse(MakePacket(MsgId::kLoginRsp, std::move(payload)));

    EXPECT_EQ(captured_account_id, 42u);
    EXPECT_EQ(captured_token, "token");
    EXPECT_EQ(request_calls, 1);
    EXPECT_EQ(failure_calls, 0);
}

TEST(LoginHandlerTest, LoginFailureInvokesFailureCallback) {
    std::string captured_error;

    LoginHandler::Callbacks callbacks;
    callbacks.on_login_failure = [&](const std::string& error) { captured_error = error; };

    LoginHandler handler(callbacks);

    mir2::common::LoginResponse response;
    response.code = mir2::proto::ErrorCode::ERR_PASSWORD_WRONG;
    response.account_id = 0u;
    response.session_token.clear();
    mir2::common::MessageCodecStatus status = mir2::common::MessageCodecStatus::kOk;
    auto payload = mir2::common::EncodeLoginResponse(response, &status);
    ASSERT_EQ(status, mir2::common::MessageCodecStatus::kOk);

    handler.HandleLoginResponse(MakePacket(MsgId::kLoginRsp, std::move(payload)));

    EXPECT_EQ(captured_error, "Password incorrect");
}

TEST(LoginHandlerTest, LoginWrongMsgIdReportsError) {
    std::string captured_error;

    LoginHandler::Callbacks callbacks;
    callbacks.on_login_failure = [&](const std::string& error) { captured_error = error; };

    LoginHandler handler(callbacks);

    mir2::common::LoginResponse response;
    response.code = mir2::proto::ErrorCode::ERR_OK;
    response.account_id = 42u;
    response.session_token = "token";
    mir2::common::MessageCodecStatus status = mir2::common::MessageCodecStatus::kOk;
    auto payload = mir2::common::EncodeLoginResponse(response, &status);
    ASSERT_EQ(status, mir2::common::MessageCodecStatus::kOk);

    handler.HandleLoginResponse(MakePacket(MsgId::kMoveRsp, std::move(payload)));

    EXPECT_EQ(captured_error, "Invalid login response");
}

TEST(LoginHandlerTest, LoginTimeoutInvokesCallback) {
    int timeout_calls = 0;

    LoginHandler::Callbacks callbacks;
    callbacks.on_login_timeout = [&]() { ++timeout_calls; };

    LoginHandler handler(callbacks);
    handler.HandleLoginTimeout();

    EXPECT_EQ(timeout_calls, 1);
}

TEST(LoginHandlerTest, LoginEmptyPayloadReportsError) {
    std::string captured_error;

    LoginHandler::Callbacks callbacks;
    callbacks.on_login_failure = [&](const std::string& error) { captured_error = error; };

    LoginHandler handler(callbacks);

    handler.HandleLoginResponse(MakePacket(MsgId::kLoginRsp, {}));

    EXPECT_EQ(captured_error, "Empty login response");
}

TEST(LoginHandlerTest, LoginInvalidPayloadReportsError) {
    std::string captured_error;

    LoginHandler::Callbacks callbacks;
    callbacks.on_login_failure = [&](const std::string& error) { captured_error = error; };

    LoginHandler handler(callbacks);

    std::vector<uint8_t> bad_payload = {0x01, 0x02, 0x03};
    handler.HandleLoginResponse(MakePacket(MsgId::kLoginRsp, std::move(bad_payload)));

    EXPECT_EQ(captured_error, "Invalid login response");
}
