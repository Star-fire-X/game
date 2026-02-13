/**
 * @file movement_handler.h
 * @brief Movement handler for logic layer.
 */

#ifndef MIR2_LOGIC_HANDLERS_MOVEMENT_MOVEMENT_HANDLER_H_
#define MIR2_LOGIC_HANDLERS_MOVEMENT_MOVEMENT_HANDLER_H_

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <unordered_map>
#include <vector>

#include <entt/entt.hpp>

#include "logic/services/client_registry.h"
#include "logic/handlers/movement/movement_validator.h"
#include "logic/handler_context.h"
#include "logic/task.h"

namespace mir2::common {
struct MoveRequest;
}  // namespace mir2::common

namespace mir2::ecs {
class CharacterEntityManager;
class TeleportSystem;
}  // namespace mir2::ecs

namespace mir2::game::map {
class GateManager;
class MapInstance;
class SceneManager;
}  // namespace mir2::game::map

namespace mir2::logic {

class ResponseSender;
class RoleStore;

class MovementHandler {
 public:
  struct AntiCheatConfig {
    int speed_violation_severity;
    int teleport_violation_severity;

    constexpr AntiCheatConfig(int speed = 10, int teleport = 5)
        : speed_violation_severity(speed),
          teleport_violation_severity(teleport) {}
  };

  MovementHandler(ResponseSender& response_sender,
                  ClientRegistry& registry,
                  mir2::ecs::CharacterEntityManager& character_manager,
                  mir2::game::map::SceneManager& scene_manager,
                  entt::registry& ecs_registry,
                  uint32_t default_map_id = 1,
                  MovementValidator::Config validator_config =
                      MovementValidator::Config(),
                  mir2::ecs::TeleportSystem* teleport_system = nullptr,
                  mir2::game::map::GateManager* gate_manager = nullptr,
                  RoleStore* role_store = nullptr,
                  AntiCheatConfig anti_cheat_config = AntiCheatConfig());

  Task<void> HandleMessage(HandlerContext ctx, const uint8_t* payload, size_t payload_size);
  Task<void> HandleHot(HandlerContext ctx, int32_t target_x, int32_t target_y);

 private:
  Task<void> HandleMessageOwned(HandlerContext ctx, std::vector<uint8_t> payload);
  Task<void> HandleMove(HandlerContext ctx, const mir2::common::MoveRequest& request);
  Task<void> SendMoveResponse(uint64_t client_id,
                              mir2::common::ErrorCode code,
                              int x,
                              int y);
  Task<void> BroadcastEntityMove(uint64_t entity_id,
                                 int x,
                                 int y,
                                 uint8_t direction,
                                 const std::vector<uint64_t>& recipient_client_ids);
  std::vector<uint64_t> ResolveAoiRecipients(const mir2::game::map::MapInstance& map,
                                             entt::entity self,
                                             entt::registry& registry) const;
  void RecordMoveViolation(uint64_t player_id,
                           mir2::common::ErrorCode code,
                           int64_t timestamp_ms);

  ResponseSender& response_sender_;
  ClientRegistry& client_registry_;
  mir2::ecs::CharacterEntityManager& character_manager_;
  mir2::game::map::SceneManager& scene_manager_;
  entt::registry& ecs_registry_;
  mir2::ecs::TeleportSystem* teleport_system_ = nullptr;
  mir2::game::map::GateManager* gate_manager_ = nullptr;
  RoleStore* role_store_ = nullptr;
  AntiCheatConfig anti_cheat_config_;
  uint32_t default_map_id_;
  MovementValidator::Config validator_config_;
  std::unordered_map<uint64_t, int64_t> last_move_time_ms_;
  mutable std::mutex move_mutex_;
};

}  // namespace mir2::logic

#endif  // MIR2_LOGIC_HANDLERS_MOVEMENT_MOVEMENT_HANDLER_H_
