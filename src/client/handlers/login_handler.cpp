#include "client/handlers/login_handler.h"

#include "common/protocol/message_codec.h"
#include "common/types/error_codes.h"

namespace mir2::game::handlers {

namespace {
const char* proto_error_to_string(mir2::proto::ErrorCode code) {
    const auto raw_code = static_cast<uint16_t>(code);
    if (raw_code == static_cast<uint16_t>(mir2::common::ErrorCode::ACCOUNT_BANNED)) {
        return "账号已被封禁";
    }
    if (raw_code == static_cast<uint16_t>(mir2::common::ErrorCode::RATE_LIMITED)) {
        return "登录过于频繁，请稍后重试";
    }

    switch (code) {
        case mir2::proto::ErrorCode::ERR_OK: return "成功";
        case mir2::proto::ErrorCode::ERR_ACCOUNT_NOT_FOUND: return "账号不存在";
        case mir2::proto::ErrorCode::ERR_PASSWORD_WRONG: return "密码错误";
        case mir2::proto::ErrorCode::ERR_NAME_EXISTS: return "名称已存在";
        case mir2::proto::ErrorCode::ERR_TARGET_DEAD: return "目标已死亡";
        case mir2::proto::ErrorCode::ERR_SKILL_COOLDOWN: return "技能冷却中";
        case mir2::proto::ErrorCode::ERR_INVALID_ACTION: return "非法操作";
        case mir2::proto::ErrorCode::ERR_TARGET_NOT_FOUND: return "目标不存在";
        case mir2::proto::ErrorCode::ERR_TARGET_OUT_OF_RANGE: return "目标超出范围";
        case mir2::proto::ErrorCode::ERR_INSUFFICIENT_MP: return "魔法值不足";
        case mir2::proto::ErrorCode::ERR_KICK_ADMIN_MANUAL: return "账号已被封禁";
        case mir2::proto::ErrorCode::ERR_KICK_DUPLICATE_LOGIN: return "账号已在其他设备登录";
        case mir2::proto::ErrorCode::ERR_KICK_HEARTBEAT_TIMEOUT: return "连接已超时";
        default: return "未知错误";
    }
}

const char* login_decode_error(mir2::common::MessageCodecStatus status) {
    switch (status) {
        case mir2::common::MessageCodecStatus::kInvalidMsgId:
        case mir2::common::MessageCodecStatus::kInvalidPayload:
            return "登录响应无效";
        case mir2::common::MessageCodecStatus::kMissingField:
        case mir2::common::MessageCodecStatus::kStringTooLong:
        case mir2::common::MessageCodecStatus::kValueOutOfRange:
            return "登录响应解析失败";
        default:
            return "登录响应无效";
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
            callbacks_.on_login_failure("登录响应为空");
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
