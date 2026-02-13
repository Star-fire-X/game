#include "client/handlers/chat_handler.h"

#include "chat_generated.h"
#include "system_generated.h"
#include "common/enums.h"

#include <flatbuffers/flatbuffers.h>

namespace mir2::game::handlers {

namespace {

/// Convert proto ChatChannel enum to client ChatChannel enum.
ChatChannel ToClientChatChannel(mir2::proto::ChatChannel channel) {
    switch (channel) {
        case mir2::proto::ChatChannel::WORLD:
            return ChatChannel::kWorld;
        case mir2::proto::ChatChannel::PRIVATE:
            return ChatChannel::kPrivate;
        case mir2::proto::ChatChannel::GUILD:
            return ChatChannel::kGuild;
        case mir2::proto::ChatChannel::SYSTEM:
            return ChatChannel::kSystem;
        case mir2::proto::ChatChannel::TEAM:
            return ChatChannel::kTeam;
        case mir2::proto::ChatChannel::AREA:
            return ChatChannel::kArea;
        default:
            return ChatChannel::kWorld;
    }
}

/// Try to lock the callback owner. Returns true if no owner is set or the owner
/// is still alive (in which case *owner_guard holds the lock).
bool TryLockCallbackOwner(const ChatHandler::Callbacks& callbacks,
                          std::shared_ptr<void>* owner_guard) {
    if (!callbacks.owner.has_value()) {
        return true;
    }
    *owner_guard = callbacks.owner->lock();
    return static_cast<bool>(*owner_guard);
}

} // namespace

ChatHandler::ChatHandler(Callbacks callbacks)
    : callbacks_(std::move(callbacks)) {}

void ChatHandler::RegisterHandlers(mir2::client::INetworkManager& /*manager*/) {
    // Chat handlers are bound per-instance to capture callbacks.
}

void ChatHandler::BindHandlers(mir2::client::INetworkManager& manager) {
    const auto weak_self = weak_from_this();

    // kChatRsp (5002) -- acknowledgement for a sent chat message.
    manager.register_handler(mir2::common::MsgId::kChatRsp,
                             [weak_self](const NetworkPacket& packet) {
                                 if (auto self = weak_self.lock()) {
                                     self->HandleChatResponse(packet);
                                 }
                             });

    // kPrivateChat (5010) -- incoming private/whisper message.
    manager.register_handler(mir2::common::MsgId::kPrivateChat,
                             [weak_self](const NetworkPacket& packet) {
                                 if (auto self = weak_self.lock()) {
                                     self->HandleChatMessage(packet);
                                 }
                             });

    // kGuildChat (5020) -- incoming guild chat message.
    manager.register_handler(mir2::common::MsgId::kGuildChat,
                             [weak_self](const NetworkPacket& packet) {
                                 if (auto self = weak_self.lock()) {
                                     self->HandleChatMessage(packet);
                                 }
                             });

    // kTeamChat (5030) -- incoming team/party chat message.
    manager.register_handler(mir2::common::MsgId::kTeamChat,
                             [weak_self](const NetworkPacket& packet) {
                                 if (auto self = weak_self.lock()) {
                                     self->HandleChatMessage(packet);
                                 }
                             });

    // kAreaChat (5040) -- incoming area/local chat message.
    manager.register_handler(mir2::common::MsgId::kAreaChat,
                             [weak_self](const NetworkPacket& packet) {
                                 if (auto self = weak_self.lock()) {
                                     self->HandleChatMessage(packet);
                                 }
                             });

    // kSystemMsg (5050) -- system broadcast message.
    manager.register_handler(mir2::common::MsgId::kSystemMsg,
                             [weak_self](const NetworkPacket& packet) {
                                 if (auto self = weak_self.lock()) {
                                     self->HandleSystemMessage(packet);
                                 }
                             });
}

void ChatHandler::HandleChatResponse(const NetworkPacket& packet) {
    std::shared_ptr<void> owner_guard;
    if (!TryLockCallbackOwner(callbacks_, &owner_guard)) {
        return;
    }

    if (packet.payload.empty()) {
        if (callbacks_.on_parse_error) {
            callbacks_.on_parse_error("Empty chat response payload");
        }
        return;
    }

    flatbuffers::Verifier verifier(packet.payload.data(), packet.payload.size());
    if (!verifier.VerifyBuffer<mir2::proto::ChatRsp>(nullptr)) {
        if (callbacks_.on_parse_error) {
            callbacks_.on_parse_error("Chat response verification failed");
        }
        return;
    }

    const auto* rsp = flatbuffers::GetRoot<mir2::proto::ChatRsp>(packet.payload.data());
    if (!rsp) {
        if (callbacks_.on_parse_error) {
            callbacks_.on_parse_error("Chat response parse failed");
        }
        return;
    }

    if (callbacks_.on_chat_response) {
        callbacks_.on_chat_response(rsp->code());
    }
}

void ChatHandler::HandleChatMessage(const NetworkPacket& packet) {
    std::shared_ptr<void> owner_guard;
    if (!TryLockCallbackOwner(callbacks_, &owner_guard)) {
        return;
    }

    if (packet.payload.empty()) {
        if (callbacks_.on_parse_error) {
            callbacks_.on_parse_error("Empty chat message payload");
        }
        return;
    }

    flatbuffers::Verifier verifier(packet.payload.data(), packet.payload.size());
    if (!verifier.VerifyBuffer<mir2::proto::ChatMessage>(nullptr)) {
        if (callbacks_.on_parse_error) {
            callbacks_.on_parse_error("Chat message verification failed");
        }
        return;
    }

    const auto* msg = flatbuffers::GetRoot<mir2::proto::ChatMessage>(packet.payload.data());
    if (!msg) {
        if (callbacks_.on_parse_error) {
            callbacks_.on_parse_error("Chat message parse failed");
        }
        return;
    }

    ChatMessageData data;
    data.channel = ToClientChatChannel(msg->channel());
    data.from_id = msg->from_id();
    data.from_name = msg->from_name() ? msg->from_name()->str() : std::string{};
    data.to_id = msg->to_id();
    data.content = msg->content() ? msg->content()->str() : std::string{};
    data.color = msg->color();
    data.timestamp = static_cast<int64_t>(msg->timestamp());

    if (callbacks_.on_chat_message) {
        callbacks_.on_chat_message(data);
    }
}

void ChatHandler::HandleSystemMessage(const NetworkPacket& packet) {
    std::shared_ptr<void> owner_guard;
    if (!TryLockCallbackOwner(callbacks_, &owner_guard)) {
        return;
    }

    if (packet.payload.empty()) {
        if (callbacks_.on_parse_error) {
            callbacks_.on_parse_error("Empty system message payload");
        }
        return;
    }

    flatbuffers::Verifier verifier(packet.payload.data(), packet.payload.size());
    if (!verifier.VerifyBuffer<mir2::proto::ServerNotice>(nullptr)) {
        if (callbacks_.on_parse_error) {
            callbacks_.on_parse_error("System message verification failed");
        }
        return;
    }

    const auto* notice = flatbuffers::GetRoot<mir2::proto::ServerNotice>(packet.payload.data());
    if (!notice || !notice->message()) {
        if (callbacks_.on_parse_error) {
            callbacks_.on_parse_error("System message parse failed");
        }
        return;
    }

    if (callbacks_.on_system_message) {
        callbacks_.on_system_message(notice->message()->str(),
                                     static_cast<int64_t>(notice->timestamp()));
    }
}

} // namespace mir2::game::handlers
