/**
 * @file chat_handler.h
 * @brief Client chat message handlers.
 */

#ifndef LEGEND2_CLIENT_HANDLERS_CHAT_HANDLER_H
#define LEGEND2_CLIENT_HANDLERS_CHAT_HANDLER_H

#include "client/network/i_network_manager.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>

namespace mir2::proto {
enum class ErrorCode : uint16_t;
}

namespace mir2::game::handlers {

/// Chat channel type (mirrors proto ChatChannel)
enum class ChatChannel : uint8_t {
    kWorld = 0,
    kPrivate = 1,
    kGuild = 2,
    kSystem = 3,
    kTeam = 4,
    kArea = 5
};

/// Chat message data
struct ChatMessageData {
    ChatChannel channel = ChatChannel::kWorld;
    uint64_t from_id = 0;
    std::string from_name;
    uint64_t to_id = 0;
    std::string content;
    uint32_t color = 0xFFFFFFFF;
    int64_t timestamp = 0;
};

/**
 * @brief Handles chat messages from server.
 *
 * Responsibilities:
 * - Register chat handlers with NetworkManager.
 * - Parse FlatBuffers payloads and forward results to callbacks.
 *
 * Threading: expected to be used on the main thread; handlers are invoked from
 * NetworkManager::update() context.
 *
 * Ownership: stores callbacks by value and does not own NetworkManager.
 * Caller manages handler lifetime and should keep it alive while handlers are bound.
 */
class ChatHandler : public std::enable_shared_from_this<ChatHandler> {
public:
    using NetworkPacket = mir2::common::NetworkPacket;

    /**
     * @brief Callback hooks for chat flow.
     *
     * Ownership: stored by value. If owner is provided, callbacks are invoked only
     * while the owner is still alive; otherwise the caller must ensure captured
     * objects outlive the handler.
     */
    struct Callbacks {
        // Optional lifetime guard for objects captured by callbacks.
        std::optional<std::weak_ptr<void>> owner;
        std::function<void(mir2::proto::ErrorCode code)> on_chat_response;
        std::function<void(const ChatMessageData& msg)> on_chat_message;
        std::function<void(const std::string& message, int64_t timestamp)> on_system_message;
        std::function<void(const std::string& error)> on_parse_error;
    };

    explicit ChatHandler(Callbacks callbacks);
    ~ChatHandler() = default;

    // Bind handlers with lifetime safety. Requires shared_ptr ownership.
    void BindHandlers(mir2::client::INetworkManager& manager);
    static void RegisterHandlers(mir2::client::INetworkManager& manager);

    void HandleChatResponse(const NetworkPacket& packet);
    void HandleChatMessage(const NetworkPacket& packet);
    void HandleSystemMessage(const NetworkPacket& packet);

private:
    Callbacks callbacks_;
};

} // namespace mir2::game::handlers

#endif // LEGEND2_CLIENT_HANDLERS_CHAT_HANDLER_H
