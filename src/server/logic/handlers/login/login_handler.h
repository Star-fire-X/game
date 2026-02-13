/**
 * @file login_handler.h
 * @brief Login handler for logic layer.
 */

#ifndef MIR2_LOGIC_HANDLERS_LOGIN_LOGIN_HANDLER_H_
#define MIR2_LOGIC_HANDLERS_LOGIN_LOGIN_HANDLER_H_

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

#include "common/types/error_codes.h"
#include "logic/handler_context.h"
#include "logic/task.h"
#include "security/rate_limiter.h"

namespace mir2::common {
struct LoginRequest;
}  // namespace mir2::common

namespace mir2::logic {

class CoroutineExecutor;
class ResponseSender;
class RoleStore;
class ClientRegistry;

struct LoginResult {
  mir2::common::ErrorCode code = mir2::common::ErrorCode::kOk;
  uint64_t account_id = 0;
  std::string token;
};

using LoginCallback = std::function<void(const LoginResult&)>;

class LoginService {
 public:
  virtual ~LoginService() = default;
  virtual void Login(const std::string& username,
                     const std::string& password,
                     LoginCallback callback) = 0;
};

class LoginHandler {
 public:
  LoginHandler(CoroutineExecutor& executor,
               ResponseSender& response_sender,
               LoginService& service,
               ClientRegistry& client_registry,
               RoleStore& role_store);

  Task<void> HandleMessage(HandlerContext ctx, const uint8_t* payload, size_t payload_size);

 private:
  Task<void> HandleLogin(HandlerContext ctx, const mir2::common::LoginRequest& req);
  Task<LoginResult> AwaitLogin(const std::string& username, const std::string& password);

  CoroutineExecutor& executor_;
  ResponseSender& response_sender_;
  LoginService& service_;
  ClientRegistry& client_registry_;
  RoleStore& role_store_;

  // Login rate limiter: 5 attempts per account, refills 1 per 12 seconds
  // (effectively ~5 attempts per minute)
  mir2::security::RateLimiter login_rate_limiter_{
      {.capacity = 5, .refill_rate = 1, .refill_interval_seconds = 12}};
};

}  // namespace mir2::logic

#endif  // MIR2_LOGIC_HANDLERS_LOGIN_LOGIN_HANDLER_H_
