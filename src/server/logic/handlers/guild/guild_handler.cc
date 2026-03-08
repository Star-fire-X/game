#include "logic/handlers/guild/guild_handler.h"

#include <exception>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <flatbuffers/flatbuffers.h>

#include "common/enums.h"
#include "ecs/components/character_components.h"
#include "ecs/components/guild_component.h"
#include "ecs/systems/guild_system.h"
#include "guild_generated.h"
#include "log/logger.h"
#include "logic/response_sender.h"
#include "logic/services/player_presence_service.h"

namespace mir2::logic {

namespace {

bool IsGuildCreateReq(uint16_t msg_id) {
  return msg_id == static_cast<uint16_t>(common::MsgId::kGuildCreateReq) ||
         msg_id == static_cast<uint16_t>(mir2::proto::GuildMessageType::CREATE);
}

bool IsGuildJoinReq(uint16_t msg_id) {
  return msg_id == static_cast<uint16_t>(common::MsgId::kGuildJoinReq) ||
         msg_id == static_cast<uint16_t>(mir2::proto::GuildMessageType::JOIN);
}

bool IsGuildLeaveReq(uint16_t msg_id) {
  return msg_id == static_cast<uint16_t>(common::MsgId::kGuildLeaveReq) ||
         msg_id == static_cast<uint16_t>(mir2::proto::GuildMessageType::LEAVE);
}

bool IsGuildKickReq(uint16_t msg_id) {
  return msg_id == static_cast<uint16_t>(common::MsgId::kGuildKickReq) ||
         msg_id == static_cast<uint16_t>(mir2::proto::GuildMessageType::KICK);
}

bool IsGuildDeclareWarReq(uint16_t msg_id) {
  return msg_id == static_cast<uint16_t>(common::MsgId::kGuildDeclareWarReq) ||
         msg_id == static_cast<uint16_t>(mir2::proto::GuildMessageType::DECLARE_WAR);
}

bool IsGuildCancelWarReq(uint16_t msg_id) {
  return msg_id == static_cast<uint16_t>(common::MsgId::kGuildCancelWarReq) ||
         msg_id == static_cast<uint16_t>(mir2::proto::GuildMessageType::CANCEL_WAR);
}

bool IsGuildMakeAllyReq(uint16_t msg_id) {
  return msg_id == static_cast<uint16_t>(common::MsgId::kGuildMakeAllyReq) ||
         msg_id == static_cast<uint16_t>(mir2::proto::GuildMessageType::MAKE_ALLY);
}

bool IsGuildBreakAllyReq(uint16_t msg_id) {
  return msg_id == static_cast<uint16_t>(common::MsgId::kGuildBreakAllyReq) ||
         msg_id == static_cast<uint16_t>(mir2::proto::GuildMessageType::BREAK_ALLY);
}

bool IsGuildUpdateNoticeReq(uint16_t msg_id) {
  return msg_id == static_cast<uint16_t>(common::MsgId::kGuildUpdateNoticeReq) ||
         msg_id == static_cast<uint16_t>(mir2::proto::GuildMessageType::UPDATE_NOTICE);
}

bool IsGuildUpdateRankReq(uint16_t msg_id) {
  return msg_id == static_cast<uint16_t>(common::MsgId::kGuildUpdateRankReq) ||
         msg_id == static_cast<uint16_t>(mir2::proto::GuildMessageType::UPDATE_RANK);
}

uint16_t ResolveGuildResponseMsgId(uint16_t request_msg_id) {
  if (IsGuildCreateReq(request_msg_id)) {
    if (request_msg_id == static_cast<uint16_t>(common::MsgId::kGuildCreateReq)) {
      return static_cast<uint16_t>(common::MsgId::kGuildCreateRsp);
    }
    return static_cast<uint16_t>(mir2::proto::GuildMessageType::CREATE);
  }
  if (IsGuildJoinReq(request_msg_id)) {
    if (request_msg_id == static_cast<uint16_t>(common::MsgId::kGuildJoinReq)) {
      return static_cast<uint16_t>(common::MsgId::kGuildJoinRsp);
    }
    return static_cast<uint16_t>(mir2::proto::GuildMessageType::JOIN);
  }
  if (IsGuildLeaveReq(request_msg_id)) {
    if (request_msg_id == static_cast<uint16_t>(common::MsgId::kGuildLeaveReq)) {
      return static_cast<uint16_t>(common::MsgId::kGuildLeaveRsp);
    }
    return static_cast<uint16_t>(mir2::proto::GuildMessageType::LEAVE);
  }
  if (IsGuildKickReq(request_msg_id)) {
    if (request_msg_id == static_cast<uint16_t>(common::MsgId::kGuildKickReq)) {
      return static_cast<uint16_t>(common::MsgId::kGuildKickRsp);
    }
    return static_cast<uint16_t>(mir2::proto::GuildMessageType::KICK);
  }
  if (IsGuildDeclareWarReq(request_msg_id)) {
    if (request_msg_id == static_cast<uint16_t>(common::MsgId::kGuildDeclareWarReq)) {
      return static_cast<uint16_t>(common::MsgId::kGuildDeclareWarRsp);
    }
    return static_cast<uint16_t>(mir2::proto::GuildMessageType::DECLARE_WAR);
  }
  if (IsGuildCancelWarReq(request_msg_id)) {
    if (request_msg_id == static_cast<uint16_t>(common::MsgId::kGuildCancelWarReq)) {
      return static_cast<uint16_t>(common::MsgId::kGuildCancelWarRsp);
    }
    return static_cast<uint16_t>(mir2::proto::GuildMessageType::CANCEL_WAR);
  }
  if (IsGuildMakeAllyReq(request_msg_id)) {
    if (request_msg_id == static_cast<uint16_t>(common::MsgId::kGuildMakeAllyReq)) {
      return static_cast<uint16_t>(common::MsgId::kGuildMakeAllyRsp);
    }
    return static_cast<uint16_t>(mir2::proto::GuildMessageType::MAKE_ALLY);
  }
  if (IsGuildBreakAllyReq(request_msg_id)) {
    if (request_msg_id == static_cast<uint16_t>(common::MsgId::kGuildBreakAllyReq)) {
      return static_cast<uint16_t>(common::MsgId::kGuildBreakAllyRsp);
    }
    return static_cast<uint16_t>(mir2::proto::GuildMessageType::BREAK_ALLY);
  }
  if (IsGuildUpdateNoticeReq(request_msg_id)) {
    if (request_msg_id ==
        static_cast<uint16_t>(common::MsgId::kGuildUpdateNoticeReq)) {
      return static_cast<uint16_t>(common::MsgId::kGuildUpdateNoticeRsp);
    }
    return static_cast<uint16_t>(mir2::proto::GuildMessageType::UPDATE_NOTICE);
  }
  if (IsGuildUpdateRankReq(request_msg_id)) {
    if (request_msg_id == static_cast<uint16_t>(common::MsgId::kGuildUpdateRankReq)) {
      return static_cast<uint16_t>(common::MsgId::kGuildUpdateRankRsp);
    }
    return static_cast<uint16_t>(mir2::proto::GuildMessageType::UPDATE_RANK);
  }
  return request_msg_id;
}

// Map GuildSystem error codes to protocol error codes.
mir2::common::ErrorCode MapGuildErrorCode(int system_error_code) {
  switch (system_error_code) {
    case 0:
      return mir2::common::ErrorCode::kOk;
    case -1:
      return mir2::common::ErrorCode::kInvalidAction;
    case -2:
      return mir2::common::ErrorCode::kInvalidAction;
    case -3:
      return mir2::common::ErrorCode::kInvalidAction;
    case -4:
      return mir2::common::ErrorCode::kNameExists;
    default:
      return mir2::common::ErrorCode::kUnknown;
  }
}

mir2::common::ErrorCode MapAllianceErrorCode(int system_error_code) {
  switch (system_error_code) {
    case 0:
      return mir2::common::ErrorCode::kOk;
    case -1:
      return mir2::common::ErrorCode::kInvalidAction;
    case -2:
      return mir2::common::ErrorCode::kTargetNotFound;
    case -3:
      return mir2::common::ErrorCode::kInvalidAction;
    case -4:
      return mir2::common::ErrorCode::kTargetRefused;
    default:
      return mir2::common::ErrorCode::kUnknown;
  }
}

std::vector<uint8_t> BuildCreateGuildRsp(bool success,
                                         int error_code,
                                         mir2::ecs::GuildId guild_id) {
  flatbuffers::FlatBufferBuilder builder;
  flatbuffers::Offset<mir2::proto::GuildInfo> guild_info_offset = 0;
  if (success && guild_id != mir2::ecs::kInvalidGuildId) {
    guild_info_offset = mir2::proto::CreateGuildInfo(builder, guild_id);
  }
  const auto rsp = mir2::proto::CreateCreateGuildResponse(
      builder, success, error_code, guild_info_offset);
  builder.Finish(rsp);
  const uint8_t* data = builder.GetBufferPointer();
  return std::vector<uint8_t>(data, data + builder.GetSize());
}

std::vector<uint8_t> BuildJoinGuildRsp(bool success, int error_code) {
  flatbuffers::FlatBufferBuilder builder;
  const auto rsp = mir2::proto::CreateJoinGuildResponse(builder, success, error_code);
  builder.Finish(rsp);
  const uint8_t* data = builder.GetBufferPointer();
  return std::vector<uint8_t>(data, data + builder.GetSize());
}

std::vector<uint8_t> BuildLeaveGuildRsp(bool success, int error_code) {
  flatbuffers::FlatBufferBuilder builder;
  const auto rsp = mir2::proto::CreateLeaveGuildResponse(builder, success, error_code);
  builder.Finish(rsp);
  const uint8_t* data = builder.GetBufferPointer();
  return std::vector<uint8_t>(data, data + builder.GetSize());
}

std::vector<uint8_t> BuildKickGuildRsp(bool success, int error_code) {
  flatbuffers::FlatBufferBuilder builder;
  const auto rsp = mir2::proto::CreateKickGuildResponse(builder, success, error_code);
  builder.Finish(rsp);
  const uint8_t* data = builder.GetBufferPointer();
  return std::vector<uint8_t>(data, data + builder.GetSize());
}

std::vector<uint8_t> BuildDeclareWarRsp(bool success, int error_code) {
  flatbuffers::FlatBufferBuilder builder;
  const auto rsp = mir2::proto::CreateDeclareWarResponse(builder, success, error_code);
  builder.Finish(rsp);
  const uint8_t* data = builder.GetBufferPointer();
  return std::vector<uint8_t>(data, data + builder.GetSize());
}

std::vector<uint8_t> BuildCancelWarRsp(bool success, int error_code) {
  flatbuffers::FlatBufferBuilder builder;
  const auto rsp = mir2::proto::CreateCancelWarResponse(builder, success, error_code);
  builder.Finish(rsp);
  const uint8_t* data = builder.GetBufferPointer();
  return std::vector<uint8_t>(data, data + builder.GetSize());
}

std::vector<uint8_t> BuildMakeAllianceRsp(bool success, int error_code) {
  flatbuffers::FlatBufferBuilder builder;
  const auto rsp =
      mir2::proto::CreateMakeAllianceResponse(builder, success, error_code);
  builder.Finish(rsp);
  const uint8_t* data = builder.GetBufferPointer();
  return std::vector<uint8_t>(data, data + builder.GetSize());
}

std::vector<uint8_t> BuildBreakAllianceRsp(bool success, int error_code) {
  flatbuffers::FlatBufferBuilder builder;
  const auto rsp =
      mir2::proto::CreateBreakAllianceResponse(builder, success, error_code);
  builder.Finish(rsp);
  const uint8_t* data = builder.GetBufferPointer();
  return std::vector<uint8_t>(data, data + builder.GetSize());
}

std::vector<uint8_t> BuildUpdateNoticeRsp(bool success, int error_code) {
  flatbuffers::FlatBufferBuilder builder;
  const auto rsp =
      mir2::proto::CreateUpdateNoticeResponse(builder, success, error_code);
  builder.Finish(rsp);
  const uint8_t* data = builder.GetBufferPointer();
  return std::vector<uint8_t>(data, data + builder.GetSize());
}

std::vector<uint8_t> BuildUpdateRankRsp(bool success, int error_code) {
  flatbuffers::FlatBufferBuilder builder;
  const auto rsp = mir2::proto::CreateUpdateRankResponse(builder, success, error_code);
  builder.Finish(rsp);
  const uint8_t* data = builder.GetBufferPointer();
  return std::vector<uint8_t>(data, data + builder.GetSize());
}

std::string GetCharacterNameByEntity(entt::registry& registry, entt::entity entity) {
  if (entity == entt::null || !registry.valid(entity)) {
    return {};
  }
  const auto* identity = registry.try_get<mir2::ecs::CharacterIdentityComponent>(entity);
  if (!identity) {
    return {};
  }
  return identity->name;
}

mir2::ecs::CharacterId GetCharacterIdByEntity(entt::registry& registry, entt::entity entity) {
  if (entity == entt::null || !registry.valid(entity)) {
    return mir2::ecs::kInvalidCharacterId;
  }
  const auto* identity = registry.try_get<mir2::ecs::CharacterIdentityComponent>(entity);
  if (!identity || identity->id == mir2::ecs::kInvalidCharacterId) {
    return mir2::ecs::kInvalidCharacterId;
  }
  return identity->id;
}

std::string GetCharacterNameById(entt::registry& registry,
                                 mir2::ecs::CharacterId character_id) {
  if (character_id == mir2::ecs::kInvalidCharacterId) {
    return {};
  }
  auto view = registry.view<mir2::ecs::CharacterIdentityComponent>();
  for (const auto entity : view) {
    const auto& identity = view.get<mir2::ecs::CharacterIdentityComponent>(entity);
    if (identity.id == character_id) {
      return identity.name;
    }
  }
  return {};
}

const mir2::ecs::GuildComponent* FindGuildById(entt::registry& registry,
                                                mir2::ecs::GuildId guild_id) {
  if (guild_id == mir2::ecs::kInvalidGuildId) {
    return nullptr;
  }
  auto view = registry.view<mir2::ecs::GuildComponent>();
  for (const auto entity : view) {
    const auto& guild = view.get<mir2::ecs::GuildComponent>(entity);
    if (guild.guild_id == guild_id) {
      return &guild;
    }
  }
  return nullptr;
}

std::vector<uint8_t> BuildGuildInfoSyncPayload(
    entt::registry& registry,
    const mir2::ecs::GuildComponent* guild) {
  flatbuffers::FlatBufferBuilder builder;
  flatbuffers::Offset<mir2::proto::GuildInfo> guild_info_offset = 0;

  if (guild) {
    std::vector<flatbuffers::Offset<flatbuffers::String>> notice_offsets;
    notice_offsets.reserve(guild->notice_list.size());
    for (const auto& notice : guild->notice_list) {
      notice_offsets.emplace_back(builder.CreateString(notice));
    }

    std::vector<flatbuffers::Offset<mir2::proto::GuildRankInfo>> rank_offsets;
    rank_offsets.reserve(guild->ranks.size());
    for (const auto& rank : guild->ranks) {
      std::vector<flatbuffers::Offset<flatbuffers::String>> member_name_offsets;
      member_name_offsets.reserve(rank.member_ids.size());
      for (const auto member_id : rank.member_ids) {
        const auto member_name = GetCharacterNameById(registry, member_id);
        member_name_offsets.emplace_back(builder.CreateString(member_name));
      }

      const auto member_names_vec = builder.CreateVector(member_name_offsets);
      const auto rank_name_offset = builder.CreateString(rank.rank_name);
      rank_offsets.emplace_back(mir2::proto::CreateGuildRankInfo(
          builder, rank.rank, rank_name_offset, member_names_vec));
    }

    std::vector<flatbuffers::Offset<mir2::proto::GuildWarInfo>> war_offsets;
    war_offsets.reserve(guild->war_guilds.size());
    for (const auto& war_info : guild->war_guilds) {
      war_offsets.emplace_back(mir2::proto::CreateGuildWarInfo(
          builder,
          war_info.enemy_guild_id,
          war_info.start_time,
          war_info.remain_time));
    }

    std::vector<uint32_t> ally_guild_ids;
    ally_guild_ids.reserve(guild->ally_guild_ids.size());
    for (const auto ally_guild_id : guild->ally_guild_ids) {
      ally_guild_ids.push_back(ally_guild_id);
    }

    std::vector<flatbuffers::Offset<flatbuffers::String>> fight_member_offsets;
    fight_member_offsets.reserve(guild->fight_members.size());
    for (const auto member_entity : guild->fight_members) {
      fight_member_offsets.emplace_back(
          builder.CreateString(GetCharacterNameByEntity(registry, member_entity)));
    }

    const auto guild_name_offset = builder.CreateString(guild->guild_name);
    const auto leader_name_offset =
        builder.CreateString(GetCharacterNameByEntity(registry, guild->leader));
    const auto notice_vec = builder.CreateVector(notice_offsets);
    const auto rank_vec = builder.CreateVector(rank_offsets);
    const auto war_vec = builder.CreateVector(war_offsets);
    const auto ally_vec = builder.CreateVector(ally_guild_ids);
    const auto fight_member_vec = builder.CreateVector(fight_member_offsets);
    const auto leader_id = GetCharacterIdByEntity(registry, guild->leader);

    guild_info_offset = mir2::proto::CreateGuildInfo(
        builder,
        guild->guild_id,
        guild_name_offset,
        1,
        static_cast<uint32_t>(guild->members.size()),
        leader_id,
        leader_name_offset,
        static_cast<uint32_t>(guild->max_members),
        notice_vec,
        rank_vec,
        war_vec,
        ally_vec,
        guild->allow_ally,
        guild->in_team_fight,
        guild->match_point,
        fight_member_vec);
  }

  const auto sync = mir2::proto::CreateGuildInfoSync(builder, guild_info_offset);
  builder.Finish(sync);
  const uint8_t* data = builder.GetBufferPointer();
  return std::vector<uint8_t>(data, data + builder.GetSize());
}

std::optional<entt::entity> FindEntityByCharacterId(entt::registry& registry,
                                                     mir2::ecs::CharacterId character_id) {
  if (character_id == mir2::ecs::kInvalidCharacterId) {
    return std::nullopt;
  }
  auto view = registry.view<mir2::ecs::CharacterIdentityComponent>();
  for (const auto entity : view) {
    const auto& identity = view.get<mir2::ecs::CharacterIdentityComponent>(entity);
    if (identity.id == character_id) {
      return entity;
    }
  }
  return std::nullopt;
}

}  // namespace

GuildHandler::GuildHandler(CoroutineExecutor& executor,
                           ResponseSender& response_sender,
                           ClientRegistry& registry,
                           PlayerPresenceService& player_presence_service,
                           mir2::ecs::GuildSystem& guild_system,
                           entt::registry& ecs_registry)
    : response_sender_(response_sender),
      player_presence_service_(player_presence_service),
      guild_system_(guild_system),
      ecs_registry_(ecs_registry) {
  // Unused parameters: executor, registry.
  (void)executor;
  (void)registry;
}

Task<void> GuildHandler::HandleMessage(HandlerContext ctx,
                                       const uint8_t* payload,
                                       size_t payload_size) {
  bool send_fallback = false;
  try {
    if (IsGuildCreateReq(ctx.msg_id)) {
      co_await HandleCreateGuild(ctx, payload, payload_size);
      co_return;
    }
    if (IsGuildJoinReq(ctx.msg_id)) {
      co_await HandleJoinGuild(ctx, payload, payload_size);
      co_return;
    }
    if (IsGuildLeaveReq(ctx.msg_id)) {
      co_await HandleLeaveGuild(ctx, payload, payload_size);
      co_return;
    }
    if (IsGuildKickReq(ctx.msg_id)) {
      co_await HandleKickGuild(ctx, payload, payload_size);
      co_return;
    }
    if (IsGuildDeclareWarReq(ctx.msg_id)) {
      co_await HandleDeclareWar(ctx, payload, payload_size);
      co_return;
    }
    if (IsGuildCancelWarReq(ctx.msg_id)) {
      co_await HandleCancelWar(ctx, payload, payload_size);
      co_return;
    }
    if (IsGuildMakeAllyReq(ctx.msg_id)) {
      co_await HandleMakeAlliance(ctx, payload, payload_size);
      co_return;
    }
    if (IsGuildBreakAllyReq(ctx.msg_id)) {
      co_await HandleBreakAlliance(ctx, payload, payload_size);
      co_return;
    }
    if (IsGuildUpdateNoticeReq(ctx.msg_id)) {
      co_await HandleUpdateNotice(ctx, payload, payload_size);
      co_return;
    }
    if (IsGuildUpdateRankReq(ctx.msg_id)) {
      co_await HandleUpdateRank(ctx, payload, payload_size);
      co_return;
    }

    SYSLOG_WARN("GuildHandler unknown msg_id={} (client_id={})",
                ctx.msg_id, ctx.client_id);
    co_return;
  } catch (const std::exception& ex) {
    SYSLOG_ERROR("GuildHandler exception client_id={} msg_id={} error={}",
                 ctx.client_id, ctx.msg_id, ex.what());
    send_fallback = true;
  } catch (...) {
    SYSLOG_ERROR("GuildHandler exception client_id={} msg_id={} error=unknown",
                 ctx.client_id, ctx.msg_id);
    send_fallback = true;
  }

  if (send_fallback) {
    try {
      if (IsGuildCreateReq(ctx.msg_id)) {
        co_await SendCreateGuildResponse(
            ctx,
            ctx.msg_id,
            false,
            static_cast<int>(mir2::common::ErrorCode::kInvalidAction),
            mir2::ecs::kInvalidGuildId);
      } else if (IsGuildJoinReq(ctx.msg_id)) {
        co_await SendJoinGuildResponse(
            ctx,
            ctx.msg_id,
            false,
            static_cast<int>(mir2::common::ErrorCode::kInvalidAction));
      } else if (IsGuildLeaveReq(ctx.msg_id)) {
        co_await SendLeaveGuildResponse(
            ctx,
            ctx.msg_id,
            false,
            static_cast<int>(mir2::common::ErrorCode::kInvalidAction));
      } else if (IsGuildKickReq(ctx.msg_id)) {
        co_await SendKickGuildResponse(
            ctx,
            ctx.msg_id,
            false,
            static_cast<int>(mir2::common::ErrorCode::kInvalidAction));
      } else if (IsGuildDeclareWarReq(ctx.msg_id)) {
        co_await SendDeclareWarResponse(
            ctx,
            ctx.msg_id,
            false,
            static_cast<int>(mir2::common::ErrorCode::kInvalidAction));
      } else if (IsGuildCancelWarReq(ctx.msg_id)) {
        co_await SendCancelWarResponse(
            ctx,
            ctx.msg_id,
            false,
            static_cast<int>(mir2::common::ErrorCode::kInvalidAction));
      } else if (IsGuildMakeAllyReq(ctx.msg_id)) {
        co_await SendMakeAllianceResponse(
            ctx,
            ctx.msg_id,
            false,
            static_cast<int>(mir2::common::ErrorCode::kInvalidAction));
      } else if (IsGuildBreakAllyReq(ctx.msg_id)) {
        co_await SendBreakAllianceResponse(
            ctx,
            ctx.msg_id,
            false,
            static_cast<int>(mir2::common::ErrorCode::kInvalidAction));
      } else if (IsGuildUpdateNoticeReq(ctx.msg_id)) {
        co_await SendUpdateNoticeResponse(
            ctx,
            ctx.msg_id,
            false,
            static_cast<int>(mir2::common::ErrorCode::kInvalidAction));
      } else if (IsGuildUpdateRankReq(ctx.msg_id)) {
        co_await SendUpdateRankResponse(
            ctx,
            ctx.msg_id,
            false,
            static_cast<int>(mir2::common::ErrorCode::kInvalidAction));
      }
    } catch (const std::exception& send_ex) {
      SYSLOG_ERROR("GuildHandler fallback send failed client_id={} msg_id={} error={}",
                   ctx.client_id, ctx.msg_id, send_ex.what());
    } catch (...) {
      SYSLOG_ERROR("GuildHandler fallback send failed client_id={} msg_id={} error=unknown",
                   ctx.client_id, ctx.msg_id);
    }
  }
  co_return;
}

Task<void> GuildHandler::HandleCreateGuild(HandlerContext ctx,
                                           const uint8_t* payload,
                                           size_t payload_size) {
  const uint16_t request_msg_id = ctx.msg_id;
  const uint64_t client_id = ctx.client_id;
  if (!payload || payload_size == 0) {
    SYSLOG_WARN("GuildHandler create guild empty payload (client_id={})", ctx.client_id);
    co_await SendCreateGuildResponse(std::move(ctx), request_msg_id,
                                     false,
                                     static_cast<int>(mir2::common::ErrorCode::kInvalidAction),
                                     0);
    co_return;
  }

  flatbuffers::Verifier verifier(payload, payload_size);
  if (!verifier.VerifyBuffer<mir2::proto::CreateGuildRequest>(nullptr)) {
    SYSLOG_WARN("GuildHandler create guild invalid payload (client_id={})", ctx.client_id);
    co_await SendCreateGuildResponse(std::move(ctx), request_msg_id,
                                     false,
                                     static_cast<int>(mir2::common::ErrorCode::kInvalidAction),
                                     0);
    co_return;
  }

  const auto* req = flatbuffers::GetRoot<mir2::proto::CreateGuildRequest>(payload);
  if (!req || !req->guild_name() || req->guild_name()->str().empty()) {
    SYSLOG_WARN("GuildHandler create guild missing name (client_id={})", ctx.client_id);
    co_await SendCreateGuildResponse(std::move(ctx), request_msg_id,
                                     false,
                                     static_cast<int>(mir2::common::ErrorCode::kInvalidAction),
                                     0);
    co_return;
  }

  auto player_entity = player_presence_service_.FindEntity(ctx.client_id);
  if (!player_entity.has_value()) {
    SYSLOG_WARN("GuildHandler create guild missing player (client_id={})", ctx.client_id);
    co_await SendCreateGuildResponse(std::move(ctx), request_msg_id,
                                     false,
                                     static_cast<int>(mir2::common::ErrorCode::kInvalidAction),
                                     0);
    co_return;
  }

  const int result = guild_system_.CreateGuild(
      ecs_registry_, *player_entity, req->guild_name()->str());
  const bool success = (result == 0);
  const auto error_code = MapGuildErrorCode(result);

  mir2::ecs::GuildId guild_id = mir2::ecs::kInvalidGuildId;
  if (success) {
    if (const auto* member =
            ecs_registry_.try_get<mir2::ecs::GuildMemberComponent>(*player_entity)) {
      guild_id = member->guild_id;
    }
  }

  co_await SendCreateGuildResponse(
      std::move(ctx), request_msg_id, success, static_cast<int>(error_code), guild_id);

  if (success) {
    co_await SendGuildInfoSync(client_id, guild_id);
    SYSLOG_INFO("GuildHandler created guild client_id={} guild_id={} name={}",
                client_id, guild_id, req->guild_name()->str());
  } else {
    SYSLOG_WARN("GuildHandler create guild failed client_id={} error={}",
                client_id, result);
  }
}

Task<void> GuildHandler::HandleJoinGuild(HandlerContext ctx,
                                         const uint8_t* payload,
                                         size_t payload_size) {
  const uint16_t request_msg_id = ctx.msg_id;
  const uint64_t client_id = ctx.client_id;
  if (!payload || payload_size == 0) {
    SYSLOG_WARN("GuildHandler join guild empty payload (client_id={})", ctx.client_id);
    co_await SendJoinGuildResponse(std::move(ctx), request_msg_id,
                                   false,
                                   static_cast<int>(mir2::common::ErrorCode::kInvalidAction));
    co_return;
  }

  flatbuffers::Verifier verifier(payload, payload_size);
  if (!verifier.VerifyBuffer<mir2::proto::JoinGuildRequest>(nullptr)) {
    SYSLOG_WARN("GuildHandler join guild invalid payload (client_id={})", ctx.client_id);
    co_await SendJoinGuildResponse(std::move(ctx), request_msg_id,
                                   false,
                                   static_cast<int>(mir2::common::ErrorCode::kInvalidAction));
    co_return;
  }

  const auto* req = flatbuffers::GetRoot<mir2::proto::JoinGuildRequest>(payload);
  const mir2::ecs::GuildId guild_id =
      req ? req->guild_id() : mir2::ecs::kInvalidGuildId;
  if (guild_id == mir2::ecs::kInvalidGuildId) {
    SYSLOG_WARN("GuildHandler join guild invalid guild_id (client_id={})", ctx.client_id);
    co_await SendJoinGuildResponse(std::move(ctx), request_msg_id,
                                   false,
                                   static_cast<int>(mir2::common::ErrorCode::kInvalidAction));
    co_return;
  }

  auto player_entity = player_presence_service_.FindEntity(ctx.client_id);
  if (!player_entity.has_value()) {
    SYSLOG_WARN("GuildHandler join guild missing player (client_id={})", ctx.client_id);
    co_await SendJoinGuildResponse(std::move(ctx), request_msg_id,
                                   false,
                                   static_cast<int>(mir2::common::ErrorCode::kInvalidAction));
    co_return;
  }

  const bool success = guild_system_.JoinGuild(ecs_registry_, *player_entity, guild_id);
  const auto error_code =
      success ? mir2::common::ErrorCode::kOk : mir2::common::ErrorCode::kInvalidAction;

  co_await SendJoinGuildResponse(
      std::move(ctx), request_msg_id, success, static_cast<int>(error_code));

  if (success) {
    co_await SendGuildInfoSync(client_id, guild_id);
    SYSLOG_INFO("GuildHandler joined guild client_id={} guild_id={}",
                client_id, guild_id);
  } else {
    SYSLOG_WARN("GuildHandler join guild failed client_id={} guild_id={}",
                client_id, guild_id);
  }
}

Task<void> GuildHandler::HandleLeaveGuild(HandlerContext ctx,
                                          const uint8_t* payload,
                                          size_t payload_size) {
  const uint16_t request_msg_id = ctx.msg_id;
  const uint64_t client_id = ctx.client_id;
  if (!payload || payload_size == 0) {
    SYSLOG_WARN("GuildHandler leave guild empty payload (client_id={})", ctx.client_id);
    co_await SendLeaveGuildResponse(std::move(ctx), request_msg_id,
                                    false,
                                    static_cast<int>(mir2::common::ErrorCode::kInvalidAction));
    co_return;
  }

  flatbuffers::Verifier verifier(payload, payload_size);
  if (!verifier.VerifyBuffer<mir2::proto::LeaveGuildRequest>(nullptr)) {
    SYSLOG_WARN("GuildHandler leave guild invalid payload (client_id={})", ctx.client_id);
    co_await SendLeaveGuildResponse(std::move(ctx), request_msg_id,
                                    false,
                                    static_cast<int>(mir2::common::ErrorCode::kInvalidAction));
    co_return;
  }

  auto player_entity = player_presence_service_.FindEntity(ctx.client_id);
  if (!player_entity.has_value()) {
    SYSLOG_WARN("GuildHandler leave guild missing player (client_id={})", ctx.client_id);
    co_await SendLeaveGuildResponse(std::move(ctx), request_msg_id,
                                    false,
                                    static_cast<int>(mir2::common::ErrorCode::kInvalidAction));
    co_return;
  }

  const bool success = guild_system_.LeaveGuild(ecs_registry_, *player_entity);
  const auto error_code =
      success ? mir2::common::ErrorCode::kOk : mir2::common::ErrorCode::kInvalidAction;

  co_await SendLeaveGuildResponse(
      std::move(ctx), request_msg_id, success, static_cast<int>(error_code));

  if (success) {
    co_await SendGuildInfoSync(client_id, mir2::ecs::kInvalidGuildId);
    SYSLOG_INFO("GuildHandler left guild client_id={}", client_id);
  } else {
    SYSLOG_WARN("GuildHandler leave guild failed client_id={}", client_id);
  }
}

Task<void> GuildHandler::HandleKickGuild(HandlerContext ctx,
                                         const uint8_t* payload,
                                         size_t payload_size) {
  const uint16_t request_msg_id = ctx.msg_id;
  const uint64_t client_id = ctx.client_id;
  if (!payload || payload_size == 0) {
    SYSLOG_WARN("GuildHandler kick guild empty payload (client_id={})", ctx.client_id);
    co_await SendKickGuildResponse(std::move(ctx), request_msg_id,
                                   false,
                                   static_cast<int>(mir2::common::ErrorCode::kInvalidAction));
    co_return;
  }

  flatbuffers::Verifier verifier(payload, payload_size);
  if (!verifier.VerifyBuffer<mir2::proto::KickGuildRequest>(nullptr)) {
    SYSLOG_WARN("GuildHandler kick guild invalid payload (client_id={})", ctx.client_id);
    co_await SendKickGuildResponse(std::move(ctx), request_msg_id,
                                   false,
                                   static_cast<int>(mir2::common::ErrorCode::kInvalidAction));
    co_return;
  }

  const auto* req = flatbuffers::GetRoot<mir2::proto::KickGuildRequest>(payload);
  const mir2::ecs::CharacterId target_character_id =
      req ? req->target_character_id() : mir2::ecs::kInvalidCharacterId;
  if (target_character_id == mir2::ecs::kInvalidCharacterId) {
    co_await SendKickGuildResponse(std::move(ctx), request_msg_id,
                                   false,
                                   static_cast<int>(mir2::common::ErrorCode::kInvalidAction));
    co_return;
  }

  auto operator_entity = player_presence_service_.FindEntity(ctx.client_id);
  if (!operator_entity.has_value()) {
    SYSLOG_WARN("GuildHandler kick guild missing operator (client_id={})", ctx.client_id);
    co_await SendKickGuildResponse(std::move(ctx), request_msg_id,
                                   false,
                                   static_cast<int>(mir2::common::ErrorCode::kInvalidAction));
    co_return;
  }

  auto target_entity = FindEntityByCharacterId(ecs_registry_, target_character_id);
  if (!target_entity.has_value()) {
    co_await SendKickGuildResponse(std::move(ctx), request_msg_id,
                                   false,
                                   static_cast<int>(mir2::common::ErrorCode::kTargetNotFound));
    co_return;
  }

  const bool success =
      guild_system_.KickMember(ecs_registry_, *operator_entity, *target_entity);
  const auto error_code =
      success ? mir2::common::ErrorCode::kOk : mir2::common::ErrorCode::kInvalidAction;

  co_await SendKickGuildResponse(
      std::move(ctx), request_msg_id, success, static_cast<int>(error_code));

  if (success) {
    const auto guild_id = GetGuildIdByPlayer(*operator_entity)
                              .value_or(mir2::ecs::kInvalidGuildId);
    co_await SendGuildInfoSync(client_id, guild_id);
  }
}

Task<void> GuildHandler::HandleDeclareWar(HandlerContext ctx,
                                          const uint8_t* payload,
                                          size_t payload_size) {
  const uint16_t request_msg_id = ctx.msg_id;
  const uint64_t client_id = ctx.client_id;
  if (!payload || payload_size == 0) {
    SYSLOG_WARN("GuildHandler declare war empty payload (client_id={})", ctx.client_id);
    co_await SendDeclareWarResponse(std::move(ctx), request_msg_id,
                                    false,
                                    static_cast<int>(mir2::common::ErrorCode::kInvalidAction));
    co_return;
  }

  flatbuffers::Verifier verifier(payload, payload_size);
  if (!verifier.VerifyBuffer<mir2::proto::DeclareWarRequest>(nullptr)) {
    SYSLOG_WARN("GuildHandler declare war invalid payload (client_id={})", ctx.client_id);
    co_await SendDeclareWarResponse(std::move(ctx), request_msg_id,
                                    false,
                                    static_cast<int>(mir2::common::ErrorCode::kInvalidAction));
    co_return;
  }

  const auto* req = flatbuffers::GetRoot<mir2::proto::DeclareWarRequest>(payload);
  const mir2::ecs::GuildId target_guild_id =
      req ? req->target_guild_id() : mir2::ecs::kInvalidGuildId;
  if (target_guild_id == mir2::ecs::kInvalidGuildId) {
    SYSLOG_WARN("GuildHandler declare war invalid target guild (client_id={})", ctx.client_id);
    co_await SendDeclareWarResponse(std::move(ctx), request_msg_id,
                                    false,
                                    static_cast<int>(mir2::common::ErrorCode::kInvalidAction));
    co_return;
  }

  auto player_entity = player_presence_service_.FindEntity(ctx.client_id);
  if (!player_entity.has_value()) {
    SYSLOG_WARN("GuildHandler declare war missing player (client_id={})", ctx.client_id);
    co_await SendDeclareWarResponse(std::move(ctx), request_msg_id,
                                    false,
                                    static_cast<int>(mir2::common::ErrorCode::kInvalidAction));
    co_return;
  }

  const int result = guild_system_.DeclareWar(ecs_registry_, *player_entity, target_guild_id);
  const bool success = (result == 0);
  const auto error_code = MapGuildErrorCode(result);

  co_await SendDeclareWarResponse(
      std::move(ctx), request_msg_id, success, static_cast<int>(error_code));

  if (success) {
    const auto guild_id = GetGuildIdByPlayer(*player_entity)
                              .value_or(mir2::ecs::kInvalidGuildId);
    co_await SendGuildInfoSync(client_id, guild_id);
    SYSLOG_INFO("GuildHandler declared war client_id={} target_guild_id={}",
                client_id, target_guild_id);
  } else {
    SYSLOG_WARN("GuildHandler declare war failed client_id={} target_guild_id={} error={}",
                client_id, target_guild_id, result);
  }
}

Task<void> GuildHandler::HandleCancelWar(HandlerContext ctx,
                                         const uint8_t* payload,
                                         size_t payload_size) {
  const uint16_t request_msg_id = ctx.msg_id;
  const uint64_t client_id = ctx.client_id;
  if (!payload || payload_size == 0) {
    SYSLOG_WARN("GuildHandler cancel war empty payload (client_id={})", ctx.client_id);
    co_await SendCancelWarResponse(std::move(ctx), request_msg_id,
                                   false,
                                   static_cast<int>(mir2::common::ErrorCode::kInvalidAction));
    co_return;
  }

  flatbuffers::Verifier verifier(payload, payload_size);
  if (!verifier.VerifyBuffer<mir2::proto::CancelWarRequest>(nullptr)) {
    SYSLOG_WARN("GuildHandler cancel war invalid payload (client_id={})", ctx.client_id);
    co_await SendCancelWarResponse(std::move(ctx), request_msg_id,
                                   false,
                                   static_cast<int>(mir2::common::ErrorCode::kInvalidAction));
    co_return;
  }

  const auto* req = flatbuffers::GetRoot<mir2::proto::CancelWarRequest>(payload);
  const mir2::ecs::GuildId target_guild_id =
      req ? req->target_guild_id() : mir2::ecs::kInvalidGuildId;
  if (target_guild_id == mir2::ecs::kInvalidGuildId) {
    SYSLOG_WARN("GuildHandler cancel war invalid target guild (client_id={})", ctx.client_id);
    co_await SendCancelWarResponse(std::move(ctx), request_msg_id,
                                   false,
                                   static_cast<int>(mir2::common::ErrorCode::kInvalidAction));
    co_return;
  }

  auto player_entity = player_presence_service_.FindEntity(ctx.client_id);
  if (!player_entity.has_value()) {
    SYSLOG_WARN("GuildHandler cancel war missing player (client_id={})", ctx.client_id);
    co_await SendCancelWarResponse(std::move(ctx), request_msg_id,
                                   false,
                                   static_cast<int>(mir2::common::ErrorCode::kInvalidAction));
    co_return;
  }

  const bool success = guild_system_.CancelWar(ecs_registry_, *player_entity, target_guild_id);
  const auto error_code =
      success ? mir2::common::ErrorCode::kOk : mir2::common::ErrorCode::kInvalidAction;

  co_await SendCancelWarResponse(
      std::move(ctx), request_msg_id, success, static_cast<int>(error_code));

  if (success) {
    const auto guild_id = GetGuildIdByPlayer(*player_entity)
                              .value_or(mir2::ecs::kInvalidGuildId);
    co_await SendGuildInfoSync(client_id, guild_id);
    SYSLOG_INFO("GuildHandler cancelled war client_id={} target_guild_id={}",
                client_id, target_guild_id);
  } else {
    SYSLOG_WARN("GuildHandler cancel war failed client_id={} target_guild_id={}",
                client_id, target_guild_id);
  }
}

Task<void> GuildHandler::HandleMakeAlliance(HandlerContext ctx,
                                            const uint8_t* payload,
                                            size_t payload_size) {
  const uint16_t request_msg_id = ctx.msg_id;
  const uint64_t client_id = ctx.client_id;
  if (!payload || payload_size == 0) {
    SYSLOG_WARN("GuildHandler make alliance empty payload (client_id={})", ctx.client_id);
    co_await SendMakeAllianceResponse(std::move(ctx), request_msg_id,
                                      false,
                                      static_cast<int>(mir2::common::ErrorCode::kInvalidAction));
    co_return;
  }

  flatbuffers::Verifier verifier(payload, payload_size);
  if (!verifier.VerifyBuffer<mir2::proto::MakeAllianceRequest>(nullptr)) {
    SYSLOG_WARN("GuildHandler make alliance invalid payload (client_id={})", ctx.client_id);
    co_await SendMakeAllianceResponse(std::move(ctx), request_msg_id,
                                      false,
                                      static_cast<int>(mir2::common::ErrorCode::kInvalidAction));
    co_return;
  }

  const auto* req = flatbuffers::GetRoot<mir2::proto::MakeAllianceRequest>(payload);
  const mir2::ecs::GuildId target_guild_id =
      req ? req->target_guild_id() : mir2::ecs::kInvalidGuildId;
  if (target_guild_id == mir2::ecs::kInvalidGuildId) {
    co_await SendMakeAllianceResponse(
        std::move(ctx), request_msg_id,
        false,
        static_cast<int>(mir2::common::ErrorCode::kInvalidAction));
    co_return;
  }

  auto player_entity = player_presence_service_.FindEntity(ctx.client_id);
  if (!player_entity.has_value()) {
    co_await SendMakeAllianceResponse(
        std::move(ctx), request_msg_id,
        false,
        static_cast<int>(mir2::common::ErrorCode::kInvalidAction));
    co_return;
  }

  const int result = guild_system_.MakeAlliance(ecs_registry_, *player_entity, target_guild_id);
  const bool success = (result == 0);
  const auto error_code = MapAllianceErrorCode(result);

  co_await SendMakeAllianceResponse(
      std::move(ctx), request_msg_id, success, static_cast<int>(error_code));

  if (success) {
    const auto guild_id = GetGuildIdByPlayer(*player_entity)
                              .value_or(mir2::ecs::kInvalidGuildId);
    co_await SendGuildInfoSync(client_id, guild_id);
  }
}

Task<void> GuildHandler::HandleBreakAlliance(HandlerContext ctx,
                                             const uint8_t* payload,
                                             size_t payload_size) {
  const uint16_t request_msg_id = ctx.msg_id;
  const uint64_t client_id = ctx.client_id;
  if (!payload || payload_size == 0) {
    SYSLOG_WARN("GuildHandler break alliance empty payload (client_id={})", ctx.client_id);
    co_await SendBreakAllianceResponse(std::move(ctx), request_msg_id,
                                       false,
                                       static_cast<int>(mir2::common::ErrorCode::kInvalidAction));
    co_return;
  }

  flatbuffers::Verifier verifier(payload, payload_size);
  if (!verifier.VerifyBuffer<mir2::proto::BreakAllianceRequest>(nullptr)) {
    SYSLOG_WARN("GuildHandler break alliance invalid payload (client_id={})", ctx.client_id);
    co_await SendBreakAllianceResponse(std::move(ctx), request_msg_id,
                                       false,
                                       static_cast<int>(mir2::common::ErrorCode::kInvalidAction));
    co_return;
  }

  const auto* req = flatbuffers::GetRoot<mir2::proto::BreakAllianceRequest>(payload);
  const mir2::ecs::GuildId target_guild_id =
      req ? req->target_guild_id() : mir2::ecs::kInvalidGuildId;
  if (target_guild_id == mir2::ecs::kInvalidGuildId) {
    co_await SendBreakAllianceResponse(
        std::move(ctx), request_msg_id,
        false,
        static_cast<int>(mir2::common::ErrorCode::kInvalidAction));
    co_return;
  }

  auto player_entity = player_presence_service_.FindEntity(ctx.client_id);
  if (!player_entity.has_value()) {
    co_await SendBreakAllianceResponse(
        std::move(ctx), request_msg_id,
        false,
        static_cast<int>(mir2::common::ErrorCode::kInvalidAction));
    co_return;
  }

  const bool success =
      guild_system_.BreakAlliance(ecs_registry_, *player_entity, target_guild_id);
  const auto error_code =
      success ? mir2::common::ErrorCode::kOk : mir2::common::ErrorCode::kInvalidAction;

  co_await SendBreakAllianceResponse(
      std::move(ctx), request_msg_id, success, static_cast<int>(error_code));

  if (success) {
    const auto guild_id = GetGuildIdByPlayer(*player_entity)
                              .value_or(mir2::ecs::kInvalidGuildId);
    co_await SendGuildInfoSync(client_id, guild_id);
  }
}

Task<void> GuildHandler::HandleUpdateNotice(HandlerContext ctx,
                                            const uint8_t* payload,
                                            size_t payload_size) {
  const uint16_t request_msg_id = ctx.msg_id;
  const uint64_t client_id = ctx.client_id;
  if (!payload || payload_size == 0) {
    SYSLOG_WARN("GuildHandler update notice empty payload (client_id={})", ctx.client_id);
    co_await SendUpdateNoticeResponse(std::move(ctx), request_msg_id,
                                      false,
                                      static_cast<int>(mir2::common::ErrorCode::kInvalidAction));
    co_return;
  }

  flatbuffers::Verifier verifier(payload, payload_size);
  if (!verifier.VerifyBuffer<mir2::proto::UpdateNoticeRequest>(nullptr)) {
    SYSLOG_WARN("GuildHandler update notice invalid payload (client_id={})", ctx.client_id);
    co_await SendUpdateNoticeResponse(std::move(ctx), request_msg_id,
                                      false,
                                      static_cast<int>(mir2::common::ErrorCode::kInvalidAction));
    co_return;
  }

  const auto* req = flatbuffers::GetRoot<mir2::proto::UpdateNoticeRequest>(payload);
  std::vector<std::string> notice_lines;
  if (req && req->notice_lines()) {
    notice_lines.reserve(req->notice_lines()->size());
    for (const auto* line : *req->notice_lines()) {
      notice_lines.push_back(line ? line->str() : std::string{});
    }
  }

  auto player_entity = player_presence_service_.FindEntity(ctx.client_id);
  if (!player_entity.has_value()) {
    co_await SendUpdateNoticeResponse(
        std::move(ctx), request_msg_id,
        false,
        static_cast<int>(mir2::common::ErrorCode::kInvalidAction));
    co_return;
  }

  const bool success =
      guild_system_.UpdateNotice(ecs_registry_, *player_entity, notice_lines);
  const auto error_code =
      success ? mir2::common::ErrorCode::kOk : mir2::common::ErrorCode::kInvalidAction;

  co_await SendUpdateNoticeResponse(
      std::move(ctx), request_msg_id, success, static_cast<int>(error_code));

  if (success) {
    const auto guild_id = GetGuildIdByPlayer(*player_entity)
                              .value_or(mir2::ecs::kInvalidGuildId);
    co_await SendGuildInfoSync(client_id, guild_id);
  }
}

Task<void> GuildHandler::HandleUpdateRank(HandlerContext ctx,
                                          const uint8_t* payload,
                                          size_t payload_size) {
  const uint16_t request_msg_id = ctx.msg_id;
  const uint64_t client_id = ctx.client_id;
  if (!payload || payload_size == 0) {
    SYSLOG_WARN("GuildHandler update rank empty payload (client_id={})", ctx.client_id);
    co_await SendUpdateRankResponse(std::move(ctx), request_msg_id,
                                    false,
                                    static_cast<int>(mir2::common::ErrorCode::kInvalidAction));
    co_return;
  }

  flatbuffers::Verifier verifier(payload, payload_size);
  if (!verifier.VerifyBuffer<mir2::proto::UpdateRankRequest>(nullptr)) {
    SYSLOG_WARN("GuildHandler update rank invalid payload (client_id={})", ctx.client_id);
    co_await SendUpdateRankResponse(std::move(ctx), request_msg_id,
                                    false,
                                    static_cast<int>(mir2::common::ErrorCode::kInvalidAction));
    co_return;
  }

  const auto* req = flatbuffers::GetRoot<mir2::proto::UpdateRankRequest>(payload);
  if (!req || !req->members() || req->members()->empty()) {
    co_await SendUpdateRankResponse(std::move(ctx), request_msg_id,
                                    false,
                                    static_cast<int>(mir2::common::ErrorCode::kInvalidAction));
    co_return;
  }

  auto operator_entity = player_presence_service_.FindEntity(ctx.client_id);
  if (!operator_entity.has_value()) {
    co_await SendUpdateRankResponse(std::move(ctx), request_msg_id,
                                    false,
                                    static_cast<int>(mir2::common::ErrorCode::kInvalidAction));
    co_return;
  }

  bool success = true;
  for (const auto* member : *req->members()) {
    if (!member || member->character_id() == mir2::ecs::kInvalidCharacterId) {
      success = false;
      break;
    }
    if (!guild_system_.SetMemberRank(ecs_registry_,
                                     *operator_entity,
                                     member->character_id(),
                                     member->rank())) {
      success = false;
      break;
    }
  }

  const auto error_code =
      success ? mir2::common::ErrorCode::kOk : mir2::common::ErrorCode::kInvalidAction;
  co_await SendUpdateRankResponse(
      std::move(ctx), request_msg_id, success, static_cast<int>(error_code));

  if (success) {
    const auto guild_id = GetGuildIdByPlayer(*operator_entity)
                              .value_or(mir2::ecs::kInvalidGuildId);
    co_await SendGuildInfoSync(client_id, guild_id);
  }
}

Task<void> GuildHandler::SendCreateGuildResponse(HandlerContext ctx,
                                                 uint16_t request_msg_id,
                                                 bool success,
                                                 int error_code,
                                                 mir2::ecs::GuildId guild_id) {
  auto payload = BuildCreateGuildRsp(success, error_code, guild_id);
  co_await response_sender_.SendAsync(
      ctx.client_id, ResolveGuildResponseMsgId(request_msg_id), std::move(payload));
}

Task<void> GuildHandler::SendJoinGuildResponse(HandlerContext ctx,
                                               uint16_t request_msg_id,
                                               bool success,
                                               int error_code) {
  auto payload = BuildJoinGuildRsp(success, error_code);
  co_await response_sender_.SendAsync(
      ctx.client_id, ResolveGuildResponseMsgId(request_msg_id), std::move(payload));
}

Task<void> GuildHandler::SendLeaveGuildResponse(HandlerContext ctx,
                                                uint16_t request_msg_id,
                                                bool success,
                                                int error_code) {
  auto payload = BuildLeaveGuildRsp(success, error_code);
  co_await response_sender_.SendAsync(
      ctx.client_id, ResolveGuildResponseMsgId(request_msg_id), std::move(payload));
}

Task<void> GuildHandler::SendKickGuildResponse(HandlerContext ctx,
                                               uint16_t request_msg_id,
                                               bool success,
                                               int error_code) {
  auto payload = BuildKickGuildRsp(success, error_code);
  co_await response_sender_.SendAsync(
      ctx.client_id, ResolveGuildResponseMsgId(request_msg_id), std::move(payload));
}

Task<void> GuildHandler::SendDeclareWarResponse(HandlerContext ctx,
                                                uint16_t request_msg_id,
                                                bool success,
                                                int error_code) {
  auto payload = BuildDeclareWarRsp(success, error_code);
  co_await response_sender_.SendAsync(
      ctx.client_id, ResolveGuildResponseMsgId(request_msg_id), std::move(payload));
}

Task<void> GuildHandler::SendCancelWarResponse(HandlerContext ctx,
                                               uint16_t request_msg_id,
                                               bool success,
                                               int error_code) {
  auto payload = BuildCancelWarRsp(success, error_code);
  co_await response_sender_.SendAsync(
      ctx.client_id, ResolveGuildResponseMsgId(request_msg_id), std::move(payload));
}

Task<void> GuildHandler::SendMakeAllianceResponse(HandlerContext ctx,
                                                  uint16_t request_msg_id,
                                                  bool success,
                                                  int error_code) {
  auto payload = BuildMakeAllianceRsp(success, error_code);
  co_await response_sender_.SendAsync(
      ctx.client_id, ResolveGuildResponseMsgId(request_msg_id), std::move(payload));
}

Task<void> GuildHandler::SendBreakAllianceResponse(HandlerContext ctx,
                                                   uint16_t request_msg_id,
                                                   bool success,
                                                   int error_code) {
  auto payload = BuildBreakAllianceRsp(success, error_code);
  co_await response_sender_.SendAsync(
      ctx.client_id, ResolveGuildResponseMsgId(request_msg_id), std::move(payload));
}

Task<void> GuildHandler::SendUpdateNoticeResponse(HandlerContext ctx,
                                                  uint16_t request_msg_id,
                                                  bool success,
                                                  int error_code) {
  auto payload = BuildUpdateNoticeRsp(success, error_code);
  co_await response_sender_.SendAsync(
      ctx.client_id, ResolveGuildResponseMsgId(request_msg_id), std::move(payload));
}

Task<void> GuildHandler::SendUpdateRankResponse(HandlerContext ctx,
                                                uint16_t request_msg_id,
                                                bool success,
                                                int error_code) {
  auto payload = BuildUpdateRankRsp(success, error_code);
  co_await response_sender_.SendAsync(
      ctx.client_id, ResolveGuildResponseMsgId(request_msg_id), std::move(payload));
}

Task<void> GuildHandler::SendGuildInfoSync(uint64_t client_id,
                                           mir2::ecs::GuildId guild_id) {
  if (client_id == 0) {
    co_return;
  }

  const mir2::ecs::GuildComponent* guild =
      FindGuildById(ecs_registry_, guild_id);
  auto payload = BuildGuildInfoSyncPayload(ecs_registry_, guild);
  co_await response_sender_.SendAsync(
      client_id,
      static_cast<uint16_t>(mir2::common::MsgId::kGuildInfoSync),
      std::move(payload));
}

std::optional<mir2::ecs::GuildId> GuildHandler::GetGuildIdByPlayer(
    entt::entity player) const {
  if (player == entt::null || !ecs_registry_.valid(player)) {
    return std::nullopt;
  }
  const auto* member = ecs_registry_.try_get<mir2::ecs::GuildMemberComponent>(player);
  if (!member || member->guild_id == mir2::ecs::kInvalidGuildId) {
    return std::nullopt;
  }
  return member->guild_id;
}

}  // namespace mir2::logic
