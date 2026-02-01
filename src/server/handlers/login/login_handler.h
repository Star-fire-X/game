/**
 * @file login_handler.h
 * @brief 登录相关处理
 */

#ifndef LEGEND2_SERVER_HANDLERS_LOGIN_HANDLER_H
#define LEGEND2_SERVER_HANDLERS_LOGIN_HANDLER_H

#include <functional>
#include <string>

#include "handlers/base_handler.h"

namespace legend2::handlers {

/**
 * @brief 登录结果
 */
struct LoginResult {
    mir2::common::ErrorCode code = mir2::common::ErrorCode::kOk;
    uint64_t account_id = 0;
    std::string token;
};

using LoginCallback = std::function<void(const LoginResult&)>;

/**
 * @brief 登录服务接口
 */
class LoginService {
public:
    virtual ~LoginService() = default;
    virtual void Login(const std::string& username,
                       const std::string& password,
                       LoginCallback callback) = 0;
};

/**
 * @brief 登录Handler
 */
class LoginHandler : public BaseHandler {
public:
    explicit LoginHandler(LoginService& service);

protected:
    void DoHandle(const HandlerContext& context,
                  uint16_t msg_id,
                  const std::vector<uint8_t>& payload,
                  ResponseCallback callback) override;

    void OnError(const HandlerContext& context,
                 uint16_t msg_id,
                 mir2::common::ErrorCode error_code,
                 ResponseCallback callback) override;

private:
    LoginService& service_;
};

}  // namespace legend2::handlers

#endif  // LEGEND2_SERVER_HANDLERS_LOGIN_HANDLER_H
