/**
 * @file ranking_handler.h
 * @brief Client ranking message handlers.
 */

#ifndef LEGEND2_CLIENT_HANDLERS_RANKING_HANDLER_H
#define LEGEND2_CLIENT_HANDLERS_RANKING_HANDLER_H

#include "client/network/i_network_manager.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace mir2::proto {
enum class ErrorCode : uint16_t;
enum class RankingType : uint8_t;
}

namespace mir2::game::handlers {

struct RankingEntryData {
    uint32_t rank = 0;
    uint64_t entity_id = 0;
    std::string name;
    int64_t value = 0;
    std::string extra;
};

struct RankingResponseData {
    mir2::proto::RankingType ranking_type;
    uint32_t total_count = 0;
    std::vector<RankingEntryData> entries;
};

struct MyRankData {
    mir2::proto::RankingType ranking_type;
    uint32_t rank = 0;
    int64_t value = 0;
};

class RankingHandler : public std::enable_shared_from_this<RankingHandler> {
public:
    using NetworkPacket = mir2::common::NetworkPacket;

    struct Callbacks {
        std::optional<std::weak_ptr<void>> owner;

        std::function<void(bool success, mir2::proto::ErrorCode code,
                           const RankingResponseData& response)> on_ranking_response;
        std::function<void(bool success, mir2::proto::ErrorCode code,
                           const MyRankData& my_rank)> on_my_rank_response;
        std::function<void(const std::string& error)> on_parse_error;
    };

    explicit RankingHandler(Callbacks callbacks);
    ~RankingHandler() = default;

    RankingHandler(const RankingHandler&) = delete;
    RankingHandler& operator=(const RankingHandler&) = delete;

    void BindHandlers(mir2::client::INetworkManager& manager);
    static void RegisterHandlers(mir2::client::INetworkManager& manager);

    void HandleRankingResponse(const NetworkPacket& packet);
    void HandleMyRankResponse(const NetworkPacket& packet);

    static void SendRankingRequest(mir2::client::INetworkManager& manager,
                                   mir2::proto::RankingType type,
                                   uint32_t page = 1,
                                   uint32_t page_size = 20);
    static void SendMyRankRequest(mir2::client::INetworkManager& manager,
                                  mir2::proto::RankingType type);

private:
    Callbacks callbacks_;
};

} // namespace mir2::game::handlers

#endif // LEGEND2_CLIENT_HANDLERS_RANKING_HANDLER_H
