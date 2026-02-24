/**
 * @file map_context_service.h
 * @brief Map runtime context binding service.
 */

#ifndef MIR2_GAME_MAP_MAP_CONTEXT_SERVICE_H_
#define MIR2_GAME_MAP_MAP_CONTEXT_SERVICE_H_

#include <cstdint>
#include <mutex>
#include <unordered_map>

#include <entt/entt.hpp>

namespace mir2::game::map {

class MapInstance;
class SceneManager;

/**
 * @brief Provides map lookup by map_id and registry binding for runtime systems.
 */
class MapContextService {
 public:
  explicit MapContextService(SceneManager& scene_manager);

  MapContextService(const MapContextService&) = delete;
  MapContextService& operator=(const MapContextService&) = delete;

  MapInstance* GetMap(uint32_t map_id) const;
  MapInstance* GetMap(entt::registry& registry) const;
  void BindRegistry(entt::registry& registry, uint32_t map_id);

 private:
  SceneManager& scene_manager_;
  mutable std::mutex mutex_;
  std::unordered_map<const entt::registry*, uint32_t> registry_map_ids_;
};

}  // namespace mir2::game::map

#endif  // MIR2_GAME_MAP_MAP_CONTEXT_SERVICE_H_
