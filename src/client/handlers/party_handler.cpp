#include "client/handlers/party_handler.h"

#include "common/enums.h"
#include "party_generated.h"

#include <flatbuffers/flatbuffers.h>

#include <utility>

namespace mir2::game::handlers {

namespace {

bool TryLockCallbackOwner(const PartyHandler::Callbacks& callbacks,
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

PartyHandler::PartyHandler(Callbacks callbacks)
    : callbacks_(std::move(callbacks)) {}

void PartyHandler::RegisterHandlers(mir2::client::INetworkManager& /*manager*/) {
    // Party handlers are bound per-instance to capture callbacks.
}

void PartyHandler::BindHandlers(mir2::client::INetworkManager& manager) {
    const auto weak_self = weak_from_this();

    auto bind = [&manager, &weak_self](mir2::common::MsgId msg_id,
                                       auto fn) {
        manager.register_handler(msg_id, [weak_self, fn](const NetworkPacket& packet) {
            if (auto self = weak_self.lock()) {
                (self.get()->*fn)(packet);
            }
        });
    };

    bind(mir2::common::MsgId::kPartyInviteRsp, &PartyHandler::HandleInviteResponse);
    bind(mir2::common::MsgId::kPartyJoinRsp, &PartyHandler::HandleJoinResponse);
    bind(mir2::common::MsgId::kPartyLeaveRsp, &PartyHandler::HandleLeaveResponse);
    bind(mir2::common::MsgId::kPartyKickRsp, &PartyHandler::HandleKickResponse);
    bind(mir2::common::MsgId::kPartyUpdate, &PartyHandler::HandlePartyUpdate);
}

void PartyHandler::HandleInviteResponse(const NetworkPacket& packet) {
    std::shared_ptr<void> owner_guard;
    if (!TryLockCallbackOwner(callbacks_, &owner_guard)) {
        return;
    }

    flatbuffers::Verifier verifier(packet.payload.data(), packet.payload.size());
    if (!verifier.VerifyBuffer<mir2::proto::PartyInviteRsp>(nullptr)) {
        if (callbacks_.on_parse_error) {
            callbacks_.on_parse_error("PartyInviteRsp verification failed");
        }
        return;
    }

    const auto* rsp = flatbuffers::GetRoot<mir2::proto::PartyInviteRsp>(packet.payload.data());
    if (rsp && callbacks_.on_invite_response) {
        callbacks_.on_invite_response(rsp->success(), ToProtoError(rsp->error_code()));
    }
}

void PartyHandler::HandleJoinResponse(const NetworkPacket& packet) {
    std::shared_ptr<void> owner_guard;
    if (!TryLockCallbackOwner(callbacks_, &owner_guard)) {
        return;
    }

    flatbuffers::Verifier verifier(packet.payload.data(), packet.payload.size());
    if (!verifier.VerifyBuffer<mir2::proto::PartyJoinRsp>(nullptr)) {
        if (callbacks_.on_parse_error) {
            callbacks_.on_parse_error("PartyJoinRsp verification failed");
        }
        return;
    }

    const auto* rsp = flatbuffers::GetRoot<mir2::proto::PartyJoinRsp>(packet.payload.data());
    if (rsp && callbacks_.on_join_response) {
        callbacks_.on_join_response(rsp->success(), ToProtoError(rsp->error_code()));
    }
}

void PartyHandler::HandleLeaveResponse(const NetworkPacket& packet) {
    std::shared_ptr<void> owner_guard;
    if (!TryLockCallbackOwner(callbacks_, &owner_guard)) {
        return;
    }

    flatbuffers::Verifier verifier(packet.payload.data(), packet.payload.size());
    if (!verifier.VerifyBuffer<mir2::proto::PartyLeaveRsp>(nullptr)) {
        if (callbacks_.on_parse_error) {
            callbacks_.on_parse_error("PartyLeaveRsp verification failed");
        }
        return;
    }

    const auto* rsp = flatbuffers::GetRoot<mir2::proto::PartyLeaveRsp>(packet.payload.data());
    if (rsp && callbacks_.on_leave_response) {
        callbacks_.on_leave_response(rsp->success(), ToProtoError(rsp->error_code()));
    }
}

void PartyHandler::HandleKickResponse(const NetworkPacket& packet) {
    std::shared_ptr<void> owner_guard;
    if (!TryLockCallbackOwner(callbacks_, &owner_guard)) {
        return;
    }

    flatbuffers::Verifier verifier(packet.payload.data(), packet.payload.size());
    if (!verifier.VerifyBuffer<mir2::proto::PartyKickRsp>(nullptr)) {
        if (callbacks_.on_parse_error) {
            callbacks_.on_parse_error("PartyKickRsp verification failed");
        }
        return;
    }

    const auto* rsp = flatbuffers::GetRoot<mir2::proto::PartyKickRsp>(packet.payload.data());
    if (rsp && callbacks_.on_kick_response) {
        callbacks_.on_kick_response(rsp->success(), ToProtoError(rsp->error_code()));
    }
}

void PartyHandler::HandlePartyUpdate(const NetworkPacket& packet) {
    std::shared_ptr<void> owner_guard;
    if (!TryLockCallbackOwner(callbacks_, &owner_guard)) {
        return;
    }

    flatbuffers::Verifier verifier(packet.payload.data(), packet.payload.size());
    if (!verifier.VerifyBuffer<mir2::proto::PartyUpdate>(nullptr)) {
        if (callbacks_.on_parse_error) {
            callbacks_.on_parse_error("PartyUpdate verification failed");
        }
        return;
    }

    const auto* update = flatbuffers::GetRoot<mir2::proto::PartyUpdate>(packet.payload.data());
    if (!update) {
        if (callbacks_.on_parse_error) {
            callbacks_.on_parse_error("PartyUpdate parse failed");
        }
        return;
    }

    if (callbacks_.on_party_update) {
        PartyUpdateData data;
        data.party_id = update->party_id();
        data.leader_character_id = update->leader_character_id();

        if (const auto* members = update->members()) {
            data.members.reserve(members->size());
            for (const auto* member : *members) {
                if (!member) {
                    continue;
                }
                PartyMemberData view;
                view.character_id = member->character_id();
                view.name = member->name() ? member->name()->str() : std::string{};
                view.hp = member->hp();
                view.max_hp = member->max_hp();
                view.map_id = member->map_id();
                view.x = member->x();
                view.y = member->y();
                view.online = member->online();
                data.members.push_back(std::move(view));
            }
        }

        callbacks_.on_party_update(data);
    }
}

void PartyHandler::SendInviteRequest(mir2::client::INetworkManager& manager,
                                     uint32_t target_character_id) {
    flatbuffers::FlatBufferBuilder builder;
    const auto req = mir2::proto::CreatePartyInviteReq(builder, target_character_id);
    builder.Finish(req);
    manager.send_message(mir2::common::MsgId::kPartyInviteReq, BuildPayload(builder));
}

void PartyHandler::SendJoinRequest(mir2::client::INetworkManager& manager,
                                   uint64_t party_id) {
    flatbuffers::FlatBufferBuilder builder;
    const auto req = mir2::proto::CreatePartyJoinReq(builder, party_id);
    builder.Finish(req);
    manager.send_message(mir2::common::MsgId::kPartyJoinReq, BuildPayload(builder));
}

void PartyHandler::SendLeaveRequest(mir2::client::INetworkManager& manager) {
    flatbuffers::FlatBufferBuilder builder;
    const auto req = mir2::proto::CreatePartyLeaveReq(builder);
    builder.Finish(req);
    manager.send_message(mir2::common::MsgId::kPartyLeaveReq, BuildPayload(builder));
}

void PartyHandler::SendKickRequest(mir2::client::INetworkManager& manager,
                                   uint32_t target_character_id) {
    flatbuffers::FlatBufferBuilder builder;
    const auto req = mir2::proto::CreatePartyKickReq(builder, target_character_id);
    builder.Finish(req);
    manager.send_message(mir2::common::MsgId::kPartyKickReq, BuildPayload(builder));
}

} // namespace mir2::game::handlers
