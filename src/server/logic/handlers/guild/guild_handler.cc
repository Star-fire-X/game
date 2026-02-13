#include "logic/handlers/guild/guild_handler.h"

#include <utility>
#include <vector>

#include <flatbuffers/flatbuffers.h>

#include "ecs/components/guild_component.h"
#include "ecs/systems/guild_system.h"
#include "guild_generated.h"
#include "log/logger.h"
#include "logic/response_sender.h"
#include "logic/services/player_presence_service.h"

namespace mir2::logic {

namespace {

// Map GuildSystem error codes to protocol error codes
mir2::common::ErrorCode MapGuildErrorCode(int system_error_code) {
  switch (system_error_code) {
    case 0:
      return mir2::common::ErrorCode::kOk;
    case -1:
      return mir2::common::ErrorCode::kInvalidAction;  // Already in guild or invalid player
    case -2:
      return mir2::common::ErrorCode::kInvalidAction;  // Insufficient gold
    case -3:
      return mir2::common::ErrorCode::kInvalidAction;  // Missing item
    case -4:
      return mir2::common::ErrorCode::kNameExists;     // Duplicate name
    default:
      return mir2::common::ErrorCode::kUnknown;
  }
}

std::vector<uint8_t> BuildCreateGuildRsp(bool success, int error_code, uint32_t guild_id) {
  flatbuffers::FlatBufferBuilder builder;
  flatbuffers::Offset<mir2::proto::GuildInfo> guild_info_offset = 0;
  if (success && guild_id != 0) {
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
  // Unused parameters: executor, registry
  (void)executor;
  (void)registry;
}

Task<void> GuildHandler::HandleMessage(HandlerContext ctx,
                                       const uint8_t* payload,
                                       size_t payload_size) {
  switch (ctx.msg_id) {
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
                  ctx.msg_id, ctx.client_id);
      break;
  }
}

Task<void> GuildHandler::HandleCreateGuild(HandlerContext ctx,
                                           const uint8_t* payload,
                                           size_t payload_size) {
  if (!payload || payload_size == 0) {
    SYSLOG_WARN("GuildHandler create guild empty payload (client_id={})", ctx.client_id);
    co_await SendCreateGuildResponse(std::move(ctx), false,
                                     static_cast<int>(mir2::common::ErrorCode::kInvalidAction), 0);
    co_return;
  }

  flatbuffers::Verifier verifier(payload, payload_size);
  if (!verifier.VerifyBuffer<mir2::proto::CreateGuildRequest>(nullptr)) {
    SYSLOG_WARN("GuildHandler create guild invalid payload (client_id={})", ctx.client_id);
    co_await SendCreateGuildResponse(std::move(ctx), false,
                                     static_cast<int>(mir2::common::ErrorCode::kInvalidAction), 0);
    co_return;
  }

  const auto* req = flatbuffers::GetRoot<mir2::proto::CreateGuildRequest>(payload);
  if (!req || !req->guild_name() || req->guild_name()->str().empty()) {
    SYSLOG_WARN("GuildHandler create guild missing name (client_id={})", ctx.client_id);
    co_await SendCreateGuildResponse(std::move(ctx), false,
                                     static_cast<int>(mir2::common::ErrorCode::kInvalidAction), 0);
    co_return;
  }

  auto player_entity = player_presence_service_.FindEntity(ctx.client_id);
  if (!player_entity.has_value()) {
    SYSLOG_WARN("GuildHandler create guild missing player (client_id={})", ctx.client_id);
    co_await SendCreateGuildResponse(std::move(ctx), false,
                                     static_cast<int>(mir2::common::ErrorCode::kInvalidAction), 0);
    co_return;
  }

  const int result = guild_system_.CreateGuild(ecs_registry_,
                                               *player_entity,
                                               req->guild_name()->str());
  const bool success = (result == 0);
  const auto error_code = MapGuildErrorCode(result);

  // Get guild_id from GuildMemberComponent if successful
  uint32_t guild_id = 0;
  if (success) {
    if (auto* member = ecs_registry_.try_get<mir2::ecs::GuildMemberComponent>(*player_entity)) {
      guild_id = member->guild_id;
    }
  }

  co_await SendCreateGuildResponse(std::move(ctx), success, static_cast<int>(error_code), guild_id);

  if (success) {
    SYSLOG_INFO("GuildHandler created guild client_id={} guild_id={} name={}",
                ctx.client_id, guild_id, req->guild_name()->str());
  } else {
    SYSLOG_WARN("GuildHandler create guild failed client_id={} error={}",
                ctx.client_id, result);
  }
}

Task<void> GuildHandler::HandleJoinGuild(HandlerContext ctx,
                                         const uint8_t* payload,
                                         size_t payload_size) {
  if (!payload || payload_size == 0) {
    SYSLOG_WARN("GuildHandler join guild empty payload (client_id={})", ctx.client_id);
    co_await SendJoinGuildResponse(std::move(ctx), false,
                                   static_cast<int>(mir2::common::ErrorCode::kInvalidAction));
    co_return;
  }

  flatbuffers::Verifier verifier(payload, payload_size);
  if (!verifier.VerifyBuffer<mir2::proto::JoinGuildRequest>(nullptr)) {
    SYSLOG_WARN("GuildHandler join guild invalid payload (client_id={})", ctx.client_id);
    co_await SendJoinGuildResponse(std::move(ctx), false,
                                   static_cast<int>(mir2::common::ErrorCode::kInvalidAction));
    co_return;
  }

  const auto* req = flatbuffers::GetRoot<mir2::proto::JoinGuildRequest>(payload);
  const uint32_t guild_id = req ? req->guild_id() : 0;
  if (guild_id == 0) {
    SYSLOG_WARN("GuildHandler join guild invalid guild_id (client_id={})", ctx.client_id);
    co_await SendJoinGuildResponse(std::move(ctx), false,
                                   static_cast<int>(mir2::common::ErrorCode::kInvalidAction));
    co_return;
  }

  auto player_entity = player_presence_service_.FindEntity(ctx.client_id);
  if (!player_entity.has_value()) {
    SYSLOG_WARN("GuildHandler join guild missing player (client_id={})", ctx.client_id);
    co_await SendJoinGuildResponse(std::move(ctx), false,
                                   static_cast<int>(mir2::common::ErrorCode::kInvalidAction));
    co_return;
  }

  const bool success = guild_system_.JoinGuild(ecs_registry_, *player_entity, guild_id);
  const auto error_code = success ? mir2::common::ErrorCode::kOk : mir2::common::ErrorCode::kInvalidAction;

  co_await SendJoinGuildResponse(std::move(ctx), success, static_cast<int>(error_code));

  if (success) {
    SYSLOG_INFO("GuildHandler joined guild client_id={} guild_id={}",
                ctx.client_id, guild_id);
  } else {
    SYSLOG_WARN("GuildHandler join guild failed client_id={} guild_id={}",
                ctx.client_id, guild_id);
  }
}

Task<void> GuildHandler::HandleLeaveGuild(HandlerContext ctx,
                                          const uint8_t* payload,
                                          size_t payload_size) {
  if (!payload || payload_size == 0) {
    SYSLOG_WARN("GuildHandler leave guild empty payload (client_id={})", ctx.client_id);
    co_await SendLeaveGuildResponse(std::move(ctx), false,
                                    static_cast<int>(mir2::common::ErrorCode::kInvalidAction));
    co_return;
  }

  flatbuffers::Verifier verifier(payload, payload_size);
  if (!verifier.VerifyBuffer<mir2::proto::LeaveGuildRequest>(nullptr)) {
    SYSLOG_WARN("GuildHandler leave guild invalid payload (client_id={})", ctx.client_id);
    co_await SendLeaveGuildResponse(std::move(ctx), false,
                                    static_cast<int>(mir2::common::ErrorCode::kInvalidAction));
    co_return;
  }

  auto player_entity = player_presence_service_.FindEntity(ctx.client_id);
  if (!player_entity.has_value()) {
    SYSLOG_WARN("GuildHandler leave guild missing player (client_id={})", ctx.client_id);
    co_await SendLeaveGuildResponse(std::move(ctx), false,
                                    static_cast<int>(mir2::common::ErrorCode::kInvalidAction));
    co_return;
  }

  const bool success = guild_system_.LeaveGuild(ecs_registry_, *player_entity);
  const auto error_code = success ? mir2::common::ErrorCode::kOk : mir2::common::ErrorCode::kInvalidAction;

  co_await SendLeaveGuildResponse(std::move(ctx), success, static_cast<int>(error_code));

  if (success) {
    SYSLOG_INFO("GuildHandler left guild client_id={}", ctx.client_id);
  } else {
    SYSLOG_WARN("GuildHandler leave guild failed client_id={}", ctx.client_id);
  }
}

Task<void> GuildHandler::HandleDeclareWar(HandlerContext ctx,
                                          const uint8_t* payload,
                                          size_t payload_size) {
  if (!payload || payload_size == 0) {
    SYSLOG_WARN("GuildHandler declare war empty payload (client_id={})", ctx.client_id);
    co_await SendDeclareWarResponse(std::move(ctx), false,
                                    static_cast<int>(mir2::common::ErrorCode::kInvalidAction));
    co_return;
  }

  flatbuffers::Verifier verifier(payload, payload_size);
  if (!verifier.VerifyBuffer<mir2::proto::DeclareWarRequest>(nullptr)) {
    SYSLOG_WARN("GuildHandler declare war invalid payload (client_id={})", ctx.client_id);
    co_await SendDeclareWarResponse(std::move(ctx), false,
                                    static_cast<int>(mir2::common::ErrorCode::kInvalidAction));
    co_return;
  }

  const auto* req = flatbuffers::GetRoot<mir2::proto::DeclareWarRequest>(payload);
  const uint32_t target_guild_id = req ? req->target_guild_id() : 0;
  if (target_guild_id == 0) {
    SYSLOG_WARN("GuildHandler declare war invalid target guild (client_id={})", ctx.client_id);
    co_await SendDeclareWarResponse(std::move(ctx), false,
                                    static_cast<int>(mir2::common::ErrorCode::kInvalidAction));
    co_return;
  }

  auto player_entity = player_presence_service_.FindEntity(ctx.client_id);
  if (!player_entity.has_value()) {
    SYSLOG_WARN("GuildHandler declare war missing player (client_id={})", ctx.client_id);
    co_await SendDeclareWarResponse(std::move(ctx), false,
                                    static_cast<int>(mir2::common::ErrorCode::kInvalidAction));
    co_return;
  }

  const int result = guild_system_.DeclareWar(ecs_registry_, *player_entity, target_guild_id);
  const bool success = (result == 0);
  const auto error_code = MapGuildErrorCode(result);

  co_await SendDeclareWarResponse(std::move(ctx), success, static_cast<int>(error_code));

  if (success) {
    SYSLOG_INFO("GuildHandler declared war client_id={} target_guild_id={}",
                ctx.client_id, target_guild_id);
  } else {
    SYSLOG_WARN("GuildHandler declare war failed client_id={} target_guild_id={} error={}",
                ctx.client_id, target_guild_id, result);
  }
}

Task<void> GuildHandler::HandleCancelWar(HandlerContext ctx,
                                         const uint8_t* payload,
                                         size_t payload_size) {
  if (!payload || payload_size == 0) {
    SYSLOG_WARN("GuildHandler cancel war empty payload (client_id={})", ctx.client_id);
    co_await SendCancelWarResponse(std::move(ctx), false,
                                   static_cast<int>(mir2::common::ErrorCode::kInvalidAction));
    co_return;
  }

  flatbuffers::Verifier verifier(payload, payload_size);
  if (!verifier.VerifyBuffer<mir2::proto::CancelWarRequest>(nullptr)) {
    SYSLOG_WARN("GuildHandler cancel war invalid payload (client_id={})", ctx.client_id);
    co_await SendCancelWarResponse(std::move(ctx), false,
                                   static_cast<int>(mir2::common::ErrorCode::kInvalidAction));
    co_return;
  }

  const auto* req = flatbuffers::GetRoot<mir2::proto::CancelWarRequest>(payload);
  const uint32_t target_guild_id = req ? req->target_guild_id() : 0;
  if (target_guild_id == 0) {
    SYSLOG_WARN("GuildHandler cancel war invalid target guild (client_id={})", ctx.client_id);
    co_await SendCancelWarResponse(std::move(ctx), false,
                                   static_cast<int>(mir2::common::ErrorCode::kInvalidAction));
    co_return;
  }

  auto player_entity = player_presence_service_.FindEntity(ctx.client_id);
  if (!player_entity.has_value()) {
    SYSLOG_WARN("GuildHandler cancel war missing player (client_id={})", ctx.client_id);
    co_await SendCancelWarResponse(std::move(ctx), false,
                                   static_cast<int>(mir2::common::ErrorCode::kInvalidAction));
    co_return;
  }

  const bool success = guild_system_.CancelWar(ecs_registry_, *player_entity, target_guild_id);
  const auto error_code = success ? mir2::common::ErrorCode::kOk : mir2::common::ErrorCode::kInvalidAction;

  co_await SendCancelWarResponse(std::move(ctx), success, static_cast<int>(error_code));

  if (success) {
    SYSLOG_INFO("GuildHandler cancelled war client_id={} target_guild_id={}",
                ctx.client_id, target_guild_id);
  } else {
    SYSLOG_WARN("GuildHandler cancel war failed client_id={} target_guild_id={}",
                ctx.client_id, target_guild_id);
  }
}

Task<void> GuildHandler::SendCreateGuildResponse(HandlerContext ctx,
                                                 bool success,
                                                 int error_code,
                                                 uint32_t guild_id) {
  auto payload = BuildCreateGuildRsp(success, error_code, guild_id);
  co_await response_sender_.SendAsync(
      ctx.client_id,
      static_cast<uint16_t>(mir2::proto::GuildMessageType::CREATE),
      std::move(payload));
}

Task<void> GuildHandler::SendJoinGuildResponse(HandlerContext ctx,
                                               bool success,
                                               int error_code) {
  auto payload = BuildJoinGuildRsp(success, error_code);
  co_await response_sender_.SendAsync(
      ctx.client_id,
      static_cast<uint16_t>(mir2::proto::GuildMessageType::JOIN),
      std::move(payload));
}

Task<void> GuildHandler::SendLeaveGuildResponse(HandlerContext ctx,
                                                bool success,
                                                int error_code) {
  auto payload = BuildLeaveGuildRsp(success, error_code);
  co_await response_sender_.SendAsync(
      ctx.client_id,
      static_cast<uint16_t>(mir2::proto::GuildMessageType::LEAVE),
      std::move(payload));
}

Task<void> GuildHandler::SendDeclareWarResponse(HandlerContext ctx,
                                                bool success,
                                                int error_code) {
  auto payload = BuildDeclareWarRsp(success, error_code);
  co_await response_sender_.SendAsync(
      ctx.client_id,
      static_cast<uint16_t>(mir2::proto::GuildMessageType::DECLARE_WAR),
      std::move(payload));
}

Task<void> GuildHandler::SendCancelWarResponse(HandlerContext ctx,
                                               bool success,
                                               int error_code) {
  auto payload = BuildCancelWarRsp(success, error_code);
  co_await response_sender_.SendAsync(
      ctx.client_id,
      static_cast<uint16_t>(mir2::proto::GuildMessageType::CANCEL_WAR),
      std::move(payload));
}

}  // namespace mir2::logic
