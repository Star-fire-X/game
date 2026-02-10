/**
 * @file effect_broadcast_service.h
 * @brief Effect broadcast service.
 */

#ifndef MIR2_LOGIC_HANDLERS_EFFECT_EFFECT_BROADCAST_SERVICE_H_
#define MIR2_LOGIC_HANDLERS_EFFECT_EFFECT_BROADCAST_SERVICE_H_

#include <cstdint>
#include <string>

#include <entt/entt.hpp>

namespace mir2::network {
class NetworkManager;
}  // namespace mir2::network

namespace mir2::game::map {
class AOIManager;
}  // namespace mir2::game::map

namespace mir2::logic {

/**
 * @brief Service for broadcasting skill effects.
 */
class EffectBroadcastService {
 public:
  EffectBroadcastService(mir2::network::NetworkManager& network,
                         mir2::game::map::AOIManager& aoi_manager,
                         entt::registry& registry);

  void BroadcastSkillEffect(uint64_t caster_id,
                            uint64_t target_id,
                            uint32_t skill_id,
                            uint8_t effect_type,
                            const std::string& effect_id,
                            const std::string& sound_id,
                            int x,
                            int y,
                            uint32_t duration_ms);

 private:
  mir2::network::NetworkManager& network_;
  mir2::game::map::AOIManager& aoi_manager_;
  entt::registry& registry_;
};

}  // namespace mir2::logic

#endif  // MIR2_LOGIC_HANDLERS_EFFECT_EFFECT_BROADCAST_SERVICE_H_
