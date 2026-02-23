/**
 * @file achievement_handler.h
 * @brief Achievement handler for logic layer.
 */

#ifndef MIR2_LOGIC_HANDLERS_ACHIEVEMENT_ACHIEVEMENT_HANDLER_H_
#define MIR2_LOGIC_HANDLERS_ACHIEVEMENT_ACHIEVEMENT_HANDLER_H_

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include <entt/entt.hpp>

#include "ecs/components/achievement_component.h"
#include "logic/handler_context.h"
#include "logic/task.h"
#include "server/common/error_codes.h"

namespace mir2::proto {
class AchievementListReq;
class AchievementClaimReq;
}  // namespace mir2::proto

namespace mir2::logic {

class ResponseSender;

class AchievementHandler {
 public:
  AchievementHandler(ResponseSender& response_sender, entt::registry& ecs_registry);

  Task<void> HandleMessage(HandlerContext ctx,
                           const uint8_t* payload,
                           size_t payload_size);

 private:
  Task<void> HandleList(HandlerContext ctx, const mir2::proto::AchievementListReq* req);
  Task<void> HandleClaim(HandlerContext ctx, const mir2::proto::AchievementClaimReq* req);

  Task<void> SendListRsp(uint64_t client_id,
                         bool success,
                         mir2::common::ErrorCode code,
                         const std::vector<mir2::ecs::AchievementProgress>& achievements);
  Task<void> SendClaimRsp(uint64_t client_id,
                          bool success,
                          mir2::common::ErrorCode code,
                          uint32_t achievement_id,
                          uint32_t reward_gold);
  Task<void> SendUpdate(uint64_t client_id, const mir2::ecs::AchievementProgress& achievement);

  mir2::ecs::AchievementComponent& EnsureComponent(entt::entity entity);
  std::vector<mir2::ecs::AchievementProgress> CollectAchievements(
      const mir2::ecs::AchievementComponent& component) const;

  ResponseSender& response_sender_;
  entt::registry& ecs_registry_;
};

}  // namespace mir2::logic

#endif  // MIR2_LOGIC_HANDLERS_ACHIEVEMENT_ACHIEVEMENT_HANDLER_H_
