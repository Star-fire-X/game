#include "client/network/message_dispatcher.h"

#include <iostream>

namespace mir2::client {

void MessageDispatcher::RegisterHandler(mir2::common::MsgId msg_id, HandlerFunc handler) {
    std::lock_guard<std::mutex> lock(mutex_);
    handlers_[static_cast<uint16_t>(msg_id)] = std::move(handler);
}

void MessageDispatcher::SetDefaultHandler(HandlerFunc handler) {
    std::lock_guard<std::mutex> lock(mutex_);
    default_handler_ = std::move(handler);
}

void MessageDispatcher::Dispatch(const NetworkPacket& packet) const {
    HandlerFunc handler;
    HandlerFunc fallback;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = handlers_.find(packet.msg_id);
        if (it != handlers_.end()) {
            handler = it->second;
        }
        if (!handler) {
            fallback = default_handler_;
        }
    }

    if (handler) {
        handler(packet);
        return;
    }

    if (fallback) {
        fallback(packet);
        return;
    }

    std::cerr << "Unhandled message id: " << packet.msg_id << std::endl;
}

} // namespace mir2::client
