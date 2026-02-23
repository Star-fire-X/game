#include "client/handlers/movement_handler.h"

#include "common/protocol/message_codec.h"
#include "combat_generated.h"
#include "game_generated.h"

#include <flatbuffers/flatbuffers.h>

namespace mir2::game::handlers {

namespace {
const int kMapMaxX = 2000;  // TODO: load from config.
const int kMapMaxY = 2000;

const char* proto_error_to_string(mir2::proto::ErrorCode code) {
    switch (code) {
        case mir2::proto::ErrorCode::ERR_OK: return "OK";
        case mir2::proto::ErrorCode::ERR_ACCOUNT_NOT_FOUND: return "Account not found";
        case mir2::proto::ErrorCode::ERR_PASSWORD_WRONG: return "Password incorrect";
        case mir2::proto::ErrorCode::ERR_NAME_EXISTS: return "Name already exists";
        case mir2::proto::ErrorCode::ERR_TARGET_DEAD: return "Target dead";
        case mir2::proto::ErrorCode::ERR_SKILL_COOLDOWN: return "Skill cooldown";
        case mir2::proto::ErrorCode::ERR_INVALID_ACTION: return "Invalid action";
        case mir2::proto::ErrorCode::ERR_TARGET_NOT_FOUND: return "Target not found";
        case mir2::proto::ErrorCode::ERR_TARGET_OUT_OF_RANGE: return "Target out of range";
        case mir2::proto::ErrorCode::ERR_INSUFFICIENT_MP: return "Insufficient MP";
        default: return "Unknown error";
    }
}

template <typename Message>
bool ValidatePositionIfPresent(const Message& message,
                               const MovementHandler::Callbacks& callbacks) {
    (void)message;
    if constexpr (requires { message.x(); message.y(); }) {
        if (message.x() < 0 || message.x() > kMapMaxX ||
            message.y() < 0 || message.y() > kMapMaxY) {
            if (callbacks.on_parse_error) {
                callbacks.on_parse_error("Entity position out of bounds");
            }
            return false;
        }
    }
    return true;
}
}

MovementHandler::MovementHandler(Callbacks callbacks)
    : callbacks_(std::move(callbacks)) {}

MovementHandler::~MovementHandler() = default;

void MovementHandler::BindHandlers(mir2::client::INetworkManager& manager) {
    manager.register_handler(mir2::common::MsgId::kMoveRsp,
                             [this](const NetworkPacket& packet) {
                                 HandleMoveResponse(packet);
                             });
    manager.register_handler(mir2::common::MsgId::kEntityMove,
                             [this](const NetworkPacket& packet) {
                                 HandleMoveBroadcast(packet);
                             });
    manager.register_handler(mir2::common::MsgId::kEntityEnter,
                             [this](const NetworkPacket& packet) {
                                 HandleEntitySpawn(packet);
                             });
    manager.register_handler(mir2::common::MsgId::kEntityLeave,
                             [this](const NetworkPacket& packet) {
                                 HandleEntityDespawn(packet);
                             });
    manager.register_handler(mir2::common::MsgId::kEntityUpdate,
                             [this](const NetworkPacket& packet) {
                                 HandleEntityUpdate(packet);
                             });
    manager.register_handler(mir2::common::MsgId::kDeath,
                             [this](const NetworkPacket& packet) {
                                 HandleMonsterDeath(packet);
                             });
    manager.register_handler(mir2::common::MsgId::kChangeMap,
                             [this](const NetworkPacket& packet) {
                                 HandleChangeMap(packet);
                             });
    manager.register_handler(mir2::common::MsgId::kTeleport,
                             [this](const NetworkPacket& packet) {
                                 HandleTeleport(packet);
                             });
    manager.register_handler(mir2::common::MsgId::kRespawn,
                             [this](const NetworkPacket& packet) {
                                 HandleRespawn(packet);
                             });
    manager.register_handler(mir2::common::MsgId::kStateSync,
                             [this](const NetworkPacket& packet) {
                                 HandleStateSync(packet);
                             });
}

void MovementHandler::HandleMoveResponse(const NetworkPacket& packet) {
    if (packet.payload.empty()) {
        if (callbacks_.on_parse_error) {
            callbacks_.on_parse_error("Empty move response payload");
        }
        return;
    }

    mir2::common::MoveResponse response;
    const auto status = mir2::common::DecodeMoveResponse(packet.msg_id, packet.payload, &response);
    if (status != mir2::common::MessageCodecStatus::kOk) {
        if (callbacks_.on_parse_error) {
            callbacks_.on_parse_error("Invalid move response payload");
        }
        return;
    }

    if (response.code != mir2::proto::ErrorCode::ERR_OK) {
        if (callbacks_.on_move_failed) {
            callbacks_.on_move_failed(proto_error_to_string(response.code));
        }
        return;
    }

    if (callbacks_.on_move_response) {
        callbacks_.on_move_response(response.x, response.y);
    }
}

void MovementHandler::HandleMoveBroadcast(const NetworkPacket& packet) {
    if (packet.payload.empty()) {
        if (callbacks_.on_parse_error) {
            callbacks_.on_parse_error("Empty entity move payload");
        }
        return;
    }

    flatbuffers::Verifier verifier(packet.payload.data(), packet.payload.size());
    if (!verifier.VerifyBuffer<mir2::proto::EntityMove>(nullptr)) {
        if (callbacks_.on_parse_error) {
            callbacks_.on_parse_error("Invalid entity move payload");
        }
        return;
    }

    const auto* move = flatbuffers::GetRoot<mir2::proto::EntityMove>(packet.payload.data());
    if (!move) {
        if (callbacks_.on_parse_error) {
            callbacks_.on_parse_error("Entity move parse failed");
        }
        return;
    }

    if (!ValidatePositionIfPresent(*move, callbacks_)) {
        return;
    }

    if (callbacks_.on_entity_move) {
        events::EntityMovedEvent event{
            .entity_id = move->entity_id(),
            .entity_type = move->entity_type(),
            .x = move->x(),
            .y = move->y(),
            .direction = move->direction(),
        };
        callbacks_.on_entity_move(event);
    }

    if (move->entity_type() == mir2::proto::EntityType::MONSTER) {
        if (callbacks_.on_monster_move) {
            callbacks_.on_monster_move(move->entity_id(),
                                       move->x(),
                                       move->y(),
                                       move->direction());
        }
    }
}

void MovementHandler::HandleEntitySpawn(const NetworkPacket& packet) {
    if (packet.payload.empty()) {
        if (callbacks_.on_parse_error) {
            callbacks_.on_parse_error("Empty entity enter payload");
        }
        return;
    }

    flatbuffers::Verifier verifier(packet.payload.data(), packet.payload.size());
    if (!verifier.VerifyBuffer<mir2::proto::EntityEnter>(nullptr)) {
        if (callbacks_.on_parse_error) {
            callbacks_.on_parse_error("Invalid entity enter payload");
        }
        return;
    }

    const auto* enter = flatbuffers::GetRoot<mir2::proto::EntityEnter>(packet.payload.data());
    if (!enter) {
        if (callbacks_.on_parse_error) {
            callbacks_.on_parse_error("Entity enter parse failed");
        }
        return;
    }

    if (!ValidatePositionIfPresent(*enter, callbacks_)) {
        return;
    }

    if (callbacks_.on_entity_enter) {
        events::EntityEnteredEvent event{
            .entity_id = enter->entity_id(),
            .entity_type = enter->entity_type(),
            .x = enter->x(),
            .y = enter->y(),
            .direction = enter->direction(),
            .template_id = enter->template_id(),
            .name = enter->name() ? enter->name()->str() : std::string{},
        };
        callbacks_.on_entity_enter(event);
    }

    if (enter->entity_type() == mir2::proto::EntityType::MONSTER) {
        if (callbacks_.on_monster_enter) {
            callbacks_.on_monster_enter(enter->entity_id(),
                                        enter->template_id(),
                                        enter->x(),
                                        enter->y(),
                                        enter->direction(),
                                        enter->hp(),
                                        enter->max_hp());
        }
    }
}

void MovementHandler::HandleEntityDespawn(const NetworkPacket& packet) {
    if (packet.payload.empty()) {
        if (callbacks_.on_parse_error) {
            callbacks_.on_parse_error("Empty entity leave payload");
        }
        return;
    }

    flatbuffers::Verifier verifier(packet.payload.data(), packet.payload.size());
    if (!verifier.VerifyBuffer<mir2::proto::EntityLeave>(nullptr)) {
        if (callbacks_.on_parse_error) {
            callbacks_.on_parse_error("Invalid entity leave payload");
        }
        return;
    }

    const auto* leave = flatbuffers::GetRoot<mir2::proto::EntityLeave>(packet.payload.data());
    if (!leave) {
        if (callbacks_.on_parse_error) {
            callbacks_.on_parse_error("Entity leave parse failed");
        }
        return;
    }

    if (!ValidatePositionIfPresent(*leave, callbacks_)) {
        return;
    }

    if (callbacks_.on_entity_leave) {
        events::EntityLeftEvent event{
            .entity_id = leave->entity_id(),
        };
        callbacks_.on_entity_leave(event);
    }

    if (leave->entity_type() == mir2::proto::EntityType::MONSTER) {
        if (callbacks_.on_monster_leave) {
            callbacks_.on_monster_leave(leave->entity_id());
        }
    }
}

void MovementHandler::HandleEntityUpdate(const NetworkPacket& packet) {
    if (packet.payload.empty()) {
        if (callbacks_.on_parse_error) {
            callbacks_.on_parse_error("Empty entity update payload");
        }
        return;
    }

    flatbuffers::Verifier verifier(packet.payload.data(), packet.payload.size());
    if (!verifier.VerifyBuffer<mir2::proto::EntityUpdate>(nullptr)) {
        if (callbacks_.on_parse_error) {
            callbacks_.on_parse_error("Invalid entity update payload");
        }
        return;
    }

    const auto* update = flatbuffers::GetRoot<mir2::proto::EntityUpdate>(packet.payload.data());
    if (!update) {
        if (callbacks_.on_parse_error) {
            callbacks_.on_parse_error("Entity update parse failed");
        }
        return;
    }

    if (!ValidatePositionIfPresent(*update, callbacks_)) {
        return;
    }

    if (callbacks_.on_entity_update) {
        events::EntityStatsUpdatedEvent event{
            .entity_id = update->entity_id(),
            .entity_type = update->entity_type(),
            .hp = update->hp(),
            .max_hp = update->max_hp(),
            .mp = update->mp(),
            .max_mp = update->max_mp(),
            .level = update->level(),
        };
        callbacks_.on_entity_update(event);
    }

    if (update->entity_type() == mir2::proto::EntityType::MONSTER) {
        if (callbacks_.on_monster_stats) {
            callbacks_.on_monster_stats(update->entity_id(),
                                        update->hp(),
                                        update->max_hp());
        }
    }
}

void MovementHandler::HandleMonsterDeath(const NetworkPacket& packet) {
    if (packet.payload.empty()) {
        if (callbacks_.on_parse_error) {
            callbacks_.on_parse_error("Empty death payload");
        }
        return;
    }

    flatbuffers::Verifier verifier(packet.payload.data(), packet.payload.size());
    if (!verifier.VerifyBuffer<mir2::proto::Death>(nullptr)) {
        if (callbacks_.on_parse_error) {
            callbacks_.on_parse_error("Invalid death payload");
        }
        return;
    }

    const auto* death = flatbuffers::GetRoot<mir2::proto::Death>(packet.payload.data());
    if (!death) {
        if (callbacks_.on_parse_error) {
            callbacks_.on_parse_error("Death parse failed");
        }
        return;
    }

    if (death->entity_type() != mir2::proto::EntityType::MONSTER) {
        return;
    }

    if (callbacks_.on_monster_death) {
        callbacks_.on_monster_death(death->entity_id(), death->killer_id());
    }
}

void MovementHandler::HandleChangeMap(const NetworkPacket& packet) {
    if (packet.payload.empty()) {
        if (callbacks_.on_parse_error) {
            callbacks_.on_parse_error("Empty change-map payload");
        }
        return;
    }

    flatbuffers::Verifier verifier(packet.payload.data(), packet.payload.size());
    if (!verifier.VerifyBuffer<mir2::proto::ChangeMap>(nullptr)) {
        if (callbacks_.on_parse_error) {
            callbacks_.on_parse_error("Invalid change-map payload");
        }
        return;
    }

    const auto* change_map =
        flatbuffers::GetRoot<mir2::proto::ChangeMap>(packet.payload.data());
    if (!change_map) {
        if (callbacks_.on_parse_error) {
            callbacks_.on_parse_error("Change-map parse failed");
        }
        return;
    }

    if (!ValidatePositionIfPresent(*change_map, callbacks_)) {
        return;
    }

    if (callbacks_.on_change_map) {
        callbacks_.on_change_map(change_map->map_id(), change_map->x(), change_map->y());
    }
}

void MovementHandler::HandleTeleport(const NetworkPacket& packet) {
    if (packet.payload.empty()) {
        if (callbacks_.on_parse_error) {
            callbacks_.on_parse_error("Empty teleport payload");
        }
        return;
    }

    flatbuffers::Verifier verifier(packet.payload.data(), packet.payload.size());
    if (!verifier.VerifyBuffer<mir2::proto::Teleport>(nullptr)) {
        if (callbacks_.on_parse_error) {
            callbacks_.on_parse_error("Invalid teleport payload");
        }
        return;
    }

    const auto* teleport =
        flatbuffers::GetRoot<mir2::proto::Teleport>(packet.payload.data());
    if (!teleport) {
        if (callbacks_.on_parse_error) {
            callbacks_.on_parse_error("Teleport parse failed");
        }
        return;
    }

    if (!ValidatePositionIfPresent(*teleport, callbacks_)) {
        return;
    }

    if (callbacks_.on_teleport) {
        callbacks_.on_teleport(teleport->map_id(), teleport->x(), teleport->y());
    }
}

void MovementHandler::HandleRespawn(const NetworkPacket& packet) {
    if (packet.payload.empty()) {
        if (callbacks_.on_parse_error) {
            callbacks_.on_parse_error("Empty respawn payload");
        }
        return;
    }

    flatbuffers::Verifier verifier(packet.payload.data(), packet.payload.size());
    if (!verifier.VerifyBuffer<mir2::proto::Respawn>(nullptr)) {
        if (callbacks_.on_parse_error) {
            callbacks_.on_parse_error("Invalid respawn payload");
        }
        return;
    }

    const auto* respawn = flatbuffers::GetRoot<mir2::proto::Respawn>(packet.payload.data());
    if (!respawn) {
        if (callbacks_.on_parse_error) {
            callbacks_.on_parse_error("Respawn parse failed");
        }
        return;
    }

    if (!ValidatePositionIfPresent(*respawn, callbacks_)) {
        return;
    }

    if (callbacks_.on_respawn) {
        callbacks_.on_respawn(events::RespawnEvent{
            .entity_id = respawn->entity_id(),
            .entity_type = respawn->entity_type(),
            .x = respawn->x(),
            .y = respawn->y(),
            .hp = respawn->hp(),
            .mp = respawn->mp(),
        });
    }
}

void MovementHandler::HandleStateSync(const NetworkPacket& packet) {
    if (packet.payload.empty()) {
        if (callbacks_.on_parse_error) {
            callbacks_.on_parse_error("Empty state-sync payload");
        }
        return;
    }

    flatbuffers::Verifier verifier(packet.payload.data(), packet.payload.size());
    if (!verifier.VerifyBuffer<mir2::proto::StateSync>(nullptr)) {
        if (callbacks_.on_parse_error) {
            callbacks_.on_parse_error("Invalid state-sync payload");
        }
        return;
    }

    const auto* sync = flatbuffers::GetRoot<mir2::proto::StateSync>(packet.payload.data());
    if (!sync || !sync->player()) {
        if (callbacks_.on_parse_error) {
            callbacks_.on_parse_error("State-sync parse failed");
        }
        return;
    }

    const auto* player = sync->player();
    if (!ValidatePositionIfPresent(*player, callbacks_)) {
        return;
    }

    events::StateSyncEvent event{};
    event.player_id = player->id();
    event.map_id = player->map_id();
    event.x = player->x();
    event.y = player->y();
    event.hp = player->hp();
    event.max_hp = player->max_hp();
    event.mp = player->mp();
    event.max_mp = player->max_mp();
    event.level = player->level();

    if (const auto* entities = sync->entities()) {
        event.entities.reserve(entities->size());
        for (const auto* snapshot : *entities) {
            if (!snapshot) {
                continue;
            }
            if (!ValidatePositionIfPresent(*snapshot, callbacks_)) {
                return;
            }
            event.entities.push_back(events::StateSyncEntitySnapshot{
                .entity_id = snapshot->entity_id(),
                .entity_type = snapshot->entity_type(),
                .x = snapshot->x(),
                .y = snapshot->y(),
                .direction = snapshot->direction(),
                .hp = snapshot->hp(),
                .max_hp = snapshot->max_hp(),
                .mp = snapshot->mp(),
                .max_mp = snapshot->max_mp(),
            });
        }
    }

    if (callbacks_.on_state_sync) {
        callbacks_.on_state_sync(event);
    }
}

} // namespace mir2::game::handlers
