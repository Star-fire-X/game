/**
 * @file login_handler.h
 * @brief Client login message handlers.
 */

#ifndef LEGEND2_CLIENT_HANDLERS_LOGIN_HANDLER_H
#define LEGEND2_CLIENT_HANDLERS_LOGIN_HANDLER_H

#include "client/network/i_network_manager.h"
#include <cstdint>
#include <functional>
#include <string>

namespace mir2::game::handlers {

/**
 * @brief Handles login related messages.
 *
 * Responsibilities:
 * - Register login handlers with NetworkManager.
 * - Parse FlatBuffers payloads and forward results to callbacks.
 *
 * Threading: expected to be used on the main thread; handlers are invoked from
 * NetworkManager::update() context.
 *
 * Ownership: stores callbacks by value and does not own NetworkManager. Only one
 * active instance is supported (static instance_ is used by RegisterHandlers).
 */
class LoginHandler {
public:
    using NetworkPacket = mir2::common::NetworkPacket;

    /**
     * @brief Callback hooks for login flow.
     *
     * Ownership: stored by value; captured objects referenced by callbacks must
     * outlive the handler.
     */
    struct Callbacks {
        std::function<void(uint64_t account_id, const std::string& session_token)> on_login_success;
        std::function<void(const std::string& error)> on_login_failure;
        std::function<void()> on_login_timeout;
        std::function<void()> request_character_list;
    };

    explicit LoginHandler(Callbacks callbacks);
    ~LoginHandler();

    void BindHandlers(mir2::client::INetworkManager& manager);
    static void RegisterHandlers(mir2::client::INetworkManager& manager);

    void HandleLoginResponse(const NetworkPacket& packet);
    void HandleLoginTimeout();

private:
    Callbacks callbacks_;
    static LoginHandler* instance_;
};

} // namespace mir2::game::handlers

#endif // LEGEND2_CLIENT_HANDLERS_LOGIN_HANDLER_H
