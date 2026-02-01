#include <gtest/gtest.h>

#include <flatbuffers/flatbuffers.h>

#include "server/common/error_codes.h"
#include "common/enums.h"
#include "common/protocol/message_codec.h"
#include "handlers/login/login_handler.h"
#include "login_generated.h"

namespace {

class StubLoginService : public legend2::handlers::LoginService {
public:
    legend2::handlers::LoginResult result;

    void Login(const std::string& /*username*/,
               const std::string& /*password*/,
               legend2::handlers::LoginCallback callback) override {
        if (callback) {
            callback(result);
        }
    }
};

std::vector<uint8_t> BuildLoginReq(const std::string& username, const std::string& password) {
    mir2::common::LoginRequest request;
    request.username = username;
    request.password = password;
    mir2::common::MessageCodecStatus status = mir2::common::MessageCodecStatus::kOk;
    auto payload = mir2::common::EncodeLoginRequest(request, &status);
    if (status != mir2::common::MessageCodecStatus::kOk) {
        return {};
    }
    return payload;
}

std::vector<uint8_t> BuildRawLoginReq(const std::string& username, const std::string& password) {
    flatbuffers::FlatBufferBuilder builder;
    const auto username_offset = builder.CreateString(username);
    const auto password_offset = builder.CreateString(password);
    const auto req = mir2::proto::CreateLoginReq(builder, username_offset, password_offset, 0);
    builder.Finish(req);
    const uint8_t* data = builder.GetBufferPointer();
    return std::vector<uint8_t>(data, data + builder.GetSize());
}

}  // namespace

TEST(LoginHandlerTest, ReturnsLoginResponseOnSuccess) {
    StubLoginService service;
    service.result.code = mir2::common::ErrorCode::kOk;
    service.result.account_id = 42;
    service.result.token = "token_42";

    legend2::handlers::LoginHandler handler(service);
    legend2::handlers::HandlerContext context;
    context.client_id = 100;

    const auto payload = BuildLoginReq("user", "pass");
    legend2::handlers::ResponseList responses;
    handler.Handle(context,
                   static_cast<uint16_t>(mir2::common::MsgId::kLoginReq),
                   payload,
                   [&responses](const legend2::handlers::ResponseList& rsp) { responses = rsp; });

    ASSERT_EQ(responses.size(), 1u);
    EXPECT_EQ(responses[0].client_id, 100u);
    EXPECT_EQ(responses[0].msg_id,
              static_cast<uint16_t>(mir2::common::MsgId::kLoginRsp));

    flatbuffers::Verifier verifier(responses[0].payload.data(), responses[0].payload.size());
    ASSERT_TRUE(verifier.VerifyBuffer<mir2::proto::LoginRsp>(nullptr));
    mir2::common::LoginResponse response;
    const auto status = mir2::common::DecodeLoginResponse(
        mir2::common::kLoginResponseMsgId, responses[0].payload, &response);
    ASSERT_EQ(status, mir2::common::MessageCodecStatus::kOk);
    EXPECT_EQ(static_cast<uint16_t>(response.code),
              static_cast<uint16_t>(mir2::common::ErrorCode::kOk));
    EXPECT_EQ(response.account_id, 42u);
    EXPECT_EQ(response.session_token, "token_42");
}

TEST(LoginHandlerTest, InvalidPayloadReturnsError) {
    StubLoginService service;
    legend2::handlers::LoginHandler handler(service);
    legend2::handlers::HandlerContext context;
    context.client_id = 100;

    legend2::handlers::ResponseList responses;
    handler.Handle(context,
                   static_cast<uint16_t>(mir2::common::MsgId::kLoginReq),
                   {},
                   [&responses](const legend2::handlers::ResponseList& rsp) { responses = rsp; });

    ASSERT_EQ(responses.size(), 1u);
    flatbuffers::Verifier verifier(responses[0].payload.data(), responses[0].payload.size());
    ASSERT_TRUE(verifier.VerifyBuffer<mir2::proto::LoginRsp>(nullptr));
    mir2::common::LoginResponse response;
    const auto status = mir2::common::DecodeLoginResponse(
        mir2::common::kLoginResponseMsgId, responses[0].payload, &response);
    ASSERT_EQ(status, mir2::common::MessageCodecStatus::kOk);
    EXPECT_EQ(static_cast<uint16_t>(response.code),
              static_cast<uint16_t>(mir2::common::ErrorCode::kInvalidAction));
}

TEST(LoginHandlerTest, EmptyUsernameReturnsError) {
    StubLoginService service;
    legend2::handlers::LoginHandler handler(service);
    legend2::handlers::HandlerContext context;
    context.client_id = 100;

    const auto payload = BuildRawLoginReq("", "pass");
    legend2::handlers::ResponseList responses;
    handler.Handle(context,
                   static_cast<uint16_t>(mir2::common::MsgId::kLoginReq),
                   payload,
                   [&responses](const legend2::handlers::ResponseList& rsp) { responses = rsp; });

    ASSERT_EQ(responses.size(), 1u);
    flatbuffers::Verifier verifier(responses[0].payload.data(), responses[0].payload.size());
    ASSERT_TRUE(verifier.VerifyBuffer<mir2::proto::LoginRsp>(nullptr));
    mir2::common::LoginResponse response;
    const auto status = mir2::common::DecodeLoginResponse(
        mir2::common::kLoginResponseMsgId, responses[0].payload, &response);
    ASSERT_EQ(status, mir2::common::MessageCodecStatus::kOk);
    EXPECT_EQ(static_cast<uint16_t>(response.code),
              static_cast<uint16_t>(mir2::common::ErrorCode::kInvalidAction));
}

TEST(LoginHandlerTest, OversizedUsernameReturnsError) {
    StubLoginService service;
    legend2::handlers::LoginHandler handler(service);
    legend2::handlers::HandlerContext context;
    context.client_id = 100;

    const std::string username(mir2::common::kMaxLoginUsernameLength + 1, 'u');
    const auto payload = BuildRawLoginReq(username, "pass");
    legend2::handlers::ResponseList responses;
    handler.Handle(context,
                   static_cast<uint16_t>(mir2::common::MsgId::kLoginReq),
                   payload,
                   [&responses](const legend2::handlers::ResponseList& rsp) { responses = rsp; });

    ASSERT_EQ(responses.size(), 1u);
    mir2::common::LoginResponse response;
    const auto status = mir2::common::DecodeLoginResponse(
        mir2::common::kLoginResponseMsgId, responses[0].payload, &response);
    ASSERT_EQ(status, mir2::common::MessageCodecStatus::kOk);
    EXPECT_EQ(static_cast<uint16_t>(response.code),
              static_cast<uint16_t>(mir2::common::ErrorCode::kInvalidAction));
}
