/**
 * @file network_manager.h
 * @brief Client network manager wrapper.
 */

#ifndef LEGEND2_CLIENT_NETWORK_MANAGER_H
#define LEGEND2_CLIENT_NETWORK_MANAGER_H

#include "client/network/i_network_manager.h"
#include "client/network/message_dispatcher.h"
#include "client/network/network_client.h"

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace mir2::client {

using mir2::common::ErrorCode;

/**
 * @brief Manages client networking and dispatches incoming packets.
 *
 * Responsibilities:
 * - Own an INetworkClient instance and manage connect/disconnect lifecycle.
 * - Forward incoming packets to MessageDispatcher.
 *
 * Threading: update() should be called from the main thread to deliver messages
 * and connection state callbacks; IO thread only marks events as pending.
 *
 * Ownership: owns the INetworkClient; does not own handler targets.
 */
class NetworkManager : public INetworkManager {
public:
    using NetworkPacket = mir2::common::NetworkPacket;
    using HandlerFunc = MessageDispatcher::HandlerFunc;
    using EventCallback = INetworkClient::EventCallback;

    NetworkManager();
    explicit NetworkManager(std::unique_ptr<INetworkClient> client);

    /// Connect to the server.
    bool connect(const std::string& host, uint16_t port) override;

    /// Disconnect from the server.
    void disconnect() override;

    /// Check whether the client is connected.
    bool is_connected() const override;

    /// Send a message to the server.
    void send_message(mir2::common::MsgId msg_id, const std::vector<uint8_t>& payload) override;

    /// Register a handler for a message id.
    void register_handler(mir2::common::MsgId msg_id, HandlerFunc handler) override;

    /// Register a fallback handler for unhandled messages.
    void set_default_handler(HandlerFunc handler) override;

    /// Set connect callback.
    void set_on_connect(EventCallback callback) override;

    /// Set disconnect callback.
    void set_on_disconnect(EventCallback callback) override;

    /// Get the underlying connection state.
    ConnectionState get_state() const override;

    /// Get the last error reported by the network client.
    ErrorCode get_last_error() const override;

    /// Update the network client (process queued callbacks).
    void update() override;

private:
    void wire_callbacks();
    void handle_connect();
    void handle_disconnect();

    std::unique_ptr<INetworkClient> client_;
    MessageDispatcher dispatcher_;
    EventCallback on_connect_;
    EventCallback on_disconnect_;
    std::atomic<bool> connect_pending_{false};
    std::atomic<bool> disconnect_pending_{false};
    std::atomic<bool> disconnect_notified_{false};
};

inline NetworkManager::NetworkManager()
    : NetworkManager(std::make_unique<NetworkClient>()) {
}

} // namespace mir2::client

#endif // LEGEND2_CLIENT_NETWORK_MANAGER_H
