#include "client/handlers/system_handler.h"

#include "system_generated.h"

#include <flatbuffers/flatbuffers.h>

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
}

SystemHandler* SystemHandler::instance_ = nullptr;

SystemHandler::SystemHandler(Callbacks callbacks)
    : callbacks_(std::move(callbacks)) {
    instance_ = this;
}

SystemHandler::~SystemHandler() {
    if (instance_ == this) {
        instance_ = nullptr;
    }
}

void SystemHandler::BindHandlers(mir2::client::INetworkManager& manager) {
    manager.register_handler(mir2::common::MsgId::kHeartbeatRsp,
                             [this](const NetworkPacket& packet) {
                                 HandleHeartbeatResponse(packet);
                             });
    manager.register_handler(mir2::common::MsgId::kServerNotice,
                             [this](const NetworkPacket& packet) {
                                 HandleServerNotice(packet);
                             });
    manager.register_handler(mir2::common::MsgId::kKick,
                             [this](const NetworkPacket& packet) {
                                 HandleKick(packet);
                             });
}

void SystemHandler::RegisterHandlers(mir2::client::INetworkManager& manager) {
    if (!instance_) {
        return;
    }
    instance_->BindHandlers(manager);
}

void SystemHandler::HandleHeartbeatResponse(const NetworkPacket& packet) {
    if (packet.payload.empty()) {
        return;
    }

    flatbuffers::Verifier verifier(packet.payload.data(), packet.payload.size());
    if (!verifier.VerifyBuffer<mir2::proto::HeartbeatRsp>(nullptr)) {
        return;
    }

    const auto* rsp = flatbuffers::GetRoot<mir2::proto::HeartbeatRsp>(packet.payload.data());
    if (!rsp) {
        return;
    }

    if (callbacks_.on_heartbeat_response) {
        callbacks_.on_heartbeat_response(rsp->seq(), rsp->server_time());
    }
}

void SystemHandler::HandleServerNotice(const NetworkPacket& packet) {
    if (packet.payload.empty()) {
        return;
    }

    flatbuffers::Verifier verifier(packet.payload.data(), packet.payload.size());
    if (!verifier.VerifyBuffer<mir2::proto::ServerNotice>(nullptr)) {
        return;
    }

    const auto* notice = flatbuffers::GetRoot<mir2::proto::ServerNotice>(packet.payload.data());
    if (!notice || !notice->message()) {
        return;
    }

    if (callbacks_.on_server_notice) {
        callbacks_.on_server_notice(notice->level(),
                                    notice->message()->str(),
                                    notice->timestamp());
    }
}

void SystemHandler::HandleKick(const NetworkPacket& packet) {
    if (packet.payload.empty()) {
        return;
    }

    flatbuffers::Verifier verifier(packet.payload.data(), packet.payload.size());
    if (!verifier.VerifyBuffer<mir2::proto::Kick>(nullptr)) {
        return;
    }

    const auto* kick = flatbuffers::GetRoot<mir2::proto::Kick>(packet.payload.data());
    if (!kick) {
        return;
    }

    const std::string message = kick->message() ? kick->message()->str() : "Kicked by server";
    if (callbacks_.on_kick) {
        callbacks_.on_kick(kick->reason(), proto_error_to_string(kick->reason()), message);
    }
}

} // namespace mir2::game::handlers
