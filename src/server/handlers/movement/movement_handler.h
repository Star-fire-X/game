/**
 * @file movement_handler.h
 * @brief 移动相关处理
 */

#ifndef LEGEND2_SERVER_HANDLERS_MOVEMENT_HANDLER_H
#define LEGEND2_SERVER_HANDLERS_MOVEMENT_HANDLER_H

#include <cstdint>
#include <mutex>
#include <unordered_map>

#include <entt/entt.hpp>

#include "ecs/systems/teleport_system.h"
#include "ecs/character_entity_manager.h"
#include "handlers/base_handler.h"
#include "handlers/client_registry.h"
#include "handlers/movement/movement_validator.h"
#include "game/map/gate_manager.h"
#include "game/map/scene_manager.h"

namespace legend2::handlers {

/**
 * @brief 移动Handler
 */
class MovementHandler : public BaseHandler {
public:
    MovementHandler(ClientRegistry& registry,
                    mir2::ecs::CharacterEntityManager& character_manager,
                    mir2::game::map::SceneManager& scene_manager,
                    uint32_t default_map_id = 1,
                    MovementValidator::Config validator_config = MovementValidator::Config(),
                    mir2::ecs::TeleportSystem* teleport_system = nullptr,
                    mir2::game::map::GateManager* gate_manager = nullptr);
    MovementHandler(ClientRegistry& registry,
                    mir2::ecs::CharacterEntityManager& character_manager,
                    mir2::game::map::SceneManager& scene_manager,
                    entt::registry& ecs_registry,
                    uint32_t default_map_id = 1,
                    MovementValidator::Config validator_config = MovementValidator::Config(),
                    mir2::ecs::TeleportSystem* teleport_system = nullptr,
                    mir2::game::map::GateManager* gate_manager = nullptr);

protected:
    void DoHandle(const HandlerContext& context,
                  uint16_t msg_id,
                  const std::vector<uint8_t>& payload,
                  ResponseCallback callback) override;

    void OnError(const HandlerContext& context,
                 uint16_t msg_id,
                 mir2::common::ErrorCode error_code,
                 ResponseCallback callback) override;

private:
    void HandleMove(const HandlerContext& context,
                    const std::vector<uint8_t>& payload,
                    ResponseCallback callback);

    void RecordMoveViolation(uint64_t player_id,
                             mir2::common::ErrorCode code,
                             int64_t timestamp_ms);

    ClientRegistry& client_registry_;
    mir2::ecs::CharacterEntityManager& character_manager_;
    mir2::game::map::SceneManager& scene_manager_;
    entt::registry* ecs_registry_ = nullptr;
    mir2::ecs::TeleportSystem* teleport_system_ = nullptr;
    mir2::game::map::GateManager* gate_manager_ = nullptr;
    uint32_t default_map_id_;
    MovementValidator::Config validator_config_;
    std::unordered_map<uint64_t, int64_t> last_move_time_ms_;
    mutable std::mutex move_mutex_;
};

}  // namespace legend2::handlers

#endif  // LEGEND2_SERVER_HANDLERS_MOVEMENT_HANDLER_H
