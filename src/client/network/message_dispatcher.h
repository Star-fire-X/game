/**
 * @file message_dispatcher.h
 * @brief Client message dispatcher
 */

#ifndef LEGEND2_CLIENT_NETWORK_MESSAGE_DISPATCHER_H
#define LEGEND2_CLIENT_NETWORK_MESSAGE_DISPATCHER_H

#include "common/enums.h"
#include "common/protocol/packet_codec.h"

#include <functional>
#include <mutex>
#include <unordered_map>

namespace mir2::client {

/**
 * @brief Client message dispatcher for NetworkPacket messages.
 *
 * Responsibilities:
 * - Maintain MsgId -> handler mapping.
 * - Provide a fallback handler for unregistered messages.
 *
 * Thread safety: RegisterHandler/SetDefaultHandler/Dispatch are safe to call concurrently;
 * handlers are invoked without holding the internal mutex.
 *
 * Ownership: stores copies of handler callables; referenced state must outlive invocation.
 */
class MessageDispatcher {
public:
    using NetworkPacket = mir2::common::NetworkPacket;
    using HandlerFunc = std::function<void(const NetworkPacket&)>;

    /// Register or replace a handler for a message id.
    void RegisterHandler(mir2::common::MsgId msg_id, HandlerFunc handler);

    /// Set a fallback handler for unregistered messages.
    void SetDefaultHandler(HandlerFunc handler);

    /// Dispatch a packet to a registered handler.
    void Dispatch(const NetworkPacket& packet) const;

private:
    mutable std::mutex mutex_;
    std::unordered_map<uint16_t, HandlerFunc> handlers_;
    HandlerFunc default_handler_;
};

} // namespace mir2::client

#endif // LEGEND2_CLIENT_NETWORK_MESSAGE_DISPATCHER_H
