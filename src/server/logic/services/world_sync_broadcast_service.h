/**
 * @file world_sync_broadcast_service.h
 * @brief Broadcasts map-change/teleport world events to clients.
 */

#ifndef MIR2_LOGIC_SERVICES_WORLD_SYNC_BROADCAST_SERVICE_H_
#define MIR2_LOGIC_SERVICES_WORLD_SYNC_BROADCAST_SERVICE_H_

#include <cstdint>
#include <unordered_map>
#include <vector>

#include <entt/entt.hpp>

#include "logic/response_sender.h"

namespace mir2::ecs {
class EventBus;
}  // namespace mir2::ecs

namespace mir2::game::map {
class SceneManager;
}  // namespace mir2::game::map

namespace mir2::logic {

class RoleStore;

/**
 * @brief Bridges ECS map relocation events to client protocol messages.
 */
class WorldSyncBroadcastService {
 public:
  struct Config {
    int64_t respawn_delay_ms = 3000;
    int64_t state_sync_interval_ms = 1000;
  };

  WorldSyncBroadcastService(ResponseSender& response_sender,
                            mir2::ecs::EventBus& event_bus,
                            RoleStore& role_store,
                            mir2::game::map::SceneManager* scene_manager = nullptr);

  WorldSyncBroadcastService(ResponseSender& response_sender,
                            mir2::ecs::EventBus& event_bus,
                            RoleStore& role_store,
                            mir2::game::map::SceneManager* scene_manager,
                            Config config);

  void Tick(int64_t now_ms);

  // Sends a one-shot StateSync immediately for a specific player role id.
  bool RequestImmediateStateSyncForRole(uint64_t role_id);

 private:
  struct PendingRespawn {
    entt::entity entity = entt::null;
    int64_t due_ms = 0;
  };

  std::vector<uint64_t> ResolveAoiRecipients(entt::entity entity,
                                             bool include_self) const;
  void BroadcastStateSyncForEntity(entt::entity self_entity);
  void ProcessPendingRespawns(int64_t now_ms);

  ResponseSender& response_sender_;
  mir2::ecs::EventBus& event_bus_;
  RoleStore& role_store_;
  mir2::game::map::SceneManager* scene_manager_ = nullptr;
  Config config_;
  int64_t next_state_sync_ms_ = 0;
  std::unordered_map<uint64_t, PendingRespawn> pending_respawns_;
};

}  // namespace mir2::logic

#endif  // MIR2_LOGIC_SERVICES_WORLD_SYNC_BROADCAST_SERVICE_H_
