/**
 * @file system_handler.h
 * @brief Client system message handlers.
 */

#ifndef LEGEND2_CLIENT_HANDLERS_SYSTEM_HANDLER_H
#define LEGEND2_CLIENT_HANDLERS_SYSTEM_HANDLER_H

#include "client/network/i_network_manager.h"

#include <cstdint>
#include <functional>
#include <string>

namespace mir2::proto {
enum class ErrorCode : uint16_t;
}

namespace mir2::game::handlers {

/**
 * @brief Handles heartbeat, server notices, and kick messages.
 *
 * Responsibilities:
 * - Register system handlers with NetworkManager.
 * - Parse FlatBuffers payloads and forward results to callbacks.
 *
 * Threading: expected to be used on the main thread; handlers are invoked from
 * NetworkManager::update() context.
 *
 * Ownership: stores callbacks by value and does not own NetworkManager. Only one
 * active instance is supported (static instance_ is used by RegisterHandlers).
 */
class SystemHandler {
public:
    using NetworkPacket = mir2::common::NetworkPacket;

    /**
     * @brief Callback hooks for system events.
     *
     * Ownership: stored by value; captured objects referenced by callbacks must
     * outlive the handler.
     */
    struct Callbacks {
        std::function<void(uint32_t seq, uint32_t server_time)> on_heartbeat_response;
        std::function<void(uint16_t level, const std::string& message, uint32_t timestamp)> on_server_notice;
        std::function<void(mir2::proto::ErrorCode reason,
                           const std::string& reason_text,
                           const std::string& message)> on_kick;
    };

    explicit SystemHandler(Callbacks callbacks);
    ~SystemHandler();

    void BindHandlers(mir2::client::INetworkManager& manager);
    static void RegisterHandlers(mir2::client::INetworkManager& manager);

    void HandleHeartbeatResponse(const NetworkPacket& packet);
    void HandleServerNotice(const NetworkPacket& packet);
    void HandleKick(const NetworkPacket& packet);

private:
    Callbacks callbacks_;
    static SystemHandler* instance_;
};

} // namespace mir2::game::handlers

#endif // LEGEND2_CLIENT_HANDLERS_SYSTEM_HANDLER_H
