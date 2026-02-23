/**
 * @file guild_handler.h
 * @brief Client guild message handlers.
 */

#ifndef LEGEND2_CLIENT_HANDLERS_GUILD_HANDLER_H
#define LEGEND2_CLIENT_HANDLERS_GUILD_HANDLER_H

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

struct GuildRankInfoData {
    uint8_t rank = 0;
    std::string rank_name;
    std::vector<std::string> members;
};

struct GuildWarInfoData {
    uint32_t enemy_guild_id = 0;
    uint64_t start_time = 0;
    uint64_t remain_time = 0;
};

struct GuildInfoSyncData {
    bool has_guild = false;
    uint64_t guild_id = 0;
    std::string guild_name;
    uint16_t level = 0;
    uint32_t member_count = 0;
    uint64_t leader_id = 0;
    std::string leader_name;
    uint32_t max_members = 0;
    std::vector<std::string> notice_list;
    std::vector<GuildRankInfoData> ranks;
    std::vector<GuildWarInfoData> war_guilds;
    std::vector<uint32_t> ally_guild_ids;
    bool allow_ally = false;
    bool in_team_fight = false;
    int32_t match_point = 0;
    std::vector<std::string> fight_members;
};

struct GuildRankUpdateMember {
    uint32_t character_id = 0;
    uint8_t rank = 0;
};

class GuildHandler : public std::enable_shared_from_this<GuildHandler> {
public:
    using NetworkPacket = mir2::common::NetworkPacket;

    struct Callbacks {
        std::optional<std::weak_ptr<void>> owner;
        std::function<void(bool success, mir2::proto::ErrorCode code, uint64_t guild_id)>
            on_create_response;
        std::function<void(bool success, mir2::proto::ErrorCode code)> on_join_response;
        std::function<void(bool success, mir2::proto::ErrorCode code)> on_leave_response;
        std::function<void(bool success, mir2::proto::ErrorCode code)> on_kick_response;
        std::function<void(bool success, mir2::proto::ErrorCode code)> on_declare_war_response;
        std::function<void(bool success, mir2::proto::ErrorCode code)> on_cancel_war_response;
        std::function<void(bool success, mir2::proto::ErrorCode code)> on_make_ally_response;
        std::function<void(bool success, mir2::proto::ErrorCode code)> on_break_ally_response;
        std::function<void(bool success, mir2::proto::ErrorCode code)> on_update_notice_response;
        std::function<void(bool success, mir2::proto::ErrorCode code)> on_update_rank_response;
        std::function<void(const GuildInfoSyncData& sync)> on_guild_info_sync;
        std::function<void(const std::string& error)> on_parse_error;
    };

    explicit GuildHandler(Callbacks callbacks);
    ~GuildHandler() = default;

    GuildHandler(const GuildHandler&) = delete;
    GuildHandler& operator=(const GuildHandler&) = delete;

    void BindHandlers(mir2::client::INetworkManager& manager);
    static void RegisterHandlers(mir2::client::INetworkManager& manager);

    void HandleCreateGuildResponse(const NetworkPacket& packet);
    void HandleJoinGuildResponse(const NetworkPacket& packet);
    void HandleLeaveGuildResponse(const NetworkPacket& packet);
    void HandleKickGuildResponse(const NetworkPacket& packet);
    void HandleDeclareWarResponse(const NetworkPacket& packet);
    void HandleCancelWarResponse(const NetworkPacket& packet);
    void HandleMakeAllyResponse(const NetworkPacket& packet);
    void HandleBreakAllyResponse(const NetworkPacket& packet);
    void HandleUpdateNoticeResponse(const NetworkPacket& packet);
    void HandleUpdateRankResponse(const NetworkPacket& packet);
    void HandleGuildInfoSync(const NetworkPacket& packet);

    static void SendCreateGuildRequest(mir2::client::INetworkManager& manager,
                                       const std::string& guild_name);
    static void SendJoinGuildRequest(mir2::client::INetworkManager& manager,
                                     uint32_t guild_id);
    static void SendLeaveGuildRequest(mir2::client::INetworkManager& manager);
    static void SendKickGuildRequest(mir2::client::INetworkManager& manager,
                                     uint32_t target_character_id);
    static void SendDeclareWarRequest(mir2::client::INetworkManager& manager,
                                      uint32_t target_guild_id);
    static void SendCancelWarRequest(mir2::client::INetworkManager& manager,
                                     uint32_t target_guild_id);
    static void SendMakeAllyRequest(mir2::client::INetworkManager& manager,
                                    uint32_t target_guild_id);
    static void SendBreakAllyRequest(mir2::client::INetworkManager& manager,
                                     uint32_t target_guild_id);
    static void SendUpdateNoticeRequest(mir2::client::INetworkManager& manager,
                                        const std::vector<std::string>& notice_lines);
    static void SendUpdateRankRequest(mir2::client::INetworkManager& manager,
                                      const std::vector<GuildRankUpdateMember>& members);

private:
    Callbacks callbacks_;
};

} // namespace mir2::game::handlers

#endif // LEGEND2_CLIENT_HANDLERS_GUILD_HANDLER_H
