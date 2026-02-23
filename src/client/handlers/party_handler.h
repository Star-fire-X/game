/**
 * @file party_handler.h
 * @brief Client party message handlers.
 */

#ifndef LEGEND2_CLIENT_HANDLERS_PARTY_HANDLER_H
#define LEGEND2_CLIENT_HANDLERS_PARTY_HANDLER_H

#include "client/network/i_network_manager.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace mir2::proto {
enum class ErrorCode : uint16_t;
}

namespace mir2::game::handlers {

struct PartyMemberData {
    uint64_t character_id = 0;
    std::string name;
    uint32_t hp = 0;
    uint32_t max_hp = 0;
    uint32_t map_id = 0;
    uint32_t x = 0;
    uint32_t y = 0;
    bool online = false;
};

struct PartyUpdateData {
    uint64_t party_id = 0;
    uint64_t leader_character_id = 0;
    std::vector<PartyMemberData> members;
};

class PartyHandler : public std::enable_shared_from_this<PartyHandler> {
public:
    using NetworkPacket = mir2::common::NetworkPacket;

    struct Callbacks {
        std::optional<std::weak_ptr<void>> owner;

        std::function<void(bool success, mir2::proto::ErrorCode code)> on_invite_response;
        std::function<void(bool success, mir2::proto::ErrorCode code)> on_join_response;
        std::function<void(bool success, mir2::proto::ErrorCode code)> on_leave_response;
        std::function<void(bool success, mir2::proto::ErrorCode code)> on_kick_response;
        std::function<void(const PartyUpdateData& update)> on_party_update;
        std::function<void(const std::string& error)> on_parse_error;
    };

    explicit PartyHandler(Callbacks callbacks);
    ~PartyHandler() = default;

    PartyHandler(const PartyHandler&) = delete;
    PartyHandler& operator=(const PartyHandler&) = delete;

    void BindHandlers(mir2::client::INetworkManager& manager);
    static void RegisterHandlers(mir2::client::INetworkManager& manager);

    void HandleInviteResponse(const NetworkPacket& packet);
    void HandleJoinResponse(const NetworkPacket& packet);
    void HandleLeaveResponse(const NetworkPacket& packet);
    void HandleKickResponse(const NetworkPacket& packet);
    void HandlePartyUpdate(const NetworkPacket& packet);

    static void SendInviteRequest(mir2::client::INetworkManager& manager,
                                  uint32_t target_character_id);
    static void SendJoinRequest(mir2::client::INetworkManager& manager,
                                uint64_t party_id);
    static void SendLeaveRequest(mir2::client::INetworkManager& manager);
    static void SendKickRequest(mir2::client::INetworkManager& manager,
                                uint32_t target_character_id);

private:
    Callbacks callbacks_;
};

} // namespace mir2::game::handlers

#endif // LEGEND2_CLIENT_HANDLERS_PARTY_HANDLER_H
