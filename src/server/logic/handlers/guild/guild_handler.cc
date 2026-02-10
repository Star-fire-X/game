#include "logic/handlers/guild/guild_handler.h"

#include <utility>
#include <vector>

#include <flatbuffers/flatbuffers.h>

#include "common/enums.h"
#include "ecs/systems/guild_system.h"
#include "guild_generated.h"
#include "log/logger.h"
#include "logic/response_sender.h"
#include "logic/services/player_presence_service.h"

namespace mir2::logic {

namespace {

std::vector<uint8_t> BuildCreateGuildRsp(bool success, int error_code) {
  flatbuffers::FlatBufferBuilder builder;
  const auto rsp = mir2::proto::CreateCreateGuildResponse(
      builder, success, error_code, 0);
  builder.Finish(rsp);
  const uint8_t* data = builder.GetBufferPointer();
  return std::vector<uint8_t>(data, data + builder.GetSize());
}

}  // namespace

GuildHandler::GuildHandler(CoroutineExecutor& executor,
                           ResponseSender& response_sender,
                           ClientRegistry& registry,
                           PlayerPresenceService& player_presence_service,
                           mir2::ecs::GuildSystem& guild_system,
                           entt::registry& ecs_registry)
    : executor_(executor),
      response_sender_(response_sender),
      client_registry_(registry),
      player_presence_service_(player_presence_service),
      guild_system_(guild_system),
      ecs_registry_(ecs_registry) {}

Task<void> GuildHandler::HandleMessage(HandlerContext ctx,
                                       uint16_t msg_id,
                                       const uint8_t* payload,
                                       size_t payload_size) {
  switch (msg_id) {
    case static_cast<uint16_t>(mir2::proto::GuildMessageType::CREATE):
      co_await HandleCreateGuild(std::move(ctx), payload, payload_size);
      break;
    case static_cast<uint16_t>(mir2::proto::GuildMessageType::JOIN):
      co_await HandleJoinGuild(std::move(ctx), payload, payload_size);
      break;
    case static_cast<uint16_t>(mir2::proto::GuildMessageType::LEAVE):
      co_await HandleLeaveGuild(std::move(ctx), payload, payload_size);
      break;
    case static_cast<uint16_t>(mir2::proto::GuildMessageType::DECLARE_WAR):
      co_await HandleDeclareWar(std::move(ctx), payload, payload_size);
      break;
    case static_cast<uint16_t>(mir2::proto::GuildMessageType::CANCEL_WAR):
      co_await HandleCancelWar(std::move(ctx), payload, payload_size);
      break;
    default:
      SYSLOG_WARN("GuildHandler unknown msg_id={} (client_id={})",
                  msg_id, ctx.client_id);
      break;
  }
}

Task<void> GuildHandler::HandleCreateGuild(HandlerContext ctx,
                                           const uint8_t* payload,
                                           size_t payload_size) {
  if (!payload || payload_size == 0) {
    SYSLOG_WARN("GuildHandler create guild empty payload (client_id={})", ctx.client_id);
    co_await SendCreateGuildResponse(std::move(ctx), false,
                                     static_cast<int>(mir2::common::ErrorCode::kInvalidAction));
    co_return;
  }

  flatbuffers::Verifier verifier(payload, payload_size);
  if (!verifier.VerifyBuffer<mir2::proto::CreateGuildRequest>(nullptr)) {
    SYSLOG_WARN("GuildHandler create guild invalid payload (client_id={})", ctx.client_id);
    co_await SendCreateGuildResponse(std::move(ctx), false,
                                     static_cast<int>(mir2::common::ErrorCode::kInvalidAction));
    co_return;
  }

  const auto* req = flatbuffers::GetRoot<mir2::proto::CreateGuildRequest>(payload);
  if (!req || !req->guild_name() || req->guild_name()->str().empty()) {
    SYSLOG_WARN("GuildHandler create guild missing name (client_id={})", ctx.client_id);
    co_await SendCreateGuildResponse(std::move(ctx), false,
                                     static_cast<int>(mir2::common::ErrorCode::kInvalidAction));
    co_return;
  }

  auto player_entity = player_presence_service_.FindEntity(ctx.client_id);
  if (!player_entity.has_value()) {
    SYSLOG_WARN("GuildHandler create guild missing player (client_id={})", ctx.client_id);
    co_await SendCreateGuildResponse(std::move(ctx), false,
                                     static_cast<int>(mir2::common::ErrorCode::kInvalidAction));
    co_return;
  }

  const int result = guild_system_.CreateGuild(ecs_registry_,
                                               *player_entity,
                                               req->guild_name()->str());
  const bool success = (result == 0);
  co_await SendCreateGuildResponse(std::move(ctx), success, result);

  SYSLOG_DEBUG("GuildHandler create guild client_id={} result={}",
               ctx.client_id, result);
}

Task<void> GuildHandler::HandleJoinGuild(HandlerContext ctx,
                                         const uint8_t* payload,
                                         size_t payload_size) {
  if (!payload || payload_size == 0) {
    SYSLOG_WARN("GuildHandler join guild empty payload (client_id={})", ctx.client_id);
    co_return;
  }

  flatbuffers::Verifier verifier(payload, payload_size);
  if (!verifier.VerifyBuffer<mir2::proto::JoinGuildRequest>(nullptr)) {
    SYSLOG_WARN("GuildHandler join guild invalid payload (client_id={})", ctx.client_id);
    co_return;
  }

  const auto* req = flatbuffers::GetRoot<mir2::proto::JoinGuildRequest>(payload);
  const uint32_t guild_id = req ? req->guild_id() : 0;
  if (guild_id == 0) {
    SYSLOG_WARN("GuildHandler join guild invalid guild_id (client_id={})", ctx.client_id);
    co_return;
  }

  auto player_entity = player_presence_service_.FindEntity(ctx.client_id);
  if (!player_entity.has_value()) {
    SYSLOG_WARN("GuildHandler join guild missing player (client_id={})", ctx.client_id);
    co_return;
  }

  (void)guild_system_.JoinGuild(ecs_registry_, *player_entity, guild_id);

  SYSLOG_DEBUG("GuildHandler join guild client_id={} guild_id={}",
               ctx.client_id, guild_id);
}

Task<void> GuildHandler::HandleLeaveGuild(HandlerContext ctx,
                                          const uint8_t* payload,
                                          size_t payload_size) {
  if (!payload || payload_size == 0) {
    SYSLOG_WARN("GuildHandler leave guild empty payload (client_id={})", ctx.client_id);
    co_return;
  }

  flatbuffers::Verifier verifier(payload, payload_size);
  if (!verifier.VerifyBuffer<mir2::proto::LeaveGuildRequest>(nullptr)) {
    SYSLOG_WARN("GuildHandler leave guild invalid payload (client_id={})", ctx.client_id);
    co_return;
  }

  auto player_entity = player_presence_service_.FindEntity(ctx.client_id);
  if (!player_entity.has_value()) {
    SYSLOG_WARN("GuildHandler leave guild missing player (client_id={})", ctx.client_id);
    co_return;
  }

  (void)guild_system_.LeaveGuild(ecs_registry_, *player_entity);

  SYSLOG_DEBUG("GuildHandler leave guild client_id={}", ctx.client_id);
}

Task<void> GuildHandler::HandleDeclareWar(HandlerContext ctx,
                                          const uint8_t* payload,
                                          size_t payload_size) {
  if (!payload || payload_size == 0) {
    SYSLOG_WARN("GuildHandler declare war empty payload (client_id={})", ctx.client_id);
    co_return;
  }

  flatbuffers::Verifier verifier(payload, payload_size);
  if (!verifier.VerifyBuffer<mir2::proto::DeclareWarRequest>(nullptr)) {
    SYSLOG_WARN("GuildHandler declare war invalid payload (client_id={})", ctx.client_id);
    co_return;
  }

  const auto* req = flatbuffers::GetRoot<mir2::proto::DeclareWarRequest>(payload);
  const uint32_t target_guild_id = req ? req->target_guild_id() : 0;
  if (target_guild_id == 0) {
    SYSLOG_WARN("GuildHandler declare war invalid target guild (client_id={})", ctx.client_id);
    co_return;
  }

  auto player_entity = player_presence_service_.FindEntity(ctx.client_id);
  if (!player_entity.has_value()) {
    SYSLOG_WARN("GuildHandler declare war missing player (client_id={})", ctx.client_id);
    co_return;
  }

  (void)guild_system_.DeclareWar(ecs_registry_, *player_entity, target_guild_id);

  SYSLOG_DEBUG("GuildHandler declare war client_id={} target_guild_id={}",
               ctx.client_id, target_guild_id);
}

Task<void> GuildHandler::HandleCancelWar(HandlerContext ctx,
                                         const uint8_t* payload,
                                         size_t payload_size) {
  if (!payload || payload_size == 0) {
    SYSLOG_WARN("GuildHandler cancel war empty payload (client_id={})", ctx.client_id);
    co_return;
  }

  flatbuffers::Verifier verifier(payload, payload_size);
  if (!verifier.VerifyBuffer<mir2::proto::DeclareWarRequest>(nullptr)) {
    SYSLOG_WARN("GuildHandler cancel war invalid payload (client_id={})", ctx.client_id);
    co_return;
  }

  const auto* req = flatbuffers::GetRoot<mir2::proto::DeclareWarRequest>(payload);
  const uint32_t target_guild_id = req ? req->target_guild_id() : 0;
  if (target_guild_id == 0) {
    SYSLOG_WARN("GuildHandler cancel war invalid target guild (client_id={})", ctx.client_id);
    co_return;
  }

  auto player_entity = player_presence_service_.FindEntity(ctx.client_id);
  if (!player_entity.has_value()) {
    SYSLOG_WARN("GuildHandler cancel war missing player (client_id={})", ctx.client_id);
    co_return;
  }

  (void)guild_system_.CancelWar(ecs_registry_, *player_entity, target_guild_id);

  SYSLOG_DEBUG("GuildHandler cancel war client_id={} target_guild_id={}",
               ctx.client_id, target_guild_id);
}

Task<void> GuildHandler::SendCreateGuildResponse(HandlerContext ctx,
                                                 bool success,
                                                 int error_code) {
  auto payload = BuildCreateGuildRsp(success, error_code);
  co_await response_sender_.SendAsync(
      ctx.client_id,
      static_cast<uint16_t>(mir2::proto::GuildMessageType::CREATE),
      std::move(payload));
}

}  // namespace mir2::logic
