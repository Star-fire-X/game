#include "handlers/movement/movement_handler.h"

#include <flatbuffers/flatbuffers.h>
#include <limits>

#include "common/protocol/message_codec.h"
#include "core/utils.h"
#include "ecs/components/character_components.h"
#include "game/map/teleport_command.h"
#include "game_generated.h"
#include "handlers/handler_utils.h"
#include "security/anti_cheat.h"

namespace legend2::handlers {

namespace {

std::vector<uint8_t> BuildMoveRsp(mir2::common::ErrorCode code, int x, int y) {
    mir2::common::MoveResponse response;
    response.code = ToProtoError(code);
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

MovementHandler::MovementHandler(ClientRegistry& registry,
                                 mir2::ecs::CharacterEntityManager& character_manager,
                                 mir2::game::map::SceneManager& scene_manager,
                                 uint32_t default_map_id,
                                 MovementValidator::Config validator_config,
                                 mir2::ecs::TeleportSystem* teleport_system,
                                 mir2::game::map::GateManager* gate_manager)
    : BaseHandler(mir2::log::LogCategory::kGame),
      client_registry_(registry),
      character_manager_(character_manager),
      scene_manager_(scene_manager),
      ecs_registry_(nullptr),
      teleport_system_(teleport_system),
      gate_manager_(gate_manager),
      default_map_id_(default_map_id),
      validator_config_(validator_config) {}

MovementHandler::MovementHandler(ClientRegistry& registry,
                                 mir2::ecs::CharacterEntityManager& character_manager,
                                 mir2::game::map::SceneManager& scene_manager,
                                 entt::registry& ecs_registry,
                                 uint32_t default_map_id,
                                 MovementValidator::Config validator_config,
                                 mir2::ecs::TeleportSystem* teleport_system,
                                 mir2::game::map::GateManager* gate_manager)
    : BaseHandler(mir2::log::LogCategory::kGame),
      client_registry_(registry),
      character_manager_(character_manager),
      scene_manager_(scene_manager),
      ecs_registry_(&ecs_registry),
      teleport_system_(teleport_system),
      gate_manager_(gate_manager),
      default_map_id_(default_map_id),
      validator_config_(validator_config) {}

void MovementHandler::DoHandle(const HandlerContext& context,
                               uint16_t msg_id,
                               const std::vector<uint8_t>& payload,
                               ResponseCallback callback) {
    if (msg_id == static_cast<uint16_t>(mir2::common::MsgId::kMoveReq)) {
        HandleMove(context, payload, std::move(callback));
        return;
    }

    OnError(context, msg_id, mir2::common::ErrorCode::kInvalidAction, std::move(callback));
}

void MovementHandler::OnError(const HandlerContext& context,
                              uint16_t msg_id,
                              mir2::common::ErrorCode error_code,
                              ResponseCallback callback) {
    BaseHandler::OnError(context, msg_id, error_code, callback);

    if (msg_id != static_cast<uint16_t>(mir2::common::MsgId::kMoveReq)) {
        if (callback) {
            callback(ResponseList{});
        }
        return;
    }

    ResponseList responses;
    responses.push_back({context.client_id,
                         static_cast<uint16_t>(mir2::common::MsgId::kMoveRsp),
                         BuildMoveRsp(error_code, 0, 0)});
    if (callback) {
        callback(responses);
    }
}

void MovementHandler::HandleMove(const HandlerContext& context,
                                 const std::vector<uint8_t>& payload,
                                 ResponseCallback callback) {
    mir2::common::MoveRequest request;
    const auto status = mir2::common::DecodeMoveRequest(
        mir2::common::kMoveRequestMsgId, payload, &request);
    if (status != mir2::common::MessageCodecStatus::kOk) {
        OnError(context, mir2::common::kMoveRequestMsgId,
                ToCommonError(status), std::move(callback));
        return;
    }

    mir2::common::ErrorCode result = mir2::common::ErrorCode::kOk;
    const int x = request.target_x;
    const int y = request.target_y;
    const uint64_t entity_id = context.client_id;
    uint32_t map_id = default_map_id_;
    const int64_t now_ms = mir2::core::GetCurrentTimestampMs();
    bool should_broadcast = false;
    entt::entity entity = entt::null;
    std::optional<mir2::game::map::GateInfo> triggered_gate;

    {
        std::lock_guard<std::mutex> lock(move_mutex_);
        entity = character_manager_.GetOrCreate(static_cast<uint32_t>(context.client_id));
        entt::registry* registry = ecs_registry_;
        if (!registry) {
            registry = character_manager_.TryGetRegistry(static_cast<uint32_t>(context.client_id));
        }

        if (!registry || entity == entt::null || !registry->valid(entity)) {
            result = mir2::common::ErrorCode::kInvalidAction;
        } else {
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
                    character_manager_.SetPosition(static_cast<uint32_t>(context.client_id),
                                                   x, y, map_id);
                    if (entity != entt::null) {
                        auto* current_map = scene_manager_.GetMapByEntity(entity);
                        if (!current_map ||
                            current_map->GetMapId() != static_cast<int32_t>(map_id)) {
                            scene_manager_.AddEntityToMap(static_cast<int32_t>(map_id),
                                                          entity, x, y);
                        } else {
                            scene_manager_.UpdateEntityPosition(entity, x, y);
                        }
                    }
                    last_move_time_ms_[entity_id] = now_ms;
                    should_broadcast = true;
                    if (gate_manager_) {
                        triggered_gate = gate_manager_->CheckGateTrigger(
                            std::to_string(map_id), x, y);
                    }
                } else {
                    RecordMoveViolation(entity_id, result, now_ms);
                }
            }
        }
    }

    ResponseList responses;
    responses.push_back({context.client_id,
                         static_cast<uint16_t>(mir2::common::MsgId::kMoveRsp),
                         BuildMoveRsp(result, x, y)});

    if (result == mir2::common::ErrorCode::kOk && should_broadcast) {
        const auto broadcast_payload = BuildEntityMove(context.client_id, x, y, 0);
        for (const auto client_id : client_registry_.GetAll()) {
            responses.push_back({client_id,
                                 static_cast<uint16_t>(mir2::common::MsgId::kEntityMove),
                                 broadcast_payload});
        }
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

    if (callback) {
        callback(responses);
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

}  // namespace legend2::handlers
