#include "logic/handlers/ranking/ranking_handler.h"

#include <exception>
#include <utility>

#include <flatbuffers/flatbuffers.h>

#include "common/enums.h"
#include "ecs/components/character_components.h"
#include "ecs/components/guild_component.h"
#include "log/logger.h"
#include "logic/handlers/handler_error_utils.h"
#include "logic/response_sender.h"
#include "logic/services/ranking_service.h"
#include "ranking_generated.h"

namespace mir2::logic {

namespace {

std::vector<uint8_t> BuildRankingRspPayload(
    bool success,
    mir2::common::ErrorCode code,
    mir2::proto::RankingType type,
    uint32_t total_count,
    const std::vector<RankingEntryView>& entries) {
  flatbuffers::FlatBufferBuilder builder;
  std::vector<flatbuffers::Offset<mir2::proto::RankEntry>> entry_offsets;
  entry_offsets.reserve(entries.size());
  for (const auto& entry : entries) {
    const auto name_offset = builder.CreateString(entry.name);
    const auto extra_offset = builder.CreateString(entry.extra);
    entry_offsets.emplace_back(mir2::proto::CreateRankEntry(builder,
                                                            entry.rank,
                                                            entry.entity_id,
                                                            name_offset,
                                                            entry.value,
                                                            extra_offset));
  }
  const auto entries_vec = builder.CreateVector(entry_offsets);
  const auto rsp = mir2::proto::CreateRankingRsp(builder,
                                                 success,
                                                 static_cast<int>(ToProtoError(code)),
                                                 type,
                                                 total_count,
                                                 entries_vec);
  builder.Finish(rsp);
  const uint8_t* data = builder.GetBufferPointer();
  return {data, data + builder.GetSize()};
}

std::vector<uint8_t> BuildMyRankRspPayload(bool success,
                                           mir2::common::ErrorCode code,
                                           mir2::proto::RankingType type,
                                           uint32_t rank,
                                           int64_t value) {
  flatbuffers::FlatBufferBuilder builder;
  const auto rsp = mir2::proto::CreateRankingMyRankRsp(
      builder, success, static_cast<int>(ToProtoError(code)), type, rank, value);
  builder.Finish(rsp);
  const uint8_t* data = builder.GetBufferPointer();
  return {data, data + builder.GetSize()};
}

}  // namespace

RankingHandler::RankingHandler(ResponseSender& response_sender,
                               RankingService& ranking_service,
                               entt::registry& ecs_registry)
    : response_sender_(response_sender),
      ranking_service_(ranking_service),
      ecs_registry_(ecs_registry) {}

Task<void> RankingHandler::HandleMessage(HandlerContext ctx,
                                         const uint8_t* payload,
                                         size_t payload_size) {
  try {
    if (!payload || payload_size == 0) {
      co_await SendRankingRsp(ctx.client_id,
                              false,
                              mir2::common::ErrorCode::kInvalidAction,
                              mir2::proto::RankingType::LEVEL,
                              0,
                              {});
      co_return;
    }

    switch (static_cast<mir2::common::MsgId>(ctx.msg_id)) {
      case mir2::common::MsgId::kRankingReq: {
        flatbuffers::Verifier verifier(payload, payload_size);
        if (!verifier.VerifyBuffer<mir2::proto::RankingReq>(nullptr)) {
          co_await SendRankingRsp(ctx.client_id,
                                  false,
                                  mir2::common::ErrorCode::kInvalidAction,
                                  mir2::proto::RankingType::LEVEL,
                                  0,
                                  {});
          co_return;
        }
        const auto* req = flatbuffers::GetRoot<mir2::proto::RankingReq>(payload);
        co_await HandleRanking(std::move(ctx), req);
        co_return;
      }
      case mir2::common::MsgId::kRankingMyRankReq: {
        flatbuffers::Verifier verifier(payload, payload_size);
        if (!verifier.VerifyBuffer<mir2::proto::RankingMyRankReq>(nullptr)) {
          co_await SendMyRankRsp(ctx.client_id,
                                 false,
                                 mir2::common::ErrorCode::kInvalidAction,
                                 mir2::proto::RankingType::LEVEL,
                                 0,
                                 0);
          co_return;
        }
        const auto* req = flatbuffers::GetRoot<mir2::proto::RankingMyRankReq>(payload);
        co_await HandleMyRank(std::move(ctx), req);
        co_return;
      }
      default:
        SYSLOG_WARN("RankingHandler unsupported msg_id={} client_id={}",
                    ctx.msg_id,
                    ctx.client_id);
        co_return;
    }
  } catch (const std::exception& ex) {
    SYSLOG_ERROR("RankingHandler exception msg_id={} client_id={} error={}",
                 ctx.msg_id,
                 ctx.client_id,
                 ex.what());
    co_return;
  } catch (...) {
    SYSLOG_ERROR("RankingHandler exception msg_id={} client_id={} error=unknown",
                 ctx.msg_id,
                 ctx.client_id);
    co_return;
  }
}

Task<void> RankingHandler::HandleRanking(HandlerContext ctx,
                                         const mir2::proto::RankingReq* req) {
  if (!req || !IsSupportedRankingType(req->ranking_type())) {
    co_await SendRankingRsp(ctx.client_id,
                            false,
                            mir2::common::ErrorCode::kRankingTypeInvalid,
                            req ? req->ranking_type() : mir2::proto::RankingType::LEVEL,
                            0,
                            {});
    co_return;
  }

  const auto result = ranking_service_.GetRanking(
      req->ranking_type(), req->page(), req->page_size());
  co_await SendRankingRsp(ctx.client_id,
                          true,
                          mir2::common::ErrorCode::kOk,
                          req->ranking_type(),
                          result.total_count,
                          result.entries);
}

Task<void> RankingHandler::HandleMyRank(HandlerContext ctx,
                                        const mir2::proto::RankingMyRankReq* req) {
  if (!req || !IsSupportedRankingType(req->ranking_type())) {
    co_await SendMyRankRsp(ctx.client_id,
                           false,
                           mir2::common::ErrorCode::kRankingTypeInvalid,
                           req ? req->ranking_type() : mir2::proto::RankingType::LEVEL,
                           0,
                           0);
    co_return;
  }

  const uint64_t entity_id = ResolveEntityIdForMyRank(ctx, req->ranking_type());
  if (entity_id == 0) {
    co_await SendMyRankRsp(ctx.client_id,
                           false,
                           mir2::common::ErrorCode::kTargetNotFound,
                           req->ranking_type(),
                           0,
                           0);
    co_return;
  }

  const auto my_rank = ranking_service_.GetMyRank(req->ranking_type(), entity_id);
  if (!my_rank.has_value()) {
    co_await SendMyRankRsp(ctx.client_id,
                           true,
                           mir2::common::ErrorCode::kOk,
                           req->ranking_type(),
                           0,
                           0);
    co_return;
  }

  co_await SendMyRankRsp(ctx.client_id,
                         true,
                         mir2::common::ErrorCode::kOk,
                         req->ranking_type(),
                         my_rank->rank,
                         my_rank->value);
}

bool RankingHandler::IsSupportedRankingType(mir2::proto::RankingType type) const {
  switch (type) {
    case mir2::proto::RankingType::LEVEL:
    case mir2::proto::RankingType::PK:
    case mir2::proto::RankingType::GUILD:
    case mir2::proto::RankingType::GOLD:
      return true;
    default:
      return false;
  }
}

uint64_t RankingHandler::ResolveEntityIdForMyRank(
    HandlerContext ctx, mir2::proto::RankingType type) const {
  if (ctx.entity == entt::null || !ecs_registry_.valid(ctx.entity)) {
    return 0;
  }

  if (type == mir2::proto::RankingType::GUILD) {
    const auto* guild_member = ecs_registry_.try_get<mir2::ecs::GuildMemberComponent>(ctx.entity);
    if (!guild_member || guild_member->guild_id == mir2::ecs::kInvalidGuildId) {
      return 0;
    }
    return guild_member->guild_id;
  }

  const auto* identity = ecs_registry_.try_get<mir2::ecs::CharacterIdentityComponent>(ctx.entity);
  if (!identity) {
    return 0;
  }
  return identity->id;
}

Task<void> RankingHandler::SendRankingRsp(
    uint64_t client_id,
    bool success,
    mir2::common::ErrorCode code,
    mir2::proto::RankingType type,
    uint32_t total_count,
    const std::vector<RankingEntryView>& entries) {
  co_await response_sender_.SendAsync(
      client_id,
      static_cast<uint16_t>(mir2::common::MsgId::kRankingRsp),
      BuildRankingRspPayload(success, code, type, total_count, entries));
}

Task<void> RankingHandler::SendMyRankRsp(uint64_t client_id,
                                         bool success,
                                         mir2::common::ErrorCode code,
                                         mir2::proto::RankingType type,
                                         uint32_t rank,
                                         int64_t value) {
  co_await response_sender_.SendAsync(
      client_id,
      static_cast<uint16_t>(mir2::common::MsgId::kRankingMyRankRsp),
      BuildMyRankRspPayload(success, code, type, rank, value));
}

}  // namespace mir2::logic
