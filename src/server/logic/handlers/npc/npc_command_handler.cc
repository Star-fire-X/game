#include "logic/handlers/npc/npc_command_handler.h"

#include <exception>
#include <limits>
#include <string>
#include <utility>

#include <nlohmann/json.hpp>

#include "common/enums.h"
#include "common/protocol/npc_message_codec.h"
#include "ecs/components/character_components.h"
#include "ecs/world.h"
#include "game/npc/npc_interaction_handler.h"
#include "game/npc/npc_manager.h"
#include "game/npc/npc_script_engine.h"
#include "log/logger.h"
#include "logic/response_sender.h"

namespace mir2::logic {

namespace {

constexpr int32_t kMaxNpcInteractDistanceTiles = 12;
constexpr int64_t kMaxNpcInteractDistanceSquared =
    static_cast<int64_t>(kMaxNpcInteractDistanceTiles) *
    static_cast<int64_t>(kMaxNpcInteractDistanceTiles);

struct PlayerContext {
  uint64_t player_id = 0;
  uint32_t map_id = 0;
  mir2::common::Position position{};
};

bool ParseJsonObject(const uint8_t* payload,
                     size_t payload_size,
                     nlohmann::json* out) {
  if (!payload || payload_size == 0 || !out) {
    return false;
  }

  try {
    *out = nlohmann::json::parse(payload, payload + payload_size);
  } catch (const nlohmann::json::exception&) {
    return false;
  }

  return out->is_object();
}

bool ReadUInt64(const nlohmann::json& j, const char* key, uint64_t* out) {
  if (!out || !j.contains(key)) {
    return false;
  }
  const auto& value = j.at(key);
  if (!value.is_number_unsigned() && !value.is_number_integer()) {
    return false;
  }
  const auto raw = value.get<uint64_t>();
  *out = raw;
  return true;
}

bool ReadUInt32(const nlohmann::json& j, const char* key, uint32_t* out) {
  if (!out) {
    return false;
  }
  uint64_t temp = 0;
  if (!ReadUInt64(j, key, &temp)) {
    return false;
  }
  if (temp > std::numeric_limits<uint32_t>::max()) {
    return false;
  }
  *out = static_cast<uint32_t>(temp);
  return true;
}

bool ReadUInt8(const nlohmann::json& j, const char* key, uint8_t* out) {
  if (!out) {
    return false;
  }
  uint64_t temp = 0;
  if (!ReadUInt64(j, key, &temp)) {
    return false;
  }
  if (temp > std::numeric_limits<uint8_t>::max()) {
    return false;
  }
  *out = static_cast<uint8_t>(temp);
  return true;
}

std::vector<uint8_t> BuildNpcInteractRsp(uint64_t npc_id,
                                         uint8_t result,
                                         const std::string& npc_name,
                                         uint8_t npc_type) {
  nlohmann::json j;
  j["version"] = mir2::common::kNpcCodecVersion;
  j["npc_id"] = npc_id;
  j["result"] = result;
  if (!npc_name.empty()) {
    j["npc_name"] = npc_name;
  }
  if (npc_type != 0) {
    j["npc_type"] = npc_type;
  }

  const auto dumped = j.dump();
  return std::vector<uint8_t>(dumped.begin(), dumped.end());
}

bool IsWithinNpcInteractionDistance(const mir2::common::Position& position,
                                    int32_t npc_x,
                                    int32_t npc_y) {
  const int64_t dx = static_cast<int64_t>(position.x) - static_cast<int64_t>(npc_x);
  const int64_t dy = static_cast<int64_t>(position.y) - static_cast<int64_t>(npc_y);
  return (dx * dx + dy * dy) <= kMaxNpcInteractDistanceSquared;
}

bool TryResolvePlayerContext(const HandlerContext& ctx,
                             entt::registry& fallback_registry,
                             PlayerContext* out) {
  if (!out || ctx.entity == entt::null) {
    return false;
  }

  entt::registry* registry = ctx.registry != nullptr ? ctx.registry : &fallback_registry;
  if (!registry || !registry->valid(ctx.entity)) {
    return false;
  }

  const auto* identity = registry->try_get<ecs::CharacterIdentityComponent>(ctx.entity);
  const auto* state = registry->try_get<ecs::CharacterStateComponent>(ctx.entity);
  if (!identity || !state || identity->id == 0) {
    return false;
  }

  out->player_id = identity->id;
  out->map_id = state->map_id;
  out->position = state->position;
  return true;
}

bool ValidateNpcReachability(const PlayerContext& player_ctx,
                             const game::npc::NpcData& npc_data) {
  if (npc_data.map_id != player_ctx.map_id) {
    return false;
  }
  return IsWithinNpcInteractionDistance(player_ctx.position, npc_data.x, npc_data.y);
}

}  // namespace

NpcCommandHandler::NpcCommandHandler(CoroutineExecutor& executor,
                                     ResponseSender& response_sender,
                                     entt::registry& registry,
                                     game::map::SceneManager& scene_manager)
    : executor_(executor),
      response_sender_(response_sender),
      registry_(registry),
      scene_manager_(scene_manager) {}

Task<void> NpcCommandHandler::HandleMessage(HandlerContext ctx,
                                            uint16_t msg_id,
                                            const uint8_t* payload,
                                            size_t payload_size) {
  bool send_fallback = false;
  try {
    switch (msg_id) {
      case static_cast<uint16_t>(mir2::common::MsgId::kNpcInteractReq):
        co_await HandleNpcInteract(ctx, payload, payload_size);
        break;
      case static_cast<uint16_t>(mir2::common::MsgId::kNpcMenuSelect):
        co_await HandleNpcMenuSelect(ctx, payload, payload_size);
        break;
      default:
        SYSLOG_WARN("NpcCommandHandler unknown msg_id={} (client_id={})",
                    msg_id, ctx.client_id);
        break;
    }
    co_return;
  } catch (const std::exception& ex) {
    SYSLOG_ERROR("NpcCommandHandler exception client_id={} msg_id={} error={}",
                 ctx.client_id, msg_id, ex.what());
    send_fallback = true;
  } catch (...) {
    SYSLOG_ERROR("NpcCommandHandler exception client_id={} msg_id={} error=unknown",
                 ctx.client_id, msg_id);
    send_fallback = true;
  }

  if (send_fallback) {
    try {
      co_await SendNpcInteractResponse(ctx, 0, 1, "", 0);
    } catch (const std::exception& send_ex) {
      SYSLOG_ERROR("NpcCommandHandler fallback send failed client_id={} msg_id={} error={}",
                   ctx.client_id, msg_id, send_ex.what());
    } catch (...) {
      SYSLOG_ERROR("NpcCommandHandler fallback send failed client_id={} msg_id={} error=unknown",
                   ctx.client_id, msg_id);
    }
  }
  co_return;
}

Task<void> NpcCommandHandler::HandleNpcInteract(HandlerContext ctx,
                                                const uint8_t* payload,
                                                size_t payload_size) {
  uint64_t npc_id = 0;
  bool send_failure_rsp = false;
  try {
    nlohmann::json j;
    if (!ParseJsonObject(payload, payload_size, &j)) {
      SYSLOG_WARN("NpcCommandHandler interact parse failed (client_id={})", ctx.client_id);
      auto rsp = BuildNpcInteractRsp(0, 1, "", 0);
      co_await response_sender_.SendAsync(
          ctx.client_id,
          static_cast<uint16_t>(mir2::common::MsgId::kNpcInteractRsp),
          std::move(rsp));
      co_return;
    }

    uint32_t version = 0;
    if (!ReadUInt32(j, "version", &version) ||
        version != mir2::common::kNpcCodecVersion) {
      SYSLOG_WARN("NpcCommandHandler interact invalid version (client_id={})",
                  ctx.client_id);
      auto rsp = BuildNpcInteractRsp(0, 1, "", 0);
      co_await response_sender_.SendAsync(
          ctx.client_id,
          static_cast<uint16_t>(mir2::common::MsgId::kNpcInteractRsp),
          std::move(rsp));
      co_return;
    }

    uint64_t player_id = 0;
    if (!ReadUInt64(j, "npc_id", &npc_id) ||
        !ReadUInt64(j, "player_id", &player_id) ||
        npc_id == 0 || player_id == 0) {
      SYSLOG_WARN("NpcCommandHandler interact missing ids (client_id={})", ctx.client_id);
      auto rsp = BuildNpcInteractRsp(npc_id, 1, "", 0);
      co_await response_sender_.SendAsync(
          ctx.client_id,
          static_cast<uint16_t>(mir2::common::MsgId::kNpcInteractRsp),
          std::move(rsp));
      co_return;
    }

    PlayerContext player_ctx;
    if (!TryResolvePlayerContext(ctx, registry_, &player_ctx)) {
      SYSLOG_WARN("NpcCommandHandler interact missing player context (client_id={})",
                  ctx.client_id);
      auto rsp = BuildNpcInteractRsp(npc_id, 1, "", 0);
      co_await response_sender_.SendAsync(
          ctx.client_id,
          static_cast<uint16_t>(mir2::common::MsgId::kNpcInteractRsp),
          std::move(rsp));
      co_return;
    }

    if (player_id != player_ctx.player_id) {
      SYSLOG_WARN("NpcCommandHandler interact player mismatch (client_id={}, claimed={}, actual={})",
                  ctx.client_id,
                  player_id,
                  player_ctx.player_id);
      auto rsp = BuildNpcInteractRsp(npc_id, 1, "", 0);
      co_await response_sender_.SendAsync(
          ctx.client_id,
          static_cast<uint16_t>(mir2::common::MsgId::kNpcInteractRsp),
          std::move(rsp));
      co_return;
    }

    auto npc_data_opt = game::npc::NpcManager::Instance().GetNpcData(npc_id);
    if (!npc_data_opt) {
      SYSLOG_WARN("NpcCommandHandler interact npc missing (client_id={}, npc_id={})",
                  ctx.client_id, npc_id);
      auto rsp = BuildNpcInteractRsp(npc_id, 1, "", 0);
      co_await response_sender_.SendAsync(
          ctx.client_id,
          static_cast<uint16_t>(mir2::common::MsgId::kNpcInteractRsp),
          std::move(rsp));
      co_return;
    }

    const auto& npc_data = *npc_data_opt;
    if (!npc_data.enabled) {
      SYSLOG_WARN("NpcCommandHandler interact npc disabled (client_id={}, npc_id={})",
                  ctx.client_id,
                  npc_id);
      auto rsp = BuildNpcInteractRsp(npc_id, 1, "", 0);
      co_await response_sender_.SendAsync(
          ctx.client_id,
          static_cast<uint16_t>(mir2::common::MsgId::kNpcInteractRsp),
          std::move(rsp));
      co_return;
    }

    if (!ValidateNpcReachability(player_ctx, npc_data)) {
      SYSLOG_WARN(
          "NpcCommandHandler interact out-of-range or map mismatch (client_id={}, player_id={}, "
          "player_map={}, player_pos=({}, {}), npc_id={}, npc_map={}, npc_pos=({}, {}))",
          ctx.client_id,
          player_ctx.player_id,
          player_ctx.map_id,
          player_ctx.position.x,
          player_ctx.position.y,
          npc_id,
          npc_data.map_id,
          npc_data.x,
          npc_data.y);
      auto rsp = BuildNpcInteractRsp(npc_id, 1, "", 0);
      co_await response_sender_.SendAsync(
          ctx.client_id,
          static_cast<uint16_t>(mir2::common::MsgId::kNpcInteractRsp),
          std::move(rsp));
      co_return;
    }

    auto rsp = BuildNpcInteractRsp(npc_id, 0, npc_data.name,
                                   static_cast<uint8_t>(npc_data.type));
    co_await response_sender_.SendAsync(
        ctx.client_id,
        static_cast<uint16_t>(mir2::common::MsgId::kNpcInteractRsp),
        std::move(rsp));

    if (ctx.world != nullptr) {
      if (auto npc = game::npc::NpcManager::Instance().GetNpc(npc_id)) {
        game::npc::NpcScriptEngine script_engine;
        game::npc::NpcInteractionHandler interaction_handler(
            ctx.world->GetEventBus(), script_engine);
        (void)interaction_handler.HandleInteraction(ctx.entity, *npc, "TALK");
      }
    }

    SYSLOG_DEBUG("NpcCommandHandler interact client_id={} npc_id={}",
                 ctx.client_id, npc_id);
    co_return;
  } catch (const std::exception& ex) {
    SYSLOG_ERROR("NpcCommandHandler interact exception client_id={} npc_id={} error={}",
                 ctx.client_id, npc_id, ex.what());
    send_failure_rsp = true;
  } catch (...) {
    SYSLOG_ERROR("NpcCommandHandler interact exception client_id={} npc_id={} error=unknown",
                 ctx.client_id, npc_id);
    send_failure_rsp = true;
  }

  if (send_failure_rsp) {
    try {
      co_await SendNpcInteractResponse(ctx, npc_id, 1, "", 0);
    } catch (const std::exception& send_ex) {
      SYSLOG_ERROR("NpcCommandHandler interact fallback send failed client_id={} npc_id={} error={}",
                   ctx.client_id, npc_id, send_ex.what());
    } catch (...) {
      SYSLOG_ERROR(
          "NpcCommandHandler interact fallback send failed client_id={} npc_id={} error=unknown",
          ctx.client_id, npc_id);
    }
  }
  co_return;
}

Task<void> NpcCommandHandler::HandleNpcMenuSelect(HandlerContext ctx,
                                                  const uint8_t* payload,
                                                  size_t payload_size) {
  uint64_t npc_id = 0;
  bool send_failure_rsp = false;
  try {
    nlohmann::json j;
    if (!ParseJsonObject(payload, payload_size, &j)) {
      SYSLOG_WARN("NpcCommandHandler menu select parse failed (client_id={})", ctx.client_id);
      auto rsp = BuildNpcInteractRsp(0, 1, "", 0);
      co_await response_sender_.SendAsync(
          ctx.client_id,
          static_cast<uint16_t>(mir2::common::MsgId::kNpcInteractRsp),
          std::move(rsp));
      co_return;
    }

    uint32_t version = 0;
    if (!ReadUInt32(j, "version", &version) ||
        version != mir2::common::kNpcCodecVersion) {
      SYSLOG_WARN("NpcCommandHandler menu select invalid version (client_id={})",
                  ctx.client_id);
      auto rsp = BuildNpcInteractRsp(0, 1, "", 0);
      co_await response_sender_.SendAsync(
          ctx.client_id,
          static_cast<uint16_t>(mir2::common::MsgId::kNpcInteractRsp),
          std::move(rsp));
      co_return;
    }

    uint8_t option_index = 0;
    if (!ReadUInt64(j, "npc_id", &npc_id) ||
        !ReadUInt8(j, "option_index", &option_index) ||
        npc_id == 0) {
      SYSLOG_WARN("NpcCommandHandler menu select invalid payload (client_id={})",
                  ctx.client_id);
      auto rsp = BuildNpcInteractRsp(npc_id, 1, "", 0);
      co_await response_sender_.SendAsync(
          ctx.client_id,
          static_cast<uint16_t>(mir2::common::MsgId::kNpcInteractRsp),
          std::move(rsp));
      co_return;
    }

    PlayerContext player_ctx;
    if (!TryResolvePlayerContext(ctx, registry_, &player_ctx)) {
      SYSLOG_WARN("NpcCommandHandler menu select missing player context (client_id={})",
                  ctx.client_id);
      auto rsp = BuildNpcInteractRsp(npc_id, 1, "", 0);
      co_await response_sender_.SendAsync(
          ctx.client_id,
          static_cast<uint16_t>(mir2::common::MsgId::kNpcInteractRsp),
          std::move(rsp));
      co_return;
    }

    const auto npc_data_opt = game::npc::NpcManager::Instance().GetNpcData(npc_id);
    if (!npc_data_opt || !npc_data_opt->enabled ||
        !ValidateNpcReachability(player_ctx, *npc_data_opt)) {
      SYSLOG_WARN(
          "NpcCommandHandler menu select rejected (client_id={}, player_id={}, npc_id={}, "
          "player_map={}, player_pos=({}, {}))",
          ctx.client_id,
          player_ctx.player_id,
          npc_id,
          player_ctx.map_id,
          player_ctx.position.x,
          player_ctx.position.y);
      auto rsp = BuildNpcInteractRsp(npc_id, 1, "", 0);
      co_await response_sender_.SendAsync(
          ctx.client_id,
          static_cast<uint16_t>(mir2::common::MsgId::kNpcInteractRsp),
          std::move(rsp));
      co_return;
    }

    SYSLOG_DEBUG("NpcCommandHandler menu select client_id={} npc_id={} option={}",
                 ctx.client_id, npc_id, static_cast<int>(option_index));
    co_return;
  } catch (const std::exception& ex) {
    SYSLOG_ERROR("NpcCommandHandler menu select exception client_id={} npc_id={} error={}",
                 ctx.client_id, npc_id, ex.what());
    send_failure_rsp = true;
  } catch (...) {
    SYSLOG_ERROR("NpcCommandHandler menu select exception client_id={} npc_id={} error=unknown",
                 ctx.client_id, npc_id);
    send_failure_rsp = true;
  }

  if (send_failure_rsp) {
    try {
      co_await SendNpcInteractResponse(ctx, npc_id, 1, "", 0);
    } catch (const std::exception& send_ex) {
      SYSLOG_ERROR(
          "NpcCommandHandler menu select fallback send failed client_id={} npc_id={} error={}",
          ctx.client_id, npc_id, send_ex.what());
    } catch (...) {
      SYSLOG_ERROR(
          "NpcCommandHandler menu select fallback send failed client_id={} npc_id={} error=unknown",
          ctx.client_id, npc_id);
    }
  }
  co_return;
}

Task<void> NpcCommandHandler::SendNpcInteractResponse(HandlerContext ctx,
                                                      uint64_t npc_id,
                                                      uint8_t result,
                                                      const std::string& npc_name,
                                                      uint8_t npc_type) {
  auto payload = BuildNpcInteractRsp(npc_id, result, npc_name, npc_type);
  co_await response_sender_.SendAsync(
      ctx.client_id,
      static_cast<uint16_t>(mir2::common::MsgId::kNpcInteractRsp),
      std::move(payload));
}

}  // namespace mir2::logic
