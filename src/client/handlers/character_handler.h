/**
 * @file character_handler.h
 * @brief Client character selection message handlers.
 */

#ifndef LEGEND2_CLIENT_HANDLERS_CHARACTER_HANDLER_H
#define LEGEND2_CLIENT_HANDLERS_CHARACTER_HANDLER_H

#include "client/network/i_network_manager.h"
#include "common/character_data.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace mir2::game::handlers {

/**
 * @brief Handles character list/create/select and enter game messages.
 *
 * Responsibilities:
 * - Register character handlers with NetworkManager.
 * - Parse FlatBuffers payloads and forward results to callbacks.
 *
 * Threading: expected to be used on the main thread; handlers are invoked from
 * NetworkManager::update() context.
 *
 * Ownership: stores callbacks by value and does not own NetworkManager. Only one
 * active instance is supported (static instance_ is used by RegisterHandlers).
 */
class CharacterHandler {
public:
    using NetworkPacket = mir2::common::NetworkPacket;

    /**
     * @brief Callback hooks for character selection flow.
     *
     * Ownership: stored by value; captured objects referenced by callbacks must
     * outlive the handler.
     */
    struct Callbacks {
        std::function<void(std::vector<mir2::common::CharacterData> characters)> on_character_list;
        std::function<void(const std::string& error)> on_character_list_failed;
        std::function<void(uint64_t player_id)> on_character_created;
        std::function<void(const std::string& error)> on_character_create_failed;
        std::function<void()> on_select_role_success;
        std::function<void(const std::string& error)> on_select_role_failed;
        std::function<void(const mir2::common::CharacterData& data)> on_enter_game_success;
        std::function<void(const std::string& error)> on_enter_game_failed;
        std::function<void()> request_character_list;
        std::function<uint64_t()> get_account_id;
        std::function<const std::vector<mir2::common::CharacterData>&()> get_character_list;
    };

    explicit CharacterHandler(Callbacks callbacks);
    ~CharacterHandler();

    void BindHandlers(mir2::client::INetworkManager& manager);
    static void RegisterHandlers(mir2::client::INetworkManager& manager);

    void HandleCharacterListResponse(const NetworkPacket& packet);
    void HandleCharacterCreateResponse(const NetworkPacket& packet);
    void HandleCharacterSelectResponse(const NetworkPacket& packet);
    void HandleEnterGameResponse(const NetworkPacket& packet);

private:
    Callbacks callbacks_;
    static CharacterHandler* instance_;
};

} // namespace mir2::game::handlers

#endif // LEGEND2_CLIENT_HANDLERS_CHARACTER_HANDLER_H
