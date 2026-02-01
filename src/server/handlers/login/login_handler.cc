#include "handlers/login/login_handler.h"

#include "common/protocol/message_codec.h"
#include "handlers/handler_utils.h"

namespace legend2::handlers {

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

}  // namespace

LoginHandler::LoginHandler(LoginService& service)
    : BaseHandler(mir2::log::LogCategory::kDatabase),
      service_(service) {}

void LoginHandler::DoHandle(const HandlerContext& context,
                            uint16_t msg_id,
                            const std::vector<uint8_t>& payload,
                            ResponseCallback callback) {
    mir2::common::LoginRequest request;
    const auto status = mir2::common::DecodeLoginRequest(msg_id, payload, &request);
    if (status != mir2::common::MessageCodecStatus::kOk) {
        OnError(context, msg_id, ToCommonError(status), std::move(callback));
        return;
    }

    LOG_DEBUG(log_category_, "Login request client_id={} username={}", context.client_id,
              request.username);

    const uint64_t client_id = context.client_id;
    service_.Login(request.username, request.password,
                   [client_id, callback = std::move(callback)](const LoginResult& result) mutable {
                       ResponseList responses;
                       responses.push_back({client_id,
                                            mir2::common::kLoginResponseMsgId,
                                            BuildLoginResponsePayload(result.code,
                                                                      result.account_id,
                                                                      result.token)});
                       if (callback) {
                           callback(responses);
                       }
                   });
}

void LoginHandler::OnError(const HandlerContext& context,
                           uint16_t msg_id,
                           mir2::common::ErrorCode error_code,
                           ResponseCallback callback) {
    BaseHandler::OnError(context, msg_id, error_code, callback);

    ResponseList responses;
    responses.push_back({context.client_id,
                         mir2::common::kLoginResponseMsgId,
                         BuildLoginResponsePayload(error_code, 0, "")});
    if (callback) {
        callback(responses);
    }
}

}  // namespace legend2::handlers
