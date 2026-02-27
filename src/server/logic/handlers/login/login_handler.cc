#include "logic/handlers/login/login_handler.h"

#include <asio/post.hpp>

#include <atomic>
#include <coroutine>
#include <exception>
#include <memory>
#include <utility>
#include <vector>

#include "common/enums.h"
#include "common/protocol/message_codec.h"
#include "logic/services/client_registry.h"
#include "log/logger.h"
#include "logic/coroutine_executor.h"
#include "logic/handlers/handler_error_utils.h"
#include "logic/response_sender.h"
#include "logic/services/session_role_store.h"

namespace mir2::logic {

namespace {

std::vector<uint8_t> BuildLoginResponsePayload(mir2::common::ErrorCode code,
                                               uint64_t account_id,
                                               const std::string& token) {
  mir2::common::LoginResponse response;
  response.code = ToProtoError(code);
  response.account_id = account_id;
  response.session_token = token;

  mir2::common::MessageCodecStatus status = mir2::common::MessageCodecStatus::kOk;
  auto payload = mir2::common::EncodeLoginResponse(response, &status);
  if (status == mir2::common::MessageCodecStatus::kOk) {
    return payload;
  }

  response.code = mir2::proto::ErrorCode::ERR_UNKNOWN;
  response.account_id = 0;
  response.session_token.clear();
  return mir2::common::EncodeLoginResponse(response, nullptr);
}

struct LoginAwaitState {
  LoginAwaitState(CoroutineExecutor& executor,
                  std::coroutine_handle<> continuation)
      : executor(executor), continuation(continuation) {}

  CoroutineExecutor& executor;
  std::coroutine_handle<> continuation;
  std::shared_ptr<LoginResult> result;
  std::atomic<bool> completed{false};
  std::atomic<bool> cancelled{false};
};

class LoginAwaiter {
 public:
  LoginAwaiter(CoroutineExecutor& executor,
               LoginService& service,
               std::string username,
               std::string password)
      : executor_(executor),
        service_(service),
        username_(std::move(username)),
        password_(std::move(password)) {}

  ~LoginAwaiter() {
    if (state_) {
      state_->cancelled.store(true, std::memory_order_release);
    }
  }

  bool await_ready() const noexcept { return false; }

  void await_suspend(std::coroutine_handle<> handle) {
    state_ = std::make_shared<LoginAwaitState>(executor_, handle);
    service_.Login(
        username_, password_,
        [state = state_](const LoginResult& result) {
          if (state->completed.exchange(true)) {
            return;
          }

          if (state->cancelled.load(std::memory_order_acquire)) {
            return;
          }
          std::atomic_store_explicit(
              &state->result, std::make_shared<LoginResult>(result), std::memory_order_release);

          asio::post(state->executor.GetIoContext(),
                     [state]() {
                       if (state->cancelled.load(std::memory_order_acquire)) {
                         return;
                       }
                       state->continuation.resume();
                     });
        });
  }

  LoginResult await_resume() {
    if (!state_) {
      LoginResult result;
      result.code = mir2::common::ErrorCode::kUnknown;
      return result;
    }

    auto result = std::atomic_load_explicit(&state_->result, std::memory_order_acquire);
    if (!result) {
      LoginResult result;
      result.code = mir2::common::ErrorCode::kUnknown;
      return result;
    }
    return std::move(*result);
  }

 private:
  CoroutineExecutor& executor_;
  LoginService& service_;
  std::string username_;
  std::string password_;
  std::shared_ptr<LoginAwaitState> state_;
};

}  // namespace

LoginHandler::LoginHandler(CoroutineExecutor& executor,
                           ResponseSender& response_sender,
                           LoginService& service,
                           ClientRegistry& client_registry,
                           RoleStore& role_store,
                           mir2::security::RateLimiter::Config rate_limit_config)
    : executor_(executor),
      response_sender_(response_sender),
      service_(service),
      client_registry_(client_registry),
      role_store_(role_store),
      login_rate_limiter_(rate_limit_config) {}

Task<void> LoginHandler::HandleMessage(HandlerContext ctx,
                                       const uint8_t* payload,
                                       size_t payload_size) {
  bool send_fallback_unknown = false;

  try {
    if (!payload || payload_size == 0) {
      SYSLOG_WARN("LoginHandler ignored empty payload (client_id={})", ctx.client_id);
      auto error_payload = BuildLoginResponsePayload(mir2::common::ErrorCode::kInvalidAction, 0, "");
      co_await response_sender_.SendAsync(
          ctx.client_id,
          static_cast<uint16_t>(mir2::common::MsgId::kLoginRsp),
          std::move(error_payload));
      co_return;
    }

    mir2::common::LoginRequest request;
    const auto status = mir2::common::DecodeLoginRequest(
        static_cast<uint16_t>(mir2::common::MsgId::kLoginReq),
        payload,
        payload_size,
        &request);
    if (status != mir2::common::MessageCodecStatus::kOk) {
      SYSLOG_WARN("LoginHandler decode failed (client_id={}, status={})",
                  ctx.client_id, static_cast<int>(status));
      auto error_payload = BuildLoginResponsePayload(ToCommonError(status), 0, "");
      co_await response_sender_.SendAsync(
          ctx.client_id,
          static_cast<uint16_t>(mir2::common::MsgId::kLoginRsp),
          std::move(error_payload));
      co_return;
    }

    co_await HandleLogin(std::move(ctx), request);
  } catch (const std::exception& ex) {
    role_store_.UnbindClient(ctx.client_id);
    client_registry_.Remove(ctx.client_id);
    SYSLOG_ERROR("LoginHandler exception (client_id={}): {}", ctx.client_id, ex.what());
    send_fallback_unknown = true;
  } catch (...) {
    role_store_.UnbindClient(ctx.client_id);
    client_registry_.Remove(ctx.client_id);
    SYSLOG_ERROR("LoginHandler unknown exception (client_id={})", ctx.client_id);
    send_fallback_unknown = true;
  }

  if (send_fallback_unknown) {
    try {
      auto error_payload = BuildLoginResponsePayload(mir2::common::ErrorCode::kUnknown, 0, "");
      co_await response_sender_.SendAsync(
          ctx.client_id,
          static_cast<uint16_t>(mir2::common::MsgId::kLoginRsp),
          std::move(error_payload));
    } catch (const std::exception& send_ex) {
      SYSLOG_ERROR("LoginHandler failed to send fallback response (client_id={}): {}",
                   ctx.client_id, send_ex.what());
    } catch (...) {
      SYSLOG_ERROR("LoginHandler failed to send fallback response (client_id={})",
                   ctx.client_id);
    }
  }
}

Task<void> LoginHandler::HandleLogin(HandlerContext ctx,
                                     const mir2::common::LoginRequest& req) {
  // Rate limit per username to prevent brute-force attacks
  if (!login_rate_limiter_.TryAcquire(req.username)) {
    SYSLOG_WARN("LoginHandler rate limited (client_id={})", ctx.client_id);
    auto payload = BuildLoginResponsePayload(
        mir2::common::ErrorCode::RATE_LIMITED, 0, "");
    co_await response_sender_.SendAsync(
        ctx.client_id,
        static_cast<uint16_t>(mir2::common::MsgId::kLoginRsp),
        std::move(payload));
    co_return;
  }

  const auto result = co_await AwaitLogin(req.username, req.password);
  mir2::common::ErrorCode response_code = result.code;
  uint64_t response_account_id = result.account_id;
  std::string response_token = result.token;

  if (result.code == mir2::common::ErrorCode::kOk && result.account_id != 0) {
    client_registry_.Track(ctx.client_id);
    role_store_.BindClientAccount(ctx.client_id, result.account_id);
  } else {
    if (result.code == mir2::common::ErrorCode::kOk && result.account_id == 0) {
      SYSLOG_WARN("LoginHandler success without account_id (client_id={})", ctx.client_id);
      response_code = mir2::common::ErrorCode::kUnknown;
      response_account_id = 0;
      response_token.clear();
    }
    role_store_.UnbindClient(ctx.client_id);
    client_registry_.Remove(ctx.client_id);
  }

  auto payload = BuildLoginResponsePayload(response_code, response_account_id, response_token);
  co_await response_sender_.SendAsync(
      ctx.client_id,
      static_cast<uint16_t>(mir2::common::MsgId::kLoginRsp),
      std::move(payload));

  SYSLOG_DEBUG("LoginHandler login client_id={} account_id={} code={}",
               ctx.client_id, response_account_id, static_cast<int>(response_code));
}

Task<LoginResult> LoginHandler::AwaitLogin(const std::string& username,
                                           const std::string& password) {
  co_return co_await LoginAwaiter(executor_, service_, username, password);
}

}  // namespace mir2::logic
