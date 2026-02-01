/**
 * @file legacy_monster_adapter.h
 * @brief Bridge legacy monster APIs to ECS systems.
 */

#ifndef LEGEND2_SERVER_LEGACY_MONSTER_ADAPTER_H_
#define LEGEND2_SERVER_LEGACY_MONSTER_ADAPTER_H_

#include <cstdint>
#include <unordered_map>

#include <entt/entt.hpp>

#include "ecs/event_bus.h"
#include "ecs/systems/monster_ai_system.h"
#include "ecs/systems/monster_spawn_system.h"
#include "legacy/character.h"
#include "legacy/monster.h"

namespace legend2 {

class MonsterAI;

// Adapter layer for legacy MonsterManager-style usage.
class LegacyMonsterAdapter {
 public:
  LegacyMonsterAdapter(entt::registry& registry, mir2::ecs::EventBus& event_bus);

  LegacyMonsterAdapter(const LegacyMonsterAdapter&) = delete;
  LegacyMonsterAdapter& operator=(const LegacyMonsterAdapter&) = delete;

  // Legacy-compatible surface. The returned MonsterAI pointer is always nullptr.
  MonsterAI* add_monster(Monster monster, uint32_t spawn_id = 0);
  bool remove_monster(uint32_t monster_id);
  Monster* get_monster(uint32_t monster_id);
  void on_monster_death(uint32_t monster_id);
  void notify_player_presence(Character& player);
  void update(float delta_time);

 private:
  void SyncMonsterFromRegistry(entt::entity entity, Monster& monster);
  entt::entity ResolvePlayerEntity(const Character& player);

  entt::registry& registry_;
  mir2::ecs::EventBus& event_bus_;
  mir2::ecs::MonsterAISystem monster_ai_system_;
  mir2::ecs::MonsterSpawnSystem monster_spawn_system_;
  std::unordered_map<uint32_t, entt::entity> monster_entities_;
  std::unordered_map<uint32_t, Monster> monster_views_;
  uint32_t next_monster_id_ = 1;
};

}  // namespace legend2

#endif  // LEGEND2_SERVER_LEGACY_MONSTER_ADAPTER_H_
