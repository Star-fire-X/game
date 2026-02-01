/**
 * @file i_network_manager.h
 * @brief Network manager interface for dependency injection.
 */

#ifndef LEGEND2_CLIENT_NETWORK_I_NETWORK_MANAGER_H
#define LEGEND2_CLIENT_NETWORK_I_NETWORK_MANAGER_H

#include "client/network/message_dispatcher.h"
#include "client/network/network_client.h"

#include <functional>
#include <string>
#include <vector>

namespace mir2::client {

/**
 * @brief Interface for client network manager implementations.
 */
class INetworkManager {
public:
    using NetworkPacket = mir2::common::NetworkPacket;
    using HandlerFunc = MessageDispatcher::HandlerFunc;
    using EventCallback = INetworkClient::EventCallback;

    virtual ~INetworkManager() = default;

    /// Connect to the server.
    virtual bool connect(const std::string& host, uint16_t port) = 0;

    /// Disconnect from the server.
    virtual void disconnect() = 0;

    /// Check whether the client is connected.
    virtual bool is_connected() const = 0;

    /// Send a message to the server.
    virtual void send_message(mir2::common::MsgId msg_id, const std::vector<uint8_t>& payload) = 0;

    /// Register a handler for a message id.
    virtual void register_handler(mir2::common::MsgId msg_id, HandlerFunc handler) = 0;

    /// Register a fallback handler for unhandled messages.
    virtual void set_default_handler(HandlerFunc handler) = 0;

    /// Set connect callback.
    virtual void set_on_connect(EventCallback callback) = 0;

    /// Set disconnect callback.
    virtual void set_on_disconnect(EventCallback callback) = 0;

    /// Get the underlying connection state.
    virtual ConnectionState get_state() const = 0;

    /// Get the last error reported by the network client.
    virtual ErrorCode get_last_error() const = 0;

    /// Update the network client (process queued callbacks).
    virtual void update() = 0;
};

} // namespace mir2::client

#endif // LEGEND2_CLIENT_NETWORK_I_NETWORK_MANAGER_H
