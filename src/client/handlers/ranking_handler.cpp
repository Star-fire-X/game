#include "client/handlers/ranking_handler.h"

#include "common/enums.h"
#include "ranking_generated.h"

#include <flatbuffers/flatbuffers.h>

#include <utility>

namespace mir2::game::handlers {

namespace {

bool TryLockCallbackOwner(const RankingHandler::Callbacks& callbacks,
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

RankingHandler::RankingHandler(Callbacks callbacks)
    : callbacks_(std::move(callbacks)) {}

void RankingHandler::RegisterHandlers(mir2::client::INetworkManager& /*manager*/) {
    // Ranking handlers are bound per-instance to capture callbacks.
}

void RankingHandler::BindHandlers(mir2::client::INetworkManager& manager) {
    const auto weak_self = weak_from_this();

    auto bind = [&manager, &weak_self](mir2::common::MsgId msg_id,
                                       auto fn) {
        manager.register_handler(msg_id, [weak_self, fn](const NetworkPacket& packet) {
            if (auto self = weak_self.lock()) {
                (self.get()->*fn)(packet);
            }
        });
    };

    bind(mir2::common::MsgId::kRankingRsp, &RankingHandler::HandleRankingResponse);
    bind(mir2::common::MsgId::kRankingMyRankRsp, &RankingHandler::HandleMyRankResponse);
}

void RankingHandler::HandleRankingResponse(const NetworkPacket& packet) {
    std::shared_ptr<void> owner_guard;
    if (!TryLockCallbackOwner(callbacks_, &owner_guard)) {
        return;
    }

    flatbuffers::Verifier verifier(packet.payload.data(), packet.payload.size());
    if (!verifier.VerifyBuffer<mir2::proto::RankingRsp>(nullptr)) {
        if (callbacks_.on_parse_error) {
            callbacks_.on_parse_error("RankingRsp verification failed");
        }
        return;
    }

    const auto* rsp = flatbuffers::GetRoot<mir2::proto::RankingRsp>(packet.payload.data());
    if (!rsp) {
        if (callbacks_.on_parse_error) {
            callbacks_.on_parse_error("RankingRsp parse failed");
        }
        return;
    }

    if (callbacks_.on_ranking_response) {
        RankingResponseData response;
        response.ranking_type = rsp->ranking_type();
        response.total_count = rsp->total_count();

        if (const auto* entries = rsp->entries()) {
            response.entries.reserve(entries->size());
            for (const auto* entry : *entries) {
                if (!entry) {
                    continue;
                }
                RankingEntryData data;
                data.rank = entry->rank();
                data.entity_id = entry->entity_id();
                data.name = entry->name() ? entry->name()->str() : std::string{};
                data.value = entry->value();
                data.extra = entry->extra() ? entry->extra()->str() : std::string{};
                response.entries.push_back(std::move(data));
            }
        }

        callbacks_.on_ranking_response(rsp->success(),
                                       ToProtoError(rsp->error_code()),
                                       response);
    }
}

void RankingHandler::HandleMyRankResponse(const NetworkPacket& packet) {
    std::shared_ptr<void> owner_guard;
    if (!TryLockCallbackOwner(callbacks_, &owner_guard)) {
        return;
    }

    flatbuffers::Verifier verifier(packet.payload.data(), packet.payload.size());
    if (!verifier.VerifyBuffer<mir2::proto::RankingMyRankRsp>(nullptr)) {
        if (callbacks_.on_parse_error) {
            callbacks_.on_parse_error("RankingMyRankRsp verification failed");
        }
        return;
    }

    const auto* rsp = flatbuffers::GetRoot<mir2::proto::RankingMyRankRsp>(packet.payload.data());
    if (!rsp) {
        if (callbacks_.on_parse_error) {
            callbacks_.on_parse_error("RankingMyRankRsp parse failed");
        }
        return;
    }

    if (callbacks_.on_my_rank_response) {
        MyRankData my_rank;
        my_rank.ranking_type = rsp->ranking_type();
        my_rank.rank = rsp->rank();
        my_rank.value = rsp->value();
        callbacks_.on_my_rank_response(rsp->success(),
                                       ToProtoError(rsp->error_code()),
                                       my_rank);
    }
}

void RankingHandler::SendRankingRequest(mir2::client::INetworkManager& manager,
                                        mir2::proto::RankingType type,
                                        uint32_t page,
                                        uint32_t page_size) {
    flatbuffers::FlatBufferBuilder builder;
    const auto req = mir2::proto::CreateRankingReq(builder, type, page, page_size);
    builder.Finish(req);
    manager.send_message(mir2::common::MsgId::kRankingReq, BuildPayload(builder));
}

void RankingHandler::SendMyRankRequest(mir2::client::INetworkManager& manager,
                                       mir2::proto::RankingType type) {
    flatbuffers::FlatBufferBuilder builder;
    const auto req = mir2::proto::CreateRankingMyRankReq(builder, type);
    builder.Finish(req);
    manager.send_message(mir2::common::MsgId::kRankingMyRankReq, BuildPayload(builder));
}

} // namespace mir2::game::handlers
