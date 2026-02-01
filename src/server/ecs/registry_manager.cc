#include "ecs/registry_manager.h"

#include "config/config_manager.h"
#include "game/event/timed_event_scheduler.h"
#include "log/logger.h"

namespace mir2::ecs {

RegistryManager::RegistryManager()
    : character_manager_(*this) {}

RegistryManager& RegistryManager::Instance() {
  static RegistryManager instance;
  return instance;
}

World* RegistryManager::GetWorld(uint32_t map_id) {
  auto it = worlds_.find(map_id);
  if (it == worlds_.end()) {
    return nullptr;
  }
  return it->second.get();
}

World* RegistryManager::CreateWorld(uint32_t map_id, std::size_t reserve_capacity) {
  auto it = worlds_.find(map_id);
  if (it != worlds_.end()) {
    return it->second.get();
  }

  if (reserve_capacity == 0) {
    reserve_capacity = config::ConfigManager::Instance()
                           .GetEcsConfig()
                           .world_registry_reserve;
  }

  auto world = std::make_unique<World>(reserve_capacity);
  World* ptr = world.get();
  worlds_.emplace(map_id, std::move(world));
  SYSLOG_INFO("RegistryManager: World created map_id={} reserve_capacity={}", map_id,
              reserve_capacity);
  return ptr;
}

void RegistryManager::UpdateAll(float delta_time) {
  for (auto& [map_id, world] : worlds_) {
    if (!world) {
      SYSLOG_WARN("RegistryManager: null World for map_id={}", map_id);
      continue;
    }
    world->Update(delta_time);
  }
  legend2::game::event::TimedEventScheduler::Instance().Update(delta_time);
}

CharacterEntityManager& RegistryManager::GetCharacterManager() {
  return character_manager_;
}

const CharacterEntityManager& RegistryManager::GetCharacterManager() const {
  return character_manager_;
}

}  // namespace mir2::ecs
