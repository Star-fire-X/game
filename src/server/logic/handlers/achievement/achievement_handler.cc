#include "logic/handlers/achievement/achievement_handler.h"

#include <algorithm>
#include <exception>
#include <utility>

#include <flatbuffers/flatbuffers.h>

#include "achievement_generated.h"
#include "common/enums.h"
#include "ecs/components/achievement_component.h"
#include "ecs/components/character_components.h"
#include "log/logger.h"
#include "logic/handlers/handler_error_utils.h"
#include "logic/response_sender.h"

namespace mir2::logic {

namespace {

flatbuffers::Offset<mir2::proto::AchievementProgress> BuildProgress(
    flatbuffers::FlatBufferBuilder& builder,
    const mir2::ecs::AchievementProgress& achievement) {
  return mir2::proto::CreateAchievementProgress(builder,
                                                achievement.achievement_id,
                                                achievement.progress,
                                                achievement.target,
                                                achievement.completed,
                                                achievement.claimed,
                                                achievement.completed_time_ms,
                                                achievement.reward_gold);
}

std::vector<uint8_t> BuildListRspPayload(
    bool success,
    mir2::common::ErrorCode code,
    const std::vector<mir2::ecs::AchievementProgress>& achievements) {
  flatbuffers::FlatBufferBuilder builder;
  std::vector<flatbuffers::Offset<mir2::proto::AchievementProgress>> offsets;
  offsets.reserve(achievements.size());
  for (const auto& achievement : achievements) {
    offsets.push_back(BuildProgress(builder, achievement));
  }
  const auto achievements_vec = builder.CreateVector(offsets);
  const auto rsp = mir2::proto::CreateAchievementListRsp(
      builder, success, static_cast<int>(ToProtoError(code)), achievements_vec);
  builder.Finish(rsp);
  const uint8_t* data = builder.GetBufferPointer();
  return {data, data + builder.GetSize()};
}

std::vector<uint8_t> BuildClaimRspPayload(bool success,
                                          mir2::common::ErrorCode code,
                                          uint32_t achievement_id,
                                          uint32_t reward_gold) {
  flatbuffers::FlatBufferBuilder builder;
  const auto rsp = mir2::proto::CreateAchievementClaimRsp(
      builder,
      success,
      static_cast<int>(ToProtoError(code)),
      achievement_id,
      reward_gold);
  builder.Finish(rsp);
  const uint8_t* data = builder.GetBufferPointer();
  return {data, data + builder.GetSize()};
}

std::vector<uint8_t> BuildUpdatePayload(const mir2::ecs::AchievementProgress& achievement) {
  flatbuffers::FlatBufferBuilder builder;
  const auto progress = BuildProgress(builder, achievement);
  const auto update = mir2::proto::CreateAchievementUpdate(builder, progress);
  builder.Finish(update);
  const uint8_t* data = builder.GetBufferPointer();
  return {data, data + builder.GetSize()};
}

}  // namespace

AchievementHandler::AchievementHandler(ResponseSender& response_sender,
                                       entt::registry& ecs_registry)
    : response_sender_(response_sender), ecs_registry_(ecs_registry) {}

Task<void> AchievementHandler::HandleMessage(HandlerContext ctx,
                                             const uint8_t* payload,
                                             size_t payload_size) {
  try {
    if (!payload || payload_size == 0) {
      co_await SendListRsp(
          ctx.client_id, false, mir2::common::ErrorCode::kInvalidAction, {});
      co_return;
    }

    switch (static_cast<mir2::common::MsgId>(ctx.msg_id)) {
      case mir2::common::MsgId::kAchievementListReq: {
        flatbuffers::Verifier verifier(payload, payload_size);
        if (!verifier.VerifyBuffer<mir2::proto::AchievementListReq>(nullptr)) {
          co_await SendListRsp(
              ctx.client_id, false, mir2::common::ErrorCode::kInvalidAction, {});
          co_return;
        }
        const auto* req =
            flatbuffers::GetRoot<mir2::proto::AchievementListReq>(payload);
        co_await HandleList(std::move(ctx), req);
        co_return;
      }
      case mir2::common::MsgId::kAchievementClaimReq: {
        flatbuffers::Verifier verifier(payload, payload_size);
        if (!verifier.VerifyBuffer<mir2::proto::AchievementClaimReq>(nullptr)) {
          co_await SendClaimRsp(ctx.client_id,
                                false,
                                mir2::common::ErrorCode::kInvalidAction,
                                0,
                                0);
          co_return;
        }
        const auto* req =
            flatbuffers::GetRoot<mir2::proto::AchievementClaimReq>(payload);
        co_await HandleClaim(std::move(ctx), req);
        co_return;
      }
      default:
        SYSLOG_WARN("AchievementHandler unsupported msg_id={} client_id={}",
                    ctx.msg_id,
                    ctx.client_id);
        co_return;
    }
  } catch (const std::exception& ex) {
    SYSLOG_ERROR("AchievementHandler exception msg_id={} client_id={} error={}",
                 ctx.msg_id,
                 ctx.client_id,
                 ex.what());
    co_return;
  } catch (...) {
    SYSLOG_ERROR("AchievementHandler exception msg_id={} client_id={} error=unknown",
                 ctx.msg_id,
                 ctx.client_id);
    co_return;
  }
}

Task<void> AchievementHandler::HandleList(HandlerContext ctx,
                                          const mir2::proto::AchievementListReq* /*req*/) {
  if (ctx.entity == entt::null || !ecs_registry_.valid(ctx.entity)) {
    co_await SendListRsp(
        ctx.client_id, false, mir2::common::ErrorCode::kTargetNotFound, {});
    co_return;
  }

  auto& component = EnsureComponent(ctx.entity);
  co_await SendListRsp(ctx.client_id,
                       true,
                       mir2::common::ErrorCode::kOk,
                       CollectAchievements(component));
}

Task<void> AchievementHandler::HandleClaim(HandlerContext ctx,
                                           const mir2::proto::AchievementClaimReq* req) {
  if (!req || req->achievement_id() == 0) {
    co_await SendClaimRsp(
        ctx.client_id, false, mir2::common::ErrorCode::kInvalidAction, 0, 0);
    co_return;
  }

  if (ctx.entity == entt::null || !ecs_registry_.valid(ctx.entity)) {
    co_await SendClaimRsp(ctx.client_id,
                          false,
                          mir2::common::ErrorCode::kTargetNotFound,
                          req->achievement_id(),
                          0);
    co_return;
  }

  auto& component = EnsureComponent(ctx.entity);
  const auto it = component.achievements.find(req->achievement_id());
  if (it == component.achievements.end()) {
    co_await SendClaimRsp(ctx.client_id,
                          false,
                          mir2::common::ErrorCode::kAchievementNotFound,
                          req->achievement_id(),
                          0);
    co_return;
  }

  auto& achievement = it->second;
  if (achievement.claimed) {
    co_await SendClaimRsp(ctx.client_id,
                          false,
                          mir2::common::ErrorCode::kAchievementAlreadyClaimed,
                          achievement.achievement_id,
                          0);
    co_return;
  }
  if (!achievement.completed) {
    co_await SendClaimRsp(ctx.client_id,
                          false,
                          mir2::common::ErrorCode::kAchievementNotCompleted,
                          achievement.achievement_id,
                          0);
    co_return;
  }

  achievement.claimed = true;
  const uint32_t reward_gold = achievement.reward_gold;
  if (reward_gold > 0) {
    if (auto* attributes =
            ecs_registry_.try_get<mir2::ecs::CharacterAttributesComponent>(ctx.entity)) {
      attributes->gold += static_cast<int>(reward_gold);
    }
  }

  co_await SendClaimRsp(ctx.client_id,
                        true,
                        mir2::common::ErrorCode::kOk,
                        achievement.achievement_id,
                        reward_gold);
  co_await SendUpdate(ctx.client_id, achievement);
}

Task<void> AchievementHandler::SendListRsp(
    uint64_t client_id,
    bool success,
    mir2::common::ErrorCode code,
    const std::vector<mir2::ecs::AchievementProgress>& achievements) {
  co_await response_sender_.SendAsync(
      client_id,
      static_cast<uint16_t>(mir2::common::MsgId::kAchievementListRsp),
      BuildListRspPayload(success, code, achievements));
}

Task<void> AchievementHandler::SendClaimRsp(uint64_t client_id,
                                            bool success,
                                            mir2::common::ErrorCode code,
                                            uint32_t achievement_id,
                                            uint32_t reward_gold) {
  co_await response_sender_.SendAsync(
      client_id,
      static_cast<uint16_t>(mir2::common::MsgId::kAchievementClaimRsp),
      BuildClaimRspPayload(success, code, achievement_id, reward_gold));
}

Task<void> AchievementHandler::SendUpdate(
    uint64_t client_id, const mir2::ecs::AchievementProgress& achievement) {
  co_await response_sender_.SendAsync(
      client_id,
      static_cast<uint16_t>(mir2::common::MsgId::kAchievementUpdate),
      BuildUpdatePayload(achievement));
}

mir2::ecs::AchievementComponent& AchievementHandler::EnsureComponent(entt::entity entity) {
  auto& component = ecs_registry_.get_or_emplace<mir2::ecs::AchievementComponent>(entity);
  if (!component.achievements.empty()) {
    return component;
  }

  mir2::ecs::AchievementProgress login_days;
  login_days.achievement_id = 1001;
  login_days.target = 7;
  login_days.progress = 0;
  login_days.reward_gold = 1000;

  mir2::ecs::AchievementProgress level_goal;
  level_goal.achievement_id = 1002;
  level_goal.target = 20;
  level_goal.progress = 1;
  level_goal.reward_gold = 3000;

  mir2::ecs::AchievementProgress monster_goal;
  monster_goal.achievement_id = 1003;
  monster_goal.target = 100;
  monster_goal.progress = 0;
  monster_goal.reward_gold = 5000;

  component.achievements.emplace(login_days.achievement_id, login_days);
  component.achievements.emplace(level_goal.achievement_id, level_goal);
  component.achievements.emplace(monster_goal.achievement_id, monster_goal);
  return component;
}

std::vector<mir2::ecs::AchievementProgress> AchievementHandler::CollectAchievements(
    const mir2::ecs::AchievementComponent& component) const {
  std::vector<mir2::ecs::AchievementProgress> achievements;
  achievements.reserve(component.achievements.size());
  for (const auto& [_, achievement] : component.achievements) {
    achievements.push_back(achievement);
  }
  std::sort(achievements.begin(),
            achievements.end(),
            [](const mir2::ecs::AchievementProgress& lhs,
               const mir2::ecs::AchievementProgress& rhs) {
              return lhs.achievement_id < rhs.achievement_id;
            });
  return achievements;
}

}  // namespace mir2::logic
