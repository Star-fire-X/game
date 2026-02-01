/**
 * @file legacy_monster_adapter.cc
 * @brief Bridge legacy monster APIs to ECS systems.
 */

#include "legacy/legacy_monster_adapter.h"

#include <algorithm>
#include <vector>

#include "ecs/components/character_components.h"
#include "ecs/components/combat_component.h"
#include "ecs/components/monster_component.h"
#include "ecs/components/transform_component.h"
#include "ecs/registry_manager.h"
#include "ecs/systems/combat_system.h"
#include "game/entity/monster.h"

namespace legend2 {

namespace {

mir2::game::entity::MonsterState ToEcsState(mir2::common::MonsterState state) {
  return static_cast<mir2::game::entity::MonsterState>(
      static_cast<uint8_t>(state));
}

mir2::common::MonsterState ToLegacyState(mir2::game::entity::MonsterState state) {
  return static_cast<mir2::common::MonsterState>(
      static_cast<uint8_t>(state));
}

bool IsWithinRange(const mir2::common::Position& a,
                   const mir2::common::Position& b,
                   int range) {
  const int clamped = std::max(0, range);
  const int dx = a.x - b.x;
  const int dy = a.y - b.y;
  return (dx * dx + dy * dy) <= (clamped * clamped);
}

}  // namespace

LegacyMonsterAdapter::LegacyMonsterAdapter(entt::registry& registry,
                                           mir2::ecs::EventBus& event_bus)
    : registry_(registry),
      event_bus_(event_bus),
      monster_ai_system_(registry, event_bus),
      monster_spawn_system_(registry, event_bus) {}

MonsterAI* LegacyMonsterAdapter::add_monster(Monster monster, uint32_t spawn_id) {
  uint32_t id = monster.id;
  if (id == 0) {
    id = next_monster_id_++;
    monster.id = id;
  }

  if (auto it = monster_entities_.find(id); it != monster_entities_.end()) {
    if (registry_.valid(it->second)) {
      registry_.destroy(it->second);
    }
    monster_entities_.erase(it);
    monster_views_.erase(id);
  }

  const entt::entity entity = registry_.create();

  auto& identity = registry_.emplace<mir2::ecs::MonsterIdentityComponent>(entity);
  identity.monster_template_id = monster.template_id;
  identity.spawn_point_id = spawn_id;

  auto& attributes = registry_.emplace<mir2::ecs::CharacterAttributesComponent>(entity);
  attributes.level = monster.stats.level;
  attributes.experience = monster.stats.experience;
  attributes.hp = monster.stats.hp;
  attributes.max_hp = monster.stats.max_hp;
  attributes.mp = monster.stats.mp;
  attributes.max_mp = monster.stats.max_mp;
  attributes.attack = monster.stats.attack;
  attributes.defense = monster.stats.defense;
  attributes.magic_attack = monster.stats.magic_attack;
  attributes.magic_defense = monster.stats.magic_defense;
  attributes.speed = monster.stats.speed;
  attributes.gold = monster.stats.gold;

  auto& state = registry_.emplace<mir2::ecs::CharacterStateComponent>(entity);
  state.map_id = monster.map_id;
  state.position = monster.position;

  auto& ai = registry_.emplace<mir2::ecs::MonsterAIComponent>(entity);
  ai.current_state = ToEcsState(monster.state);
  ai.return_position = monster.spawn_position;

  auto& aggro = registry_.emplace<mir2::ecs::MonsterAggroComponent>(entity);
  aggro.aggro_range = monster.aggro_range;
  aggro.attack_range = monster.attack_range;

  auto& combat = registry_.emplace<mir2::ecs::CombatComponent>(entity);
  combat.attack_range = monster.attack_range;

  monster_entities_[id] = entity;
  monster_views_[id] = std::move(monster);

  return nullptr;
}

bool LegacyMonsterAdapter::remove_monster(uint32_t monster_id) {
  auto it = monster_entities_.find(monster_id);
  if (it == monster_entities_.end()) {
    return false;
  }

  if (registry_.valid(it->second)) {
    registry_.destroy(it->second);
  }

  monster_entities_.erase(it);
  monster_views_.erase(monster_id);
  return true;
}

Monster* LegacyMonsterAdapter::get_monster(uint32_t monster_id) {
  auto it = monster_entities_.find(monster_id);
  if (it == monster_entities_.end()) {
    return nullptr;
  }

  entt::entity entity = it->second;
  if (!registry_.valid(entity)) {
    monster_entities_.erase(it);
    monster_views_.erase(monster_id);
    return nullptr;
  }

  auto view_it = monster_views_.find(monster_id);
  if (view_it == monster_views_.end()) {
    Monster monster;
    monster.id = monster_id;
    view_it = monster_views_.emplace(monster_id, std::move(monster)).first;
  }

  SyncMonsterFromRegistry(entity, view_it->second);
  return &view_it->second;
}

void LegacyMonsterAdapter::on_monster_death(uint32_t monster_id) {
  auto it = monster_entities_.find(monster_id);
  if (it == monster_entities_.end()) {
    return;
  }

  entt::entity entity = it->second;
  if (!registry_.valid(entity)) {
    return;
  }

  mir2::ecs::CombatSystem::Die(registry_, entity, &event_bus_);

  if (auto* ai = registry_.try_get<mir2::ecs::MonsterAIComponent>(entity)) {
    ai->current_state = mir2::game::entity::MonsterState::kDead;
    ai->target_entity = entt::null;
    ai->state_timer = 0.0f;
  }

  if (auto* aggro = registry_.try_get<mir2::ecs::MonsterAggroComponent>(entity)) {
    aggro->Clear();
  }

  if (auto view_it = monster_views_.find(monster_id); view_it != monster_views_.end()) {
    view_it->second.stats.hp = 0;
    view_it->second.state = mir2::common::MonsterState::DEAD;
  }
}

void LegacyMonsterAdapter::notify_player_presence(Character& player) {
  entt::entity player_entity = ResolvePlayerEntity(player);
  if (player_entity == entt::null) {
    return;
  }

  mir2::common::Position player_pos = player.get_position();
  uint32_t player_map_id = player.get_map_id();

  if (auto* player_state = registry_.try_get<mir2::ecs::CharacterStateComponent>(player_entity)) {
    player_pos = player_state->position;
    player_map_id = player_state->map_id;
  }

  std::vector<uint32_t> to_remove;
  for (const auto& [monster_id, entity] : monster_entities_) {
    if (!registry_.valid(entity)) {
      to_remove.push_back(monster_id);
      continue;
    }

    auto* state = registry_.try_get<mir2::ecs::CharacterStateComponent>(entity);
    auto* aggro = registry_.try_get<mir2::ecs::MonsterAggroComponent>(entity);
    auto* attributes = registry_.try_get<mir2::ecs::CharacterAttributesComponent>(entity);
    if (!state || !aggro || !attributes) {
      continue;
    }

    if (attributes->hp <= 0) {
      continue;
    }

    if (state->map_id != player_map_id) {
      continue;
    }

    if (!IsWithinRange(state->position, player_pos, aggro->aggro_range)) {
      continue;
    }

    aggro->AddHatred(player_entity, 1);
  }

  for (uint32_t monster_id : to_remove) {
    monster_entities_.erase(monster_id);
    monster_views_.erase(monster_id);
  }
}

void LegacyMonsterAdapter::update(float delta_time) {
  monster_spawn_system_.Update(registry_, delta_time);
  monster_ai_system_.Update(registry_, delta_time);
}

void LegacyMonsterAdapter::SyncMonsterFromRegistry(entt::entity entity,
                                                   Monster& monster) {
  if (auto* identity = registry_.try_get<mir2::ecs::MonsterIdentityComponent>(entity)) {
    monster.template_id = identity->monster_template_id;
  }

  if (auto* state = registry_.try_get<mir2::ecs::CharacterStateComponent>(entity)) {
    monster.map_id = state->map_id;
    monster.position = state->position;
  }

  if (auto* ai = registry_.try_get<mir2::ecs::MonsterAIComponent>(entity)) {
    monster.state = ToLegacyState(ai->current_state);
    monster.spawn_position = ai->return_position;
  }

  if (auto* aggro = registry_.try_get<mir2::ecs::MonsterAggroComponent>(entity)) {
    monster.aggro_range = aggro->aggro_range;
    monster.attack_range = aggro->attack_range;
  }

  if (auto* combat = registry_.try_get<mir2::ecs::CombatComponent>(entity)) {
    if (combat->attack_range > 0) {
      monster.attack_range = combat->attack_range;
    }
  }

  if (auto* attributes = registry_.try_get<mir2::ecs::CharacterAttributesComponent>(entity)) {
    monster.stats.level = attributes->level;
    monster.stats.experience = attributes->experience;
    monster.stats.hp = attributes->hp;
    monster.stats.max_hp = attributes->max_hp;
    monster.stats.mp = attributes->mp;
    monster.stats.max_mp = attributes->max_mp;
    monster.stats.attack = attributes->attack;
    monster.stats.defense = attributes->defense;
    monster.stats.magic_attack = attributes->magic_attack;
    monster.stats.magic_defense = attributes->magic_defense;
    monster.stats.speed = attributes->speed;
    monster.stats.gold = attributes->gold;
  }
}

entt::entity LegacyMonsterAdapter::ResolvePlayerEntity(const Character& player) {
  auto& character_manager = mir2::ecs::RegistryManager::Instance().GetCharacterManager();
  auto entity_opt = character_manager.TryGet(player.get_id());
  if (entity_opt.has_value()) {
    if (character_manager.TryGetRegistry(player.get_id()) == &registry_) {
      return *entity_opt;
    }
  }

  auto view = registry_.view<mir2::ecs::CharacterIdentityComponent>();
  for (auto entity : view) {
    if (view.get<mir2::ecs::CharacterIdentityComponent>(entity).id == player.get_id()) {
      return entity;
    }
  }

  return entt::null;
}

}  // namespace legend2
