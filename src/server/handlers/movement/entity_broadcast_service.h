/**
 * @file entity_broadcast_service.h
 * @brief Entity enter/leave broadcast service based on AOI events.
 */

#ifndef LEGEND2_SERVER_HANDLERS_MOVEMENT_ENTITY_BROADCAST_SERVICE_H
#define LEGEND2_SERVER_HANDLERS_MOVEMENT_ENTITY_BROADCAST_SERVICE_H

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <entt/entt.hpp>

#include "game/map/aoi_manager.h"

namespace mir2::network {
class NetworkManager;
}

namespace mir2::proto {
enum class EntityType : uint8_t;
}

namespace legend2::handlers {

/**
 * @brief Broadcast AOI enter/leave events to clients.
 */
class EntityBroadcastService {
 public:
  EntityBroadcastService(mir2::network::NetworkManager& network,
                         entt::registry& registry);

  void HandleAOIEvent(mir2::game::map::AOIEventType event_type,
                      entt::entity watcher,
                      entt::entity target,
                      int32_t x,
                      int32_t y);

 private:
  std::optional<uint64_t> ResolveClientId(entt::entity entity) const;
  mir2::proto::EntityType ResolveEntityType(entt::entity entity) const;
  uint64_t ResolveEntityId(entt::entity entity,
                           mir2::proto::EntityType type) const;

  std::vector<uint8_t> BuildEntityEnter(entt::entity entity,
                                        int32_t x,
                                        int32_t y) const;
  std::vector<uint8_t> BuildEntityLeave(entt::entity entity) const;

  void SendToClient(uint64_t client_id,
                    uint16_t msg_id,
                    const std::vector<uint8_t>& payload) const;

  mir2::network::NetworkManager& network_;
  entt::registry& registry_;
};

}  // namespace legend2::handlers

#endif  // LEGEND2_SERVER_HANDLERS_MOVEMENT_ENTITY_BROADCAST_SERVICE_H
