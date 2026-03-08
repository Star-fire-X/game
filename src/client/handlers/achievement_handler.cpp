#include "client/handlers/achievement_handler.h"

#include "achievement_generated.h"
#include "common/enums.h"

#include <flatbuffers/flatbuffers.h>

#include <utility>

namespace mir2::game::handlers {

namespace {

bool TryLockCallbackOwner(const AchievementHandler::Callbacks& callbacks,
                          std::shared_ptr<void>* owner_guard) {
    if (!callbacks.owner.has_value()) {
        return true;
    }
    *owner_guard = callbacks.owner->lock();
    return static_cast<bool>(*owner_guard);
}

mir2::proto::ErrorCode ToProtoError(int error_code) {
    return static_cast<mir2::proto::ErrorCode>(static_cast<uint16_t>(error_code));
}

std::vector<uint8_t> BuildPayload(flatbuffers::FlatBufferBuilder& builder) {
    const uint8_t* data = builder.GetBufferPointer();
    return std::vector<uint8_t>(data, data + builder.GetSize());
}

} // namespace

AchievementHandler::AchievementHandler(Callbacks callbacks)
    : callbacks_(std::move(callbacks)) {}

void AchievementHandler::RegisterHandlers(mir2::client::INetworkManager& /*manager*/) {
    // Achievement handlers are bound per-instance to capture callbacks.
}

void AchievementHandler::BindHandlers(mir2::client::INetworkManager& manager) {
    const auto weak_self = weak_from_this();

    auto bind = [&manager, &weak_self](mir2::common::MsgId msg_id,
                                       auto fn) {
        manager.register_handler(msg_id, [weak_self, fn](const NetworkPacket& packet) {
            if (auto self = weak_self.lock()) {
                (self.get()->*fn)(packet);
            }
        });
    };

    bind(mir2::common::MsgId::kAchievementListRsp, &AchievementHandler::HandleListResponse);
    bind(mir2::common::MsgId::kAchievementClaimRsp, &AchievementHandler::HandleClaimResponse);
    bind(mir2::common::MsgId::kAchievementUpdate, &AchievementHandler::HandleUpdate);
}

void AchievementHandler::HandleListResponse(const NetworkPacket& packet) {
    std::shared_ptr<void> owner_guard;
    if (!TryLockCallbackOwner(callbacks_, &owner_guard)) {
        return;
    }

    flatbuffers::Verifier verifier(packet.payload.data(), packet.payload.size());
    if (!verifier.VerifyBuffer<mir2::proto::AchievementListRsp>(nullptr)) {
        if (callbacks_.on_parse_error) {
            callbacks_.on_parse_error("AchievementListRsp verification failed");
        }
        return;
    }

    const auto* rsp =
        flatbuffers::GetRoot<mir2::proto::AchievementListRsp>(packet.payload.data());
    if (!rsp) {
        if (callbacks_.on_parse_error) {
            callbacks_.on_parse_error("AchievementListRsp parse failed");
        }
        return;
    }

    if (callbacks_.on_list_response) {
        std::vector<AchievementProgressData> achievements;
        if (const auto* progresses = rsp->achievements()) {
            achievements.reserve(progresses->size());
            for (const auto* progress : *progresses) {
                if (!progress) {
                    continue;
                }
                achievements.push_back(BuildProgressView(progress));
            }
        }
        callbacks_.on_list_response(rsp->success(),
                                    ToProtoError(rsp->error_code()),
                                    achievements);
    }
}

void AchievementHandler::HandleClaimResponse(const NetworkPacket& packet) {
    std::shared_ptr<void> owner_guard;
    if (!TryLockCallbackOwner(callbacks_, &owner_guard)) {
        return;
    }

    flatbuffers::Verifier verifier(packet.payload.data(), packet.payload.size());
    if (!verifier.VerifyBuffer<mir2::proto::AchievementClaimRsp>(nullptr)) {
        if (callbacks_.on_parse_error) {
            callbacks_.on_parse_error("AchievementClaimRsp verification failed");
        }
        return;
    }

    const auto* rsp =
        flatbuffers::GetRoot<mir2::proto::AchievementClaimRsp>(packet.payload.data());
    if (rsp && callbacks_.on_claim_response) {
        callbacks_.on_claim_response(rsp->success(),
                                     ToProtoError(rsp->error_code()),
                                     rsp->achievement_id(),
                                     rsp->reward_gold());
    }
}

void AchievementHandler::HandleUpdate(const NetworkPacket& packet) {
    std::shared_ptr<void> owner_guard;
    if (!TryLockCallbackOwner(callbacks_, &owner_guard)) {
        return;
    }

    flatbuffers::Verifier verifier(packet.payload.data(), packet.payload.size());
    if (!verifier.VerifyBuffer<mir2::proto::AchievementUpdate>(nullptr)) {
        if (callbacks_.on_parse_error) {
            callbacks_.on_parse_error("AchievementUpdate verification failed");
        }
        return;
    }

    const auto* update =
        flatbuffers::GetRoot<mir2::proto::AchievementUpdate>(packet.payload.data());
    if (!update || !callbacks_.on_update || !update->achievement()) {
        return;
    }

    callbacks_.on_update(BuildProgressView(update->achievement()));
}

void AchievementHandler::SendListRequest(mir2::client::INetworkManager& manager) {
    flatbuffers::FlatBufferBuilder builder;
    const auto req = mir2::proto::CreateAchievementListReq(builder);
    builder.Finish(req);
    manager.send_message(mir2::common::MsgId::kAchievementListReq, BuildPayload(builder));
}

void AchievementHandler::SendClaimRequest(mir2::client::INetworkManager& manager,
                                          uint32_t achievement_id) {
    flatbuffers::FlatBufferBuilder builder;
    const auto req = mir2::proto::CreateAchievementClaimReq(builder, achievement_id);
    builder.Finish(req);
    manager.send_message(mir2::common::MsgId::kAchievementClaimReq, BuildPayload(builder));
}

AchievementProgressData AchievementHandler::BuildProgressView(
    const mir2::proto::AchievementProgress* progress) {
    AchievementProgressData data;
    if (!progress) {
        return data;
    }
    data.achievement_id = progress->achievement_id();
    data.progress = progress->progress();
    data.target = progress->target();
    data.completed = progress->completed();
    data.claimed = progress->claimed();
    data.completed_time = progress->completed_time();
    data.reward_gold = progress->reward_gold();
    return data;
}

} // namespace mir2::game::handlers
