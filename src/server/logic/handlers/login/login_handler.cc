#include "logic/handlers/login/login_handler.h"

#include <asio/post.hpp>

#include <atomic>
#include <coroutine>
#include <memory>
#include <mutex>
#include <optional>
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
  std::mutex mutex;
  std::optional<LoginResult> result;
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

          {
            std::lock_guard<std::mutex> lock(state->mutex);
            if (state->cancelled.load(std::memory_order_acquire)) {
              return;
            }
            state->result = result;
          }

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

    std::lock_guard<std::mutex> lock(state_->mutex);
    if (!state_->result.has_value()) {
      LoginResult result;
      result.code = mir2::common::ErrorCode::kUnknown;
      return result;
    }
    return std::move(*state_->result);
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
                           RoleStore& role_store)
    : executor_(executor),
      response_sender_(response_sender),
      service_(service),
      client_registry_(client_registry),
      role_store_(role_store) {}

Task<void> LoginHandler::HandleMessage(HandlerContext ctx,
                                       const uint8_t* payload,
                                       size_t payload_size) {
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
}

Task<void> LoginHandler::HandleLogin(HandlerContext ctx,
                                     const mir2::common::LoginRequest& req) {
  // Rate limit per username to prevent brute-force attacks
  if (!login_rate_limiter_.TryAcquire(req.username)) {
    SYSLOG_WARN("LoginHandler rate limited user={} client_id={}",
                req.username, ctx.client_id);
    auto payload = BuildLoginResponsePayload(
        mir2::common::ErrorCode::RATE_LIMITED, 0, "");
    co_await response_sender_.SendAsync(
        ctx.client_id,
        static_cast<uint16_t>(mir2::common::MsgId::kLoginRsp),
        std::move(payload));
    co_return;
  }

  const auto result = co_await AwaitLogin(req.username, req.password);
  if (result.code == mir2::common::ErrorCode::kOk) {
    client_registry_.Track(ctx.client_id);
    if (result.account_id != 0) {
      role_store_.BindClientAccount(ctx.client_id, result.account_id);
    } else {
      SYSLOG_WARN("LoginHandler success without account_id (client_id={})", ctx.client_id);
      role_store_.UnbindClient(ctx.client_id);
    }
  } else {
    role_store_.UnbindClient(ctx.client_id);
  }

  auto payload = BuildLoginResponsePayload(result.code, result.account_id, result.token);
  co_await response_sender_.SendAsync(
      ctx.client_id,
      static_cast<uint16_t>(mir2::common::MsgId::kLoginRsp),
      std::move(payload));

  SYSLOG_DEBUG("LoginHandler login user={} client_id={} code={}",
               req.username, ctx.client_id, static_cast<int>(result.code));
}

Task<LoginResult> LoginHandler::AwaitLogin(const std::string& username,
                                           const std::string& password) {
  co_return co_await LoginAwaiter(executor_, service_, username, password);
}

}  // namespace mir2::logic
