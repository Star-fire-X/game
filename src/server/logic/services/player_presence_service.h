/**
 * @file player_presence_service.h
 * @brief Presence/query facade for online players.
 */

#ifndef MIR2_LOGIC_SERVICES_PLAYER_PRESENCE_SERVICE_H_
#define MIR2_LOGIC_SERVICES_PLAYER_PRESENCE_SERVICE_H_

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <entt/entt.hpp>

namespace mir2::logic {

/**
 * @brief Adapter service for player presence and chat preference queries.
 *
 * ECS-only query facade for online/player preference state.
 */
class PlayerPresenceService {
 public:
  static std::unique_ptr<PlayerPresenceService> CreateDefault(
      entt::registry& registry);

  explicit PlayerPresenceService(entt::registry& registry);

  bool IsOnline(uint64_t player_id) const;
  std::optional<entt::entity> FindEntity(uint64_t player_id) const;
  std::optional<uint64_t> FindPlayerIdByName(const std::string& name) const;
  std::optional<std::string> FindName(uint64_t player_id) const;
  std::optional<uint32_t> FindMapId(uint64_t player_id) const;

  std::vector<uint64_t> GetOnlinePlayerIdsOnMap(uint32_t map_id) const;
  std::vector<uint64_t> GetAllOnlinePlayerIds() const;

  bool CanHearWhisper(uint64_t player_id) const;
  bool CanHearCry(uint64_t player_id) const;
  bool CanHearGuildMessage(uint64_t player_id) const;
  bool IsDead(uint64_t player_id) const;
  bool IsBlocked(uint64_t owner_id, uint64_t target_id) const;

  bool AddBlock(uint64_t owner_id, uint64_t target_id);
  bool RemoveBlock(uint64_t owner_id, uint64_t target_id);
  bool SetHearWhisper(uint64_t player_id, bool enabled);
  bool SetHearCry(uint64_t player_id, bool enabled);
  bool SetHearGuildMessage(uint64_t player_id, bool enabled);

 private:
  entt::registry& registry_;
};

}  // namespace mir2::logic

#endif  // MIR2_LOGIC_SERVICES_PLAYER_PRESENCE_SERVICE_H_
