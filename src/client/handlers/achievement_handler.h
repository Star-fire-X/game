/**
 * @file achievement_handler.h
 * @brief Client achievement message handlers.
 */

#ifndef LEGEND2_CLIENT_HANDLERS_ACHIEVEMENT_HANDLER_H
#define LEGEND2_CLIENT_HANDLERS_ACHIEVEMENT_HANDLER_H

#include "client/network/i_network_manager.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace mir2::proto {
enum class ErrorCode : uint16_t;
class AchievementProgress;
}

namespace mir2::game::handlers {

struct AchievementProgressData {
    uint32_t achievement_id = 0;
    uint32_t progress = 0;
    uint32_t target = 0;
    bool completed = false;
    bool claimed = false;
    uint64_t completed_time = 0;
    uint32_t reward_gold = 0;
};

class AchievementHandler : public std::enable_shared_from_this<AchievementHandler> {
public:
    using NetworkPacket = mir2::common::NetworkPacket;

    struct Callbacks {
        std::optional<std::weak_ptr<void>> owner;

        std::function<void(bool success, mir2::proto::ErrorCode code,
                           const std::vector<AchievementProgressData>& achievements)>
            on_list_response;
        std::function<void(bool success, mir2::proto::ErrorCode code,
                           uint32_t achievement_id, uint32_t reward_gold)>
            on_claim_response;
        std::function<void(const AchievementProgressData& achievement)> on_update;
        std::function<void(const std::string& error)> on_parse_error;
    };

    explicit AchievementHandler(Callbacks callbacks);
    ~AchievementHandler() = default;

    AchievementHandler(const AchievementHandler&) = delete;
    AchievementHandler& operator=(const AchievementHandler&) = delete;

    void BindHandlers(mir2::client::INetworkManager& manager);
    static void RegisterHandlers(mir2::client::INetworkManager& manager);

    void HandleListResponse(const NetworkPacket& packet);
    void HandleClaimResponse(const NetworkPacket& packet);
    void HandleUpdate(const NetworkPacket& packet);

    static void SendListRequest(mir2::client::INetworkManager& manager);
    static void SendClaimRequest(mir2::client::INetworkManager& manager,
                                 uint32_t achievement_id);

private:
    static AchievementProgressData BuildProgressView(
        const mir2::proto::AchievementProgress* progress);

    Callbacks callbacks_;
};

} // namespace mir2::game::handlers

#endif // LEGEND2_CLIENT_HANDLERS_ACHIEVEMENT_HANDLER_H
