/**
 * @file login_handler_test.cc
 * @brief Tests for logic LoginHandler coroutine behavior.
 */

#include <gtest/gtest.h>

#include <asio/io_context.hpp>
#include <asio/post.hpp>

#include <chrono>
#include <string>
#include <thread>
#include <vector>

#include "common/enums.h"
#include "common/protocol/message_codec.h"
#include "logic/services/client_registry.h"
#include "logic/services/session_role_store.h"
#include "logic/coroutine_executor.h"
#include "logic/handlers/login/login_handler.h"
#include "logic/mock_response_sender.h"

namespace mir2::logic::test {
namespace {

class MockLoginService : public mir2::logic::LoginService {
 public:
  explicit MockLoginService(asio::io_context& io_context)
      : io_context_(io_context) {}

  void SetResult(const mir2::logic::LoginResult& result) { result_ = result; }
  void SetAsync(bool async) { async_ = async; }
  bool WasCalled() const { return called_; }
  const std::string& LastUsername() const { return last_username_; }
  const std::string& LastPassword() const { return last_password_; }

  void Login(const std::string& username,
             const std::string& password,
             mir2::logic::LoginCallback callback) override {
    called_ = true;
    last_username_ = username;
    last_password_ = password;

    if (async_) {
      asio::post(io_context_, [callback, result = result_]() { callback(result); });
      return;
    }

    callback(result_);
  }

 private:
  asio::io_context& io_context_;
  mir2::logic::LoginResult result_{};
  bool async_ = true;
  bool called_ = false;
  std::string last_username_;
  std::string last_password_;
};

std::vector<uint8_t> BuildLoginReq(const std::string& username,
                                   const std::string& password,
                                   const std::string& version) {
  mir2::common::LoginRequest request;
  request.username = username;
  request.password = password;
  request.version = version;
  mir2::common::MessageCodecStatus status = mir2::common::MessageCodecStatus::kOk;
  auto payload = mir2::common::EncodeLoginRequest(request, &status);
  if (status != mir2::common::MessageCodecStatus::kOk) {
    return {};
  }
  return payload;
}

}  // namespace

class LoginHandlerTest : public ::testing::Test {
 protected:
  void SetUp() override {
    executor_ = std::make_unique<CoroutineExecutor>(io_context_, 1);
    response_sender_ = std::make_unique<MockResponseSender>();
    service_ = std::make_unique<MockLoginService>(io_context_);
    handler_ = std::make_unique<LoginHandler>(*executor_,
                                               *response_sender_,
                                               *service_,
                                               client_registry_,
                                               role_store_);
  }

  bool PumpIoUntil(size_t expected_responses, std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
      io_context_.restart();
      io_context_.poll();
      if (response_sender_->ResponseCount() >= expected_responses &&
          executor_->RunningCount() == 0) {
        return true;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    io_context_.restart();
    io_context_.poll();
    return response_sender_->ResponseCount() >= expected_responses;
  }

  asio::io_context io_context_;
  std::unique_ptr<CoroutineExecutor> executor_;
  std::unique_ptr<MockResponseSender> response_sender_;
  std::unique_ptr<MockLoginService> service_;
  std::unique_ptr<LoginHandler> handler_;
  ClientRegistry client_registry_;
  RoleStore role_store_;
};

// 登录成功返回 OK、账号与 token。
TEST_F(LoginHandlerTest, LoginSuccess) {
  mir2::logic::LoginResult result;
  result.code = mir2::common::ErrorCode::kOk;
  result.account_id = 9001;
  result.token = "token123";
  service_->SetResult(result);

  HandlerContext context;
  context.client_id = 7007;

  const auto payload = BuildLoginReq("user", "pass", "1.0");
  executor_->Spawn(handler_->HandleMessage(context, payload.data(), payload.size()));

  ASSERT_TRUE(PumpIoUntil(1, std::chrono::seconds(2)));
  ASSERT_EQ(response_sender_->ResponseCount(), 1u);

  const auto responses = response_sender_->GetCapturedResponses();
  EXPECT_EQ(responses[0].msg_id,
            static_cast<uint16_t>(mir2::common::MsgId::kLoginRsp));

  mir2::common::LoginResponse response;
  const auto status = mir2::common::DecodeLoginResponse(
      mir2::common::kLoginResponseMsgId, responses[0].payload, &response);
  ASSERT_EQ(status, mir2::common::MessageCodecStatus::kOk);
  EXPECT_EQ(response.code, mir2::proto::ErrorCode::ERR_OK);
  EXPECT_EQ(response.account_id, result.account_id);
  EXPECT_EQ(response.session_token, result.token);
  EXPECT_TRUE(client_registry_.Contains(context.client_id));
  const auto account_id = role_store_.GetAccountId(context.client_id);
  ASSERT_TRUE(account_id.has_value());
  EXPECT_EQ(*account_id, result.account_id);
  EXPECT_TRUE(service_->WasCalled());
  EXPECT_EQ(service_->LastUsername(), "user");
  EXPECT_EQ(service_->LastPassword(), "pass");
}

// 凭证错误应返回 ERR_PASSWORD_WRONG。
TEST_F(LoginHandlerTest, LoginInvalidCredentials) {
  mir2::logic::LoginResult result;
  result.code = mir2::common::ErrorCode::kPasswordWrong;
  result.account_id = 0;
  result.token = "";
  service_->SetResult(result);

  HandlerContext context;
  context.client_id = 8008;

  const auto payload = BuildLoginReq("user", "badpass", "1.0");
  executor_->Spawn(handler_->HandleMessage(context, payload.data(), payload.size()));

  ASSERT_TRUE(PumpIoUntil(1, std::chrono::seconds(2)));
  const auto responses = response_sender_->GetCapturedResponses();

  mir2::common::LoginResponse response;
  const auto status = mir2::common::DecodeLoginResponse(
      mir2::common::kLoginResponseMsgId, responses[0].payload, &response);
  ASSERT_EQ(status, mir2::common::MessageCodecStatus::kOk);
  EXPECT_EQ(response.code, mir2::proto::ErrorCode::ERR_PASSWORD_WRONG);
  EXPECT_FALSE(client_registry_.Contains(context.client_id));
  EXPECT_FALSE(role_store_.GetAccountId(context.client_id).has_value());
}

// 账号不存在应返回 ERR_ACCOUNT_NOT_FOUND。
TEST_F(LoginHandlerTest, LoginAccountNotFound) {
  mir2::logic::LoginResult result;
  result.code = mir2::common::ErrorCode::kAccountNotFound;
  service_->SetResult(result);

  HandlerContext context;
  context.client_id = 9009;

  const auto payload = BuildLoginReq("ghost", "pass", "1.0");
  executor_->Spawn(handler_->HandleMessage(context, payload.data(), payload.size()));

  ASSERT_TRUE(PumpIoUntil(1, std::chrono::seconds(2)));
  const auto responses = response_sender_->GetCapturedResponses();

  mir2::common::LoginResponse response;
  const auto status = mir2::common::DecodeLoginResponse(
      mir2::common::kLoginResponseMsgId, responses[0].payload, &response);
  ASSERT_EQ(status, mir2::common::MessageCodecStatus::kOk);
  EXPECT_EQ(response.code, mir2::proto::ErrorCode::ERR_ACCOUNT_NOT_FOUND);
  EXPECT_FALSE(client_registry_.Contains(context.client_id));
  EXPECT_FALSE(role_store_.GetAccountId(context.client_id).has_value());
}

// 被封禁账号应返回 ACCOUNT_BANNED(2009)。
TEST_F(LoginHandlerTest, LoginAccountBanned) {
  mir2::logic::LoginResult result;
  result.code = mir2::common::ErrorCode::ACCOUNT_BANNED;
  service_->SetResult(result);

  HandlerContext context;
  context.client_id = 9010;

  const auto payload = BuildLoginReq("banned", "pass", "1.0");
  executor_->Spawn(handler_->HandleMessage(context, payload.data(), payload.size()));

  ASSERT_TRUE(PumpIoUntil(1, std::chrono::seconds(2)));
  const auto responses = response_sender_->GetCapturedResponses();

  mir2::common::LoginResponse response;
  const auto status = mir2::common::DecodeLoginResponse(
      mir2::common::kLoginResponseMsgId, responses[0].payload, &response);
  ASSERT_EQ(status, mir2::common::MessageCodecStatus::kOk);
  EXPECT_EQ(static_cast<uint16_t>(response.code),
            static_cast<uint16_t>(mir2::common::ErrorCode::ACCOUNT_BANNED));
  EXPECT_FALSE(client_registry_.Contains(context.client_id));
  EXPECT_FALSE(role_store_.GetAccountId(context.client_id).has_value());
}

// 空载荷应直接返回 ERR_INVALID_ACTION，不触发 LoginService。
TEST_F(LoginHandlerTest, LoginEmptyPayload) {
  HandlerContext context;
  context.client_id = 10010;

  executor_->Spawn(handler_->HandleMessage(context, nullptr, 0));

  ASSERT_TRUE(PumpIoUntil(1, std::chrono::seconds(2)));
  const auto responses = response_sender_->GetCapturedResponses();

  mir2::common::LoginResponse response;
  const auto status = mir2::common::DecodeLoginResponse(
      mir2::common::kLoginResponseMsgId, responses[0].payload, &response);
  ASSERT_EQ(status, mir2::common::MessageCodecStatus::kOk);
  EXPECT_EQ(response.code, mir2::proto::ErrorCode::ERR_INVALID_ACTION);
  EXPECT_FALSE(client_registry_.Contains(context.client_id));
  EXPECT_FALSE(role_store_.GetAccountId(context.client_id).has_value());
  EXPECT_FALSE(service_->WasCalled());
}

// 损坏的 FlatBuffers 载荷应返回 ERR_INVALID_ACTION。
TEST_F(LoginHandlerTest, LoginMalformedPayload) {
  HandlerContext context;
  context.client_id = 11011;

  const std::vector<uint8_t> payload = {0x01, 0x02, 0x03};
  executor_->Spawn(handler_->HandleMessage(context, payload.data(), payload.size()));

  ASSERT_TRUE(PumpIoUntil(1, std::chrono::seconds(2)));
  const auto responses = response_sender_->GetCapturedResponses();

  mir2::common::LoginResponse response;
  const auto status = mir2::common::DecodeLoginResponse(
      mir2::common::kLoginResponseMsgId, responses[0].payload, &response);
  ASSERT_EQ(status, mir2::common::MessageCodecStatus::kOk);
  EXPECT_EQ(response.code, mir2::proto::ErrorCode::ERR_INVALID_ACTION);
  EXPECT_FALSE(client_registry_.Contains(context.client_id));
  EXPECT_FALSE(role_store_.GetAccountId(context.client_id).has_value());
  EXPECT_FALSE(service_->WasCalled());
}

}  // namespace mir2::logic::test
