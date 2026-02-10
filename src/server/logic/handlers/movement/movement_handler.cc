#include "logic/handlers/movement/movement_handler.h"

#include <flatbuffers/flatbuffers.h>

#include <limits>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "common/enums.h"
#include "common/protocol/message_codec.h"
#include "core/utils.h"
#include "ecs/components/character_components.h"
#include "ecs/character_entity_manager.h"
#include "game/map/gate_manager.h"
#include "game/map/teleport_command.h"
#include "game_generated.h"
#include "ecs/systems/teleport_system.h"
#include "log/logger.h"
#include "logic/coroutine_executor.h"
#include "logic/handlers/handler_error_utils.h"
#include "logic/response_sender.h"
#include "security/anti_cheat.h"

namespace mir2::logic {

namespace {

std::vector<uint8_t> BuildMoveRsp(mir2::common::ErrorCode code, int x, int y) {
  mir2::common::MoveResponse response;
  response.code = static_cast<mir2::proto::ErrorCode>(static_cast<uint16_t>(code));
  response.x = x;
  response.y = y;

  mir2::common::MessageCodecStatus status = mir2::common::MessageCodecStatus::kOk;
  auto payload = mir2::common::EncodeMoveResponse(response, &status);
  if (status == mir2::common::MessageCodecStatus::kOk) {
    return payload;
  }

  response.code = mir2::proto::ErrorCode::ERR_UNKNOWN;
  response.x = 0;
  response.y = 0;
  return mir2::common::EncodeMoveResponse(response, nullptr);
}

std::vector<uint8_t> BuildEntityMove(uint64_t entity_id, int x, int y, uint8_t direction) {
  flatbuffers::FlatBufferBuilder builder;
  const auto rsp = mir2::proto::CreateEntityMove(
      builder, entity_id, mir2::proto::EntityType::PLAYER, x, y, direction);
  builder.Finish(rsp);
  const uint8_t* data = builder.GetBufferPointer();
  return std::vector<uint8_t>(data, data + builder.GetSize());
}

bool MapViolationToCheat(mir2::common::ErrorCode code,
                         mir2::security::CheatType* out_type,
                         int* out_severity) {
  switch (code) {
    case mir2::common::ErrorCode::kSpeedViolation:
      if (out_type) {
        *out_type = mir2::security::CheatType::kSpeedHack;
      }
      if (out_severity) {
        *out_severity = 10;
      }
      return true;
    case mir2::common::ErrorCode::kTargetOutOfRange:
    case mir2::common::ErrorCode::kInvalidPath:
    case mir2::common::ErrorCode::kPathBlocked:
      if (out_type) {
        *out_type = mir2::security::CheatType::kTeleportHack;
      }
      if (out_severity) {
        *out_severity = 5;
      }
      return true;
    default:
      return false;
  }
}

bool TryParseMapId(const std::string& value, int32_t* out) {
  if (!out) {
    return false;
  }
  try {
    size_t pos = 0;
    const long long parsed = std::stoll(value, &pos, 10);
    if (pos != value.size()) {
      return false;
    }
    if (parsed < std::numeric_limits<int32_t>::min() ||
        parsed > std::numeric_limits<int32_t>::max()) {
      return false;
    }
    *out = static_cast<int32_t>(parsed);
    return true;
  } catch (const std::exception&) {
    return false;
  }
}

}  // namespace

MovementHandler::MovementHandler(CoroutineExecutor& executor,
                                 ResponseSender& response_sender,
                                 ClientRegistry& registry,
                                 mir2::ecs::CharacterEntityManager& character_manager,
                                 mir2::game::map::SceneManager& scene_manager,
                                 uint32_t default_map_id,
                                 MovementValidator::Config validator_config,
                                 mir2::ecs::TeleportSystem* teleport_system,
                                 mir2::game::map::GateManager* gate_manager)
    : executor_(executor),
      response_sender_(response_sender),
      client_registry_(registry),
      character_manager_(character_manager),
      scene_manager_(scene_manager),
      ecs_registry_(nullptr),
      teleport_system_(teleport_system),
      gate_manager_(gate_manager),
      default_map_id_(default_map_id),
      validator_config_(validator_config) {}

MovementHandler::MovementHandler(CoroutineExecutor& executor,
                                 ResponseSender& response_sender,
                                 ClientRegistry& registry,
                                 mir2::ecs::CharacterEntityManager& character_manager,
                                 mir2::game::map::SceneManager& scene_manager,
                                 entt::registry& ecs_registry,
                                 uint32_t default_map_id,
                                 MovementValidator::Config validator_config,
                                 mir2::ecs::TeleportSystem* teleport_system,
                                 mir2::game::map::GateManager* gate_manager)
    : executor_(executor),
      response_sender_(response_sender),
      client_registry_(registry),
      character_manager_(character_manager),
      scene_manager_(scene_manager),
      ecs_registry_(&ecs_registry),
      teleport_system_(teleport_system),
      gate_manager_(gate_manager),
      default_map_id_(default_map_id),
      validator_config_(validator_config) {}

Task<void> MovementHandler::HandleMessage(HandlerContext ctx,
                                          const uint8_t* payload,
                                          size_t payload_size) {
  std::vector<uint8_t> payload_copy;
  if (payload && payload_size > 0) {
    payload_copy.assign(payload, payload + payload_size);
  }
  return HandleMessageOwned(std::move(ctx), std::move(payload_copy));
}

Task<void> MovementHandler::HandleMessageOwned(HandlerContext ctx,
                                               std::vector<uint8_t> payload) {
  if (payload.empty()) {
    SYSLOG_WARN("MovementHandler ignored empty payload (client_id={})", ctx.client_id);
    co_await SendMoveResponse(ctx.client_id,
                              mir2::common::ErrorCode::kInvalidAction,
                              0,
                              0);
    co_return;
  }

  mir2::common::MoveRequest request;
  const auto status = mir2::common::DecodeMoveRequest(
      static_cast<uint16_t>(mir2::common::MsgId::kMoveReq),
      payload.data(),
      payload.size(),
      &request);
  if (status != mir2::common::MessageCodecStatus::kOk) {
    SYSLOG_WARN("MovementHandler decode failed (client_id={}, status={})",
                ctx.client_id, static_cast<int>(status));
    co_await SendMoveResponse(ctx.client_id, ToCommonError(status), 0, 0);
    co_return;
  }

  co_await HandleMove(std::move(ctx), request);
}

Task<void> MovementHandler::HandleHot(HandlerContext ctx,
                                      int32_t target_x,
                                      int32_t target_y) {
  mir2::common::MoveRequest request;
  request.target_x = target_x;
  request.target_y = target_y;
  co_await HandleMove(std::move(ctx), request);
}

Task<void> MovementHandler::HandleMove(HandlerContext ctx,
                                       const mir2::common::MoveRequest& request) {
  mir2::common::ErrorCode result = mir2::common::ErrorCode::kOk;
  const int x = request.target_x;
  const int y = request.target_y;
  uint64_t entity_id = 0;
  uint32_t map_id = default_map_id_;
  const int64_t now_ms = mir2::core::GetCurrentTimestampMs();
  bool should_broadcast = false;
  entt::entity entity = ctx.entity;
  std::optional<mir2::game::map::GateInfo> triggered_gate;

  {
    std::lock_guard<std::mutex> lock(move_mutex_);
    entt::registry* registry = ctx.registry != nullptr ? ctx.registry : ecs_registry_;

    if (!registry || entity == entt::null || !registry->valid(entity)) {
      result = mir2::common::ErrorCode::kInvalidAction;
    } else {
      const auto* identity = registry->try_get<mir2::ecs::CharacterIdentityComponent>(entity);
      if (!identity || identity->id == 0) {
        result = mir2::common::ErrorCode::kInvalidAction;
      } else {
        entity_id = identity->id;
      }
    }

    if (result == mir2::common::ErrorCode::kOk) {
      auto& state = registry->get_or_emplace<mir2::ecs::CharacterStateComponent>(entity);
      map_id = state.map_id;
      const mir2::common::Position from = state.position;
      const mir2::common::Position to{x, y};
      int speed = 0;
      if (auto* attrs = registry->try_get<mir2::ecs::CharacterAttributesComponent>(entity)) {
        speed = attrs->speed;
      }

      int64_t last_move_time = 0;
      auto it = last_move_time_ms_.find(entity_id);
      if (it != last_move_time_ms_.end()) {
        last_move_time = it->second;
      }

      const auto* map_instance = scene_manager_.GetMap(static_cast<int32_t>(map_id));
      if (!map_instance) {
        result = mir2::common::ErrorCode::kInvalidAction;
      } else {
        MovementValidator validator(*map_instance, validator_config_);
        result = validator.Validate(from, to, speed, last_move_time, now_ms);
        if (result == mir2::common::ErrorCode::kOk) {
          const bool anti_cheat_ok = mir2::security::AntiCheat::Instance().ValidateMove(
              entity_id, from.x, from.y, to.x, to.y, now_ms);
          if (!anti_cheat_ok) {
            result = mir2::common::ErrorCode::kSpeedViolation;
          }
        }

        if (result == mir2::common::ErrorCode::kOk) {
          character_manager_.SetPosition(static_cast<uint32_t>(entity_id), x, y, map_id);
          if (entity != entt::null) {
            auto* current_map = scene_manager_.GetMapByEntity(entity);
            if (!current_map ||
                current_map->GetMapId() != static_cast<int32_t>(map_id)) {
              scene_manager_.AddEntityToMap(static_cast<int32_t>(map_id), entity, x, y);
            } else {
              scene_manager_.UpdateEntityPosition(entity, x, y);
            }
          }
          last_move_time_ms_[entity_id] = now_ms;
          should_broadcast = true;
          if (gate_manager_) {
            triggered_gate =
                gate_manager_->CheckGateTrigger(static_cast<int32_t>(map_id), x, y);
          }
        } else {
          RecordMoveViolation(entity_id, result, now_ms);
        }
      }
    }
  }

  co_await SendMoveResponse(ctx.client_id, result, x, y);

  if (result == mir2::common::ErrorCode::kOk && should_broadcast) {
    co_await BroadcastEntityMove(entity_id, x, y, 0);
  }

  if (result == mir2::common::ErrorCode::kOk && should_broadcast &&
      triggered_gate.has_value() && teleport_system_ && entity != entt::null) {
    int32_t target_map_id = 0;
    if (TryParseMapId(triggered_gate->target_map, &target_map_id)) {
      mir2::game::map::TeleportCommand cmd(
          entity, target_map_id, triggered_gate->target_x, triggered_gate->target_y);
      teleport_system_->RequestTeleport(cmd);
    }
  }

  SYSLOG_DEBUG("MovementHandler move client_id={} result={}",
               ctx.client_id, static_cast<int>(result));
  co_return;
}

Task<void> MovementHandler::SendMoveResponse(uint64_t client_id,
                                             mir2::common::ErrorCode code,
                                             int x,
                                             int y) {
  auto payload = BuildMoveRsp(code, x, y);
  co_await response_sender_.SendAsync(
      client_id,
      static_cast<uint16_t>(mir2::common::MsgId::kMoveRsp),
      std::move(payload));
}

Task<void> MovementHandler::BroadcastEntityMove(uint64_t entity_id,
                                                int x,
                                                int y,
                                                uint8_t direction) {
  const auto payload = BuildEntityMove(entity_id, x, y, direction);
  for (const auto client_id : client_registry_.GetAll()) {
    co_await response_sender_.SendAsync(
        client_id,
        static_cast<uint16_t>(mir2::common::MsgId::kEntityMove),
        payload);
  }
}

void MovementHandler::RecordMoveViolation(uint64_t player_id,
                                          mir2::common::ErrorCode code,
                                          int64_t timestamp_ms) {
  mir2::security::CheatType cheat_type;
  int severity = 0;
  if (!MapViolationToCheat(code, &cheat_type, &severity)) {
    return;
  }
  mir2::security::AntiCheat::Instance().ProcessViolation(
      player_id, cheat_type, severity, timestamp_ms);
}

}  // namespace mir2::logic
