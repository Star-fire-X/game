/**
 * @file monster.cc
 * @brief 怪物实体类实现
 */

#include "game/entity/monster.h"

#include <algorithm>

#include "ecs/components/monster_component.h"
#include "ecs/registry_manager.h"

namespace mir2::game::entity {

namespace {

entt::registry* ResolveRegistry(entt::registry* registry, uint32_t map_id) {
  if (registry) {
    return registry;
  }
  // 兜底从全局 RegistryManager 获取（兼容未显式绑定注册表的场景）
  if (auto* world = ecs::RegistryManager::Instance().GetWorld(map_id)) {
    return &world->Registry();
  }
  return nullptr;
}

}  // namespace

Monster::Monster(entt::entity entity, uint64_t id, entt::registry* registry)
    : entity_(entity), registry_(registry), data_{} {
  data_.id = id;
}

void Monster::SetPosition(int32_t x, int32_t y) {
  data_.x = x;
  data_.y = y;
}

void Monster::SetState(MonsterState state) {
  auto* registry = ResolveRegistry(registry_, data_.map_id);
  if (!registry || !registry->valid(entity_)) {
    return;
  }
  // AI状态由 ECS 组件维护
  auto& ai = registry->get_or_emplace<ecs::MonsterAIComponent>(entity_);
  ai.current_state = state;
}

void Monster::SetTarget(uint64_t target_id) {
  auto* registry = ResolveRegistry(registry_, data_.map_id);
  if (!registry || !registry->valid(entity_)) {
    return;
  }
  // 目标由 ECS 组件维护（target_id 视为 entt::entity 值）
  entt::entity target = entt::null;
  if (target_id != 0) {
    target = static_cast<entt::entity>(target_id);
    if (!registry->valid(target)) {
      target = entt::null;
    }
  }
  auto& ai = registry->get_or_emplace<ecs::MonsterAIComponent>(entity_);
  ai.target_entity = target;
}

MonsterState Monster::GetState() const {
  auto* registry = ResolveRegistry(registry_, data_.map_id);
  if (!registry || !registry->valid(entity_)) {
    return MonsterState::kIdle;
  }
  // AI状态从 ECS 组件读取
  if (auto* ai = registry->try_get<ecs::MonsterAIComponent>(entity_)) {
    return ai->current_state;
  }
  return MonsterState::kIdle;
}

int32_t Monster::TakeDamage(int32_t damage) {
  if (damage <= 0 || IsDead()) {
    return 0;
  }

  int32_t actual_damage = std::min(damage, data_.stats.hp);
  data_.stats.hp -= actual_damage;

  if (data_.stats.hp <= 0) {
    Die();
  }

  return actual_damage;
}

void Monster::Die() {
  data_.stats.hp = 0;
  // 死亡状态与目标清理由 ECS 组件维护
  SetState(MonsterState::kDead);
  SetTarget(0);
}

void Monster::Respawn() {
  // 恢复满血
  data_.stats.hp = data_.stats.max_hp;
  // 回到出生点
  data_.x = data_.spawn_x;
  data_.y = data_.spawn_y;
  // 重置状态
  SetState(MonsterState::kIdle);
  SetTarget(0);
}

void Monster::ReturnToSpawn() {
  // 返回状态与目标清理由 ECS 组件维护
  SetState(MonsterState::kReturn);
  SetTarget(0);
}

}  // namespace mir2::game::entity
