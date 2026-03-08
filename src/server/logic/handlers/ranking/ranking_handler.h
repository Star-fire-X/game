/**
 * @file ranking_handler.h
 * @brief Ranking handler for logic layer.
 */

#ifndef MIR2_LOGIC_HANDLERS_RANKING_RANKING_HANDLER_H_
#define MIR2_LOGIC_HANDLERS_RANKING_RANKING_HANDLER_H_

#include <cstddef>
#include <cstdint>
#include <vector>

#include <entt/entt.hpp>

#include "logic/handler_context.h"
#include "logic/services/ranking_service.h"
#include "logic/task.h"
#include "server/common/error_codes.h"

namespace mir2::proto {
class RankingReq;
class RankingMyRankReq;
enum class RankingType : uint8_t;
}  // namespace mir2::proto

namespace mir2::logic {

class ResponseSender;

class RankingHandler {
 public:
  RankingHandler(ResponseSender& response_sender,
                 RankingService& ranking_service,
                 entt::registry& ecs_registry);

  Task<void> HandleMessage(HandlerContext ctx,
                           const uint8_t* payload,
                           size_t payload_size);

 private:
  Task<void> HandleRanking(HandlerContext ctx, const mir2::proto::RankingReq* req);
  Task<void> HandleMyRank(HandlerContext ctx, const mir2::proto::RankingMyRankReq* req);

  bool IsSupportedRankingType(mir2::proto::RankingType type) const;
  uint64_t ResolveEntityIdForMyRank(HandlerContext ctx,
                                    mir2::proto::RankingType type) const;

  Task<void> SendRankingRsp(uint64_t client_id,
                            bool success,
                            mir2::common::ErrorCode code,
                            mir2::proto::RankingType type,
                            uint32_t total_count,
                            const std::vector<RankingEntryView>& entries);
  Task<void> SendMyRankRsp(uint64_t client_id,
                           bool success,
                           mir2::common::ErrorCode code,
                           mir2::proto::RankingType type,
                           uint32_t rank,
                           int64_t value);

  ResponseSender& response_sender_;
  RankingService& ranking_service_;
  entt::registry& ecs_registry_;
};

}  // namespace mir2::logic

#endif  // MIR2_LOGIC_HANDLERS_RANKING_RANKING_HANDLER_H_
