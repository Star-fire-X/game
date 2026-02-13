#include "logic/handlers/chat/chat_handler.h"

#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <flatbuffers/flatbuffers.h>

#include "chat_generated.h"
#include "common/enums.h"
#include "ecs/components/character_components.h"
#include "game/chat/chat_service.h"
#include "game/map/aoi_manager.h"
#include "log/logger.h"
#include "logic/handlers/handler_error_utils.h"
#include "logic/response_sender.h"
#include "logic/services/player_presence_service.h"

namespace mir2::logic {

namespace {

constexpr size_t kMaxChatLength = 256;

/// Extract character ID from HandlerContext.
/// Returns nullopt if context is invalid or entity has no identity component.
std::optional<uint64_t> GetCharacterId(const HandlerContext& ctx,
                                       entt::registry& registry) {
  if (ctx.entity == entt::null || !registry.valid(ctx.entity)) {
    return std::nullopt;
  }
  const auto* identity = registry.try_get<ecs::CharacterIdentityComponent>(ctx.entity);
  if (!identity) {
    return std::nullopt;
  }
  return identity->id;
}

std::optional<entt::entity> FindOnlineCharacterEntity(entt::registry& registry,
                                                       uint64_t character_id) {
  auto view = registry.view<ecs::CharacterIdentityComponent, ecs::CharacterStateComponent>();
  for (auto entity : view) {
    const auto& identity = view.get<ecs::CharacterIdentityComponent>(entity);
    if (identity.id != character_id) {
      continue;
    }

    const auto& state = view.get<ecs::CharacterStateComponent>(entity);
    if (!state.is_online) {
      return std::nullopt;
    }
    return entity;
  }
  return std::nullopt;
}

std::optional<uint64_t> FindOnlineCharacterIdByName(entt::registry& registry,
                                                     const std::string& name) {
  auto view = registry.view<ecs::CharacterIdentityComponent, ecs::CharacterStateComponent>();
  for (auto entity : view) {
    const auto& identity = view.get<ecs::CharacterIdentityComponent>(entity);
    if (identity.name != name) {
      continue;
    }

    const auto& state = view.get<ecs::CharacterStateComponent>(entity);
    if (!state.is_online) {
      return std::nullopt;
    }
    return identity.id;
  }
  return std::nullopt;
}

ecs::ChatPreferenceComponent& EnsureChatPreference(entt::registry& registry,
                                                   entt::entity entity) {
  return registry.get_or_emplace<ecs::ChatPreferenceComponent>(entity);
}

std::vector<uint8_t> BuildChatRsp(mir2::common::ErrorCode code) {
  flatbuffers::FlatBufferBuilder builder;
  const auto rsp = mir2::proto::CreateChatRsp(builder, ToProtoError(code));
  builder.Finish(rsp);
  const uint8_t* data = builder.GetBufferPointer();
  return std::vector<uint8_t>(data, data + builder.GetSize());
}

}  // namespace

ChatHandler::ChatHandler(ResponseSender& response_sender,
                         PlayerPresenceService& player_presence_service,
                         mir2::game::map::AOIManager& aoi_mgr,
                         entt::registry& ecs_registry)
    : response_sender_(response_sender),
      aoi_mgr_(aoi_mgr),
      ecs_registry_(ecs_registry),
      chat_service_(std::make_unique<mir2::game::chat::ChatService>(
          player_presence_service, ecs_registry_)) {}

ChatHandler::~ChatHandler() = default;

Task<void> ChatHandler::HandleMessage(HandlerContext ctx,
                                      const uint8_t* payload,
                                      size_t payload_size) {
  if (!payload || payload_size == 0) {
    SYSLOG_WARN("ChatHandler ignored empty payload (client_id={})", ctx.client_id);
    co_await SendChatResponse(ctx, mir2::common::ErrorCode::kInvalidAction);
    co_return;
  }

  flatbuffers::Verifier verifier(payload, payload_size);
  if (!verifier.VerifyBuffer<mir2::proto::ChatReq>(nullptr)) {
    SYSLOG_WARN("ChatHandler payload verify failed (client_id={})", ctx.client_id);
    co_await SendChatResponse(ctx, mir2::common::ErrorCode::kInvalidAction);
    co_return;
  }

  const auto* req = flatbuffers::GetRoot<mir2::proto::ChatReq>(payload);
  if (!req || !req->content()) {
    SYSLOG_WARN("ChatHandler payload missing content (client_id={})", ctx.client_id);
    co_await SendChatResponse(ctx, mir2::common::ErrorCode::kInvalidAction);
    co_return;
  }

  const std::string content = req->content()->str();
  if (content.empty()) {
    SYSLOG_WARN("ChatHandler empty content (client_id={})", ctx.client_id);
    co_await SendChatResponse(ctx, mir2::common::ErrorCode::kInvalidAction);
    co_return;
  }

  if (content.size() > kMaxChatLength) {
    SYSLOG_WARN("ChatHandler content too long (client_id={}, len={})",
                ctx.client_id, content.size());
    co_await SendChatResponse(ctx, mir2::common::ErrorCode::kInvalidAction);
    co_return;
  }

  // HandleHot will check for '@' commands internally
  co_await HandleHot(std::move(ctx), req->channel(), req->target_id(), content);
}

Task<void> ChatHandler::HandleHot(HandlerContext ctx,
                                  mir2::proto::ChatChannel channel,
                                  uint64_t target_id,
                                  std::string content) {
  if (content.empty()) {
    SYSLOG_WARN("ChatHandler empty content (client_id={})", ctx.client_id);
    co_await SendChatResponse(ctx, mir2::common::ErrorCode::kInvalidAction);
    co_return;
  }

  if (content.size() > kMaxChatLength) {
    SYSLOG_WARN("ChatHandler content too long (client_id={}, len={})",
                ctx.client_id, content.size());
    co_await SendChatResponse(ctx, mir2::common::ErrorCode::kInvalidAction);
    co_return;
  }

  if (content.size() > 1 && content[0] == '@') {
    co_await HandleChatCommand(std::move(ctx), content);
    co_return;
  }

  switch (channel) {
    case mir2::proto::ChatChannel::WORLD:
      co_await HandleWorldChat(std::move(ctx), content);
      break;
    case mir2::proto::ChatChannel::PRIVATE:
      co_await HandlePrivateChat(std::move(ctx), target_id, content);
      break;
    case mir2::proto::ChatChannel::TEAM:
      co_await HandleTeamChat(std::move(ctx), content);
      break;
    case mir2::proto::ChatChannel::AREA:
      co_await HandleAreaChat(std::move(ctx), content);
      break;
    case mir2::proto::ChatChannel::GUILD:
      co_await HandleGuildChat(std::move(ctx), content);
      break;
    default:
      co_await HandleWorldChat(std::move(ctx), content);
      break;
  }
}

Task<void> ChatHandler::HandleWorldChat(HandlerContext ctx, const std::string& content) {
  auto character_id = GetCharacterId(ctx, ecs_registry_);
  if (!character_id.has_value()) {
    SYSLOG_WARN("ChatHandler world chat invalid context (client_id={})", ctx.client_id);
    co_await SendChatResponse(ctx, mir2::common::ErrorCode::kInvalidAction);
    co_return;
  }

  const auto dispatches =
      chat_service_->SendNormalChat(*character_id, content, aoi_mgr_);
  co_await SendChatDispatches(static_cast<uint16_t>(mir2::common::MsgId::kChatReq),
                              dispatches);
  co_await SendChatResponse(ctx, mir2::common::ErrorCode::kOk);

  SYSLOG_DEBUG("ChatHandler world chat character_id={} count={}",
               *character_id, dispatches.size());
}

Task<void> ChatHandler::HandlePrivateChat(HandlerContext ctx,
                                          uint64_t target_id,
                                          const std::string& content) {
  auto character_id = GetCharacterId(ctx, ecs_registry_);
  if (!character_id.has_value()) {
    SYSLOG_WARN("ChatHandler private chat invalid context (client_id={})", ctx.client_id);
    co_await SendChatResponse(ctx, mir2::common::ErrorCode::kInvalidAction);
    co_return;
  }

  if (!FindOnlineCharacterEntity(ecs_registry_, target_id).has_value()) {
    SYSLOG_WARN("ChatHandler private target offline (character_id={}, target_id={})",
                *character_id, target_id);
    co_await SendChatResponse(ctx, mir2::common::ErrorCode::kTargetNotFound);
    co_return;
  }

  auto dispatches = chat_service_->SendWhisper(*character_id, target_id, content);
  if (dispatches.empty()) {
    SYSLOG_WARN("ChatHandler private chat refused (character_id={}, target_id={})",
                *character_id, target_id);
    co_await SendChatResponse(ctx, mir2::common::ErrorCode::kTargetRefused);
    co_return;
  }

  co_await SendChatDispatches(static_cast<uint16_t>(mir2::common::MsgId::kPrivateChat),
                              dispatches);
  co_await SendChatResponse(ctx, mir2::common::ErrorCode::kOk);

  SYSLOG_DEBUG("ChatHandler private chat character_id={} target={} count={}",
               *character_id, target_id, dispatches.size());
}

Task<void> ChatHandler::HandleTeamChat(HandlerContext ctx, const std::string& content) {
  auto character_id = GetCharacterId(ctx, ecs_registry_);
  if (!character_id.has_value()) {
    SYSLOG_WARN("ChatHandler team chat invalid context (client_id={})", ctx.client_id);
    co_await SendChatResponse(ctx, mir2::common::ErrorCode::kInvalidAction);
    co_return;
  }

  auto dispatches = chat_service_->SendTeamChat(*character_id, content);
  if (dispatches.empty()) {
    SYSLOG_WARN("ChatHandler team chat no party (character_id={})", *character_id);
    co_await SendChatResponse(ctx, mir2::common::ErrorCode::kNoParty);
    co_return;
  }

  co_await SendChatDispatches(static_cast<uint16_t>(mir2::common::MsgId::kTeamChat),
                              dispatches);
  co_await SendChatResponse(ctx, mir2::common::ErrorCode::kOk);

  SYSLOG_DEBUG("ChatHandler team chat character_id={} count={}",
               *character_id, dispatches.size());
}

Task<void> ChatHandler::HandleAreaChat(HandlerContext ctx, const std::string& content) {
  auto character_id = GetCharacterId(ctx, ecs_registry_);
  if (!character_id.has_value()) {
    SYSLOG_WARN("ChatHandler area chat invalid context (client_id={})", ctx.client_id);
    co_await SendChatResponse(ctx, mir2::common::ErrorCode::kInvalidAction);
    co_return;
  }

  const auto dispatches = chat_service_->SendAreaChat(*character_id, content);
  co_await SendChatDispatches(static_cast<uint16_t>(mir2::common::MsgId::kAreaChat),
                              dispatches);
  co_await SendChatResponse(ctx, mir2::common::ErrorCode::kOk);

  SYSLOG_DEBUG("ChatHandler area chat character_id={} count={}",
               *character_id, dispatches.size());
}

Task<void> ChatHandler::HandleGuildChat(HandlerContext ctx, const std::string& content) {
  auto character_id = GetCharacterId(ctx, ecs_registry_);
  if (!character_id.has_value()) {
    SYSLOG_WARN("ChatHandler guild chat invalid context (client_id={})", ctx.client_id);
    co_await SendChatResponse(ctx, mir2::common::ErrorCode::kInvalidAction);
    co_return;
  }

  auto dispatches = chat_service_->SendGuildChat(*character_id, content);
  if (dispatches.empty()) {
    SYSLOG_WARN("ChatHandler guild chat unavailable (character_id={})", *character_id);
    co_await SendChatResponse(ctx, mir2::common::ErrorCode::kInvalidAction);
    co_return;
  }

  co_await SendChatDispatches(static_cast<uint16_t>(mir2::common::MsgId::kGuildChat),
                              dispatches);
  co_await SendChatResponse(ctx, mir2::common::ErrorCode::kOk);

  SYSLOG_DEBUG("ChatHandler guild chat character_id={} count={}",
               *character_id, dispatches.size());
}

Task<void> ChatHandler::HandleChatCommand(HandlerContext ctx, const std::string& content) {
  // Use ctx.entity directly instead of finding it
  if (ctx.entity == entt::null || !ecs_registry_.valid(ctx.entity)) {
    SYSLOG_WARN("ChatHandler command invalid entity (client_id={})", ctx.client_id);
    co_await SendChatResponse(ctx, mir2::common::ErrorCode::kInvalidAction);
    co_return;
  }

  // Get character ID for comparison in block command
  auto character_id = GetCharacterId(ctx, ecs_registry_);
  if (!character_id.has_value()) {
    SYSLOG_WARN("ChatHandler command missing character identity (client_id={})", ctx.client_id);
    co_await SendChatResponse(ctx, mir2::common::ErrorCode::kInvalidAction);
    co_return;
  }

  auto& chat_pref = EnsureChatPreference(ecs_registry_, ctx.entity);

  size_t space_pos = content.find(' ');
  std::string cmd = (space_pos != std::string::npos)
                        ? content.substr(1, space_pos - 1)
                        : content.substr(1);
  std::string arg = (space_pos != std::string::npos)
                        ? content.substr(space_pos + 1)
                        : "";

  mir2::common::ErrorCode result = mir2::common::ErrorCode::kInvalidAction;

  if (cmd == "block" && !arg.empty()) {
    auto target_id = FindOnlineCharacterIdByName(ecs_registry_, arg);
    if (target_id.has_value() && *target_id != *character_id) {
      if (chat_pref.AddBlock(static_cast<uint32_t>(*target_id))) {
        result = mir2::common::ErrorCode::kOk;
      }
    } else {
      result = mir2::common::ErrorCode::kTargetNotFound;
    }
  } else if (cmd == "unblock" && !arg.empty()) {
    auto target_id = FindOnlineCharacterIdByName(ecs_registry_, arg);
    if (target_id.has_value()) {
      chat_pref.RemoveBlock(static_cast<uint32_t>(*target_id));
      result = mir2::common::ErrorCode::kOk;
    } else {
      result = mir2::common::ErrorCode::kTargetNotFound;
    }
  } else if (cmd == "whisper") {
    if (arg == "on") {
      chat_pref.hear_whisper = true;
      result = mir2::common::ErrorCode::kOk;
    } else if (arg == "off") {
      chat_pref.hear_whisper = false;
      result = mir2::common::ErrorCode::kOk;
    }
  } else if (cmd == "cry") {
    if (arg == "on") {
      chat_pref.hear_cry = true;
      result = mir2::common::ErrorCode::kOk;
    } else if (arg == "off") {
      chat_pref.hear_cry = false;
      result = mir2::common::ErrorCode::kOk;
    }
  } else if (cmd == "guild") {
    if (arg == "on") {
      chat_pref.hear_guild_msg = true;
      result = mir2::common::ErrorCode::kOk;
    } else if (arg == "off") {
      chat_pref.hear_guild_msg = false;
      result = mir2::common::ErrorCode::kOk;
    }
  }

  co_await SendChatResponse(ctx, result);
  SYSLOG_DEBUG("ChatHandler command={} result={} character_id={}",
               cmd, static_cast<uint16_t>(result), *character_id);
}

Task<void> ChatHandler::SendChatResponse(HandlerContext ctx, mir2::common::ErrorCode code) {
  auto payload = BuildChatRsp(code);
  co_await response_sender_.SendAsync(
      ctx.client_id,
      static_cast<uint16_t>(mir2::common::MsgId::kChatRsp),
      std::move(payload));
}

Task<void> ChatHandler::SendChatDispatches(
    uint16_t msg_id,
    const mir2::game::chat::ChatDispatchList& dispatches) {
  for (const auto& dispatch : dispatches) {
    co_await response_sender_.SendAsync(dispatch.first, msg_id, dispatch.second);
  }
}

}  // namespace mir2::logic
