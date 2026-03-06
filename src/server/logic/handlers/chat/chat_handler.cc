#include "logic/handlers/chat/chat_handler.h"

#include <algorithm>
#include <cstddef>
#include <exception>
#include <chrono>
#include <optional>
#include <string>
#include <unordered_map>
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
#include "logic/services/session_role_store.h"
#include "monitor/metrics.h"

namespace mir2::logic {

namespace {

constexpr size_t kMaxChatLength = 256;
constexpr const char* kMetricChatDispatchBatchTotal = "logic.chat.dispatch.batch_total";
constexpr const char* kMetricChatDispatchRecipientTotal = "logic.chat.dispatch.recipient_total";
constexpr const char* kMetricChatDispatchFailedTotal = "logic.chat.dispatch.failed_total";
constexpr const char* kMetricChatDispatchDroppedTotal = "logic.chat.dispatch.dropped_total";
constexpr const char* kMetricChatDispatchLargestBatch = "logic.chat.dispatch.largest_batch";
constexpr const char* kMetricChatDispatchLatencyMs = "logic.chat.dispatch.loop_latency_ms";

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

uint64_t HashPayload(const std::vector<uint8_t>& payload) {
  uint64_t hash = 1469598103934665603ull;
  for (const uint8_t byte : payload) {
    hash ^= byte;
    hash *= 1099511628211ull;
  }
  return hash;
}

}  // namespace

ChatHandler::ChatHandler(ResponseSender& response_sender,
                         PlayerPresenceService& player_presence_service,
                         RoleStore& role_store,
                         mir2::game::map::AOIManager& aoi_mgr,
                         entt::registry& ecs_registry,
                         bool batch_send_enabled)
    : response_sender_(response_sender),
      role_store_(role_store),
      aoi_mgr_(aoi_mgr),
      ecs_registry_(ecs_registry),
      batch_send_enabled_(batch_send_enabled),
      chat_service_(std::make_unique<mir2::game::chat::ChatService>(
          player_presence_service, ecs_registry_)) {}

ChatHandler::~ChatHandler() = default;

Task<void> ChatHandler::HandleMessage(HandlerContext ctx,
                                      const uint8_t* payload,
                                      size_t payload_size) {
  bool send_invalid_action = false;
  try {
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
    co_return;
  } catch (const std::exception& ex) {
    SYSLOG_ERROR("ChatHandler HandleMessage exception client_id={} error={}",
                 ctx.client_id, ex.what());
    send_invalid_action = true;
  } catch (...) {
    SYSLOG_ERROR("ChatHandler HandleMessage exception client_id={} error=unknown",
                 ctx.client_id);
    send_invalid_action = true;
  }

  if (send_invalid_action) {
    try {
      co_await SendChatResponse(ctx, mir2::common::ErrorCode::kInvalidAction);
    } catch (const std::exception& send_ex) {
      SYSLOG_ERROR("ChatHandler HandleMessage fallback send failed client_id={} error={}",
                   ctx.client_id, send_ex.what());
    } catch (...) {
      SYSLOG_ERROR("ChatHandler HandleMessage fallback send failed client_id={} error=unknown",
                   ctx.client_id);
    }
  }
  co_return;
}

Task<void> ChatHandler::HandleHot(HandlerContext ctx,
                                  mir2::proto::ChatChannel channel,
                                  uint64_t target_id,
                                  std::string content) {
  bool send_invalid_action = false;
  try {
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
    co_return;
  } catch (const std::exception& ex) {
    SYSLOG_ERROR("ChatHandler HandleHot exception client_id={} channel={} target_id={} error={}",
                 ctx.client_id,
                 static_cast<int>(channel),
                 target_id,
                 ex.what());
    send_invalid_action = true;
  } catch (...) {
    SYSLOG_ERROR("ChatHandler HandleHot exception client_id={} channel={} target_id={} error=unknown",
                 ctx.client_id,
                 static_cast<int>(channel),
                 target_id);
    send_invalid_action = true;
  }

  if (send_invalid_action) {
    try {
      co_await SendChatResponse(ctx, mir2::common::ErrorCode::kInvalidAction);
    } catch (const std::exception& send_ex) {
      SYSLOG_ERROR("ChatHandler HandleHot fallback send failed client_id={} error={}",
                   ctx.client_id, send_ex.what());
    } catch (...) {
      SYSLOG_ERROR("ChatHandler HandleHot fallback send failed client_id={} error=unknown",
                   ctx.client_id);
    }
  }
  co_return;
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
  const auto started_at = std::chrono::steady_clock::now();

  size_t batch_total = 0;
  size_t recipient_total = 0;
  size_t largest_batch = 0;
  size_t failed = 0;
  size_t dropped = 0;

  if (!batch_send_enabled_) {
    for (const auto& dispatch : dispatches) {
      const uint64_t role_id = dispatch.first;
      const auto& payload = dispatch.second;
      if (role_id == 0 || payload.empty()) {
        ++dropped;
        continue;
      }

      const auto client_id_opt = role_store_.GetClientIdByRoleId(role_id);
      if (!client_id_opt.has_value()) {
        ++dropped;
        continue;
      }
      const uint64_t client_id = *client_id_opt;

      ++recipient_total;
      try {
        co_await response_sender_.SendAsync(client_id, msg_id, payload);
      } catch (...) {
        ++failed;
      }
    }
    batch_total = recipient_total;
    if (recipient_total > 0) {
      largest_batch = 1;
    }
  } else {
    struct DispatchBatch {
      std::vector<uint8_t> payload;
      std::vector<uint64_t> client_ids;
    };

    std::vector<DispatchBatch> batches;
    batches.reserve(dispatches.size());
    std::unordered_map<uint64_t, std::vector<size_t>> hash_to_batch_indices;
    hash_to_batch_indices.reserve(dispatches.size());

    for (const auto& dispatch : dispatches) {
      const uint64_t role_id = dispatch.first;
      const auto& payload = dispatch.second;

      if (role_id == 0 || payload.empty()) {
        ++dropped;
        continue;
      }

      const auto client_id_opt = role_store_.GetClientIdByRoleId(role_id);
      if (!client_id_opt.has_value()) {
        ++dropped;
        continue;
      }
      const uint64_t client_id = *client_id_opt;

      const uint64_t payload_hash = HashPayload(payload);
      bool grouped = false;

      const auto hash_it = hash_to_batch_indices.find(payload_hash);
      if (hash_it != hash_to_batch_indices.end()) {
        for (const size_t batch_index : hash_it->second) {
          if (batches[batch_index].payload == payload) {
            batches[batch_index].client_ids.push_back(client_id);
            grouped = true;
            break;
          }
        }
      }

      if (!grouped) {
        DispatchBatch batch;
        batch.payload = payload;
        batch.client_ids.push_back(client_id);
        const size_t batch_index = batches.size();
        batches.push_back(std::move(batch));
        hash_to_batch_indices[payload_hash].push_back(batch_index);
      }
    }

    for (const auto& batch : batches) {
      if (batch.client_ids.empty()) {
        continue;
      }
      ++batch_total;
      recipient_total += batch.client_ids.size();
      largest_batch = std::max(largest_batch, batch.client_ids.size());

      const SendManyResult result =
          co_await response_sender_.SendMany(batch.client_ids, msg_id, batch.payload);
      failed += result.failed;
      dropped += result.dropped;
    }
  }

  if (batch_total > 0) {
    monitor::Metrics::Instance().IncrementCounter(kMetricChatDispatchBatchTotal,
                                                  static_cast<uint64_t>(batch_total));
  }
  if (recipient_total > 0) {
    monitor::Metrics::Instance().IncrementCounter(kMetricChatDispatchRecipientTotal,
                                                  static_cast<uint64_t>(recipient_total));
  }
  if (failed > 0) {
    monitor::Metrics::Instance().IncrementCounter(kMetricChatDispatchFailedTotal,
                                                  static_cast<uint64_t>(failed));
  }
  if (dropped > 0) {
    monitor::Metrics::Instance().IncrementCounter(kMetricChatDispatchDroppedTotal,
                                                  static_cast<uint64_t>(dropped));
  }
  if (largest_batch > 0) {
    monitor::Metrics::Instance().SetGauge(kMetricChatDispatchLargestBatch,
                                          static_cast<double>(largest_batch));
  }

  const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
      std::chrono::steady_clock::now() - started_at);
  monitor::Metrics::Instance().SetGauge(
      kMetricChatDispatchLatencyMs, static_cast<double>(elapsed.count()) / 1000.0);

  if (failed > 0 || dropped > 0) {
    SYSLOG_WARN(
        "ChatHandler dispatch summary msg_id={} batches={} recipients={} failed={} dropped={} latency_us={}",
        msg_id,
        batch_total,
        recipient_total,
        failed,
        dropped,
        elapsed.count());
  }
}

}  // namespace mir2::logic
