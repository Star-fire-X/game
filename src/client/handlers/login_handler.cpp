#include "client/handlers/login_handler.h"

#include "common/protocol/message_codec.h"

namespace mir2::game::handlers {

namespace {
const char* proto_error_to_string(mir2::proto::ErrorCode code) {
    switch (code) {
        case mir2::proto::ErrorCode::ERR_OK: return "OK";
        case mir2::proto::ErrorCode::ERR_ACCOUNT_NOT_FOUND: return "Account not found";
        case mir2::proto::ErrorCode::ERR_PASSWORD_WRONG: return "Password incorrect";
        case mir2::proto::ErrorCode::ERR_NAME_EXISTS: return "Name already exists";
        case mir2::proto::ErrorCode::ERR_TARGET_DEAD: return "Target dead";
        case mir2::proto::ErrorCode::ERR_SKILL_COOLDOWN: return "Skill cooldown";
        case mir2::proto::ErrorCode::ERR_INVALID_ACTION: return "Invalid action";
        case mir2::proto::ErrorCode::ERR_TARGET_NOT_FOUND: return "Target not found";
        case mir2::proto::ErrorCode::ERR_TARGET_OUT_OF_RANGE: return "Target out of range";
        case mir2::proto::ErrorCode::ERR_INSUFFICIENT_MP: return "Insufficient MP";
        default: return "Unknown error";
    }
}

const char* login_decode_error(mir2::common::MessageCodecStatus status) {
    switch (status) {
        case mir2::common::MessageCodecStatus::kInvalidMsgId:
        case mir2::common::MessageCodecStatus::kInvalidPayload:
            return "Invalid login response";
        case mir2::common::MessageCodecStatus::kMissingField:
        case mir2::common::MessageCodecStatus::kStringTooLong:
        case mir2::common::MessageCodecStatus::kValueOutOfRange:
            return "Login response parse failed";
        default:
            return "Invalid login response";
    }
}

}

LoginHandler* LoginHandler::instance_ = nullptr;

LoginHandler::LoginHandler(Callbacks callbacks)
    : callbacks_(std::move(callbacks)) {
    instance_ = this;
}

LoginHandler::~LoginHandler() {
    if (instance_ == this) {
        instance_ = nullptr;
    }
}

void LoginHandler::BindHandlers(mir2::client::INetworkManager& manager) {
    manager.register_handler(mir2::common::MsgId::kLoginRsp,
                             [this](const NetworkPacket& packet) {
                                 HandleLoginResponse(packet);
                             });
}

void LoginHandler::RegisterHandlers(mir2::client::INetworkManager& manager) {
    if (!instance_) {
        return;
    }
    instance_->BindHandlers(manager);
}

void LoginHandler::HandleLoginResponse(const NetworkPacket& packet) {
    if (packet.payload.empty()) {
        if (callbacks_.on_login_failure) {
            callbacks_.on_login_failure("Empty login response");
        }
        return;
    }

    mir2::common::LoginResponse response;
    const auto status = mir2::common::DecodeLoginResponse(packet.msg_id, packet.payload, &response);
    if (status != mir2::common::MessageCodecStatus::kOk) {
        if (callbacks_.on_login_failure) {
            callbacks_.on_login_failure(login_decode_error(status));
        }
        return;
    }

    if (response.code == mir2::proto::ErrorCode::ERR_OK) {
        const uint64_t account_id = response.account_id;
        const std::string token = response.session_token;
        if (callbacks_.on_login_success) {
            callbacks_.on_login_success(account_id, token);
        }
        if (callbacks_.request_character_list) {
            callbacks_.request_character_list();
        }
        return;
    }

    const std::string error = proto_error_to_string(response.code);
    if (callbacks_.on_login_failure) {
        callbacks_.on_login_failure(error);
    }
}

void LoginHandler::HandleLoginTimeout() {
    if (callbacks_.on_login_timeout) {
        callbacks_.on_login_timeout();
    }
}

} // namespace mir2::game::handlers
