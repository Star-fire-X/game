/**
 * @file monster_manager.cc
 * @brief 怪物管理器实现
 */

#include "game/entity/monster_manager.h"

#include <algorithm>

#include "log/logger.h"

namespace mir2::game::entity {

std::shared_ptr<Monster> MonsterManager::SpawnMonster(uint32_t template_id,
                                                       uint32_t map_id,
                                                       int32_t x, int32_t y,
                                                       entt::registry& registry) {
  if (template_id == 0) {
    SYSLOG_WARN("Monster template_id is 0");
    return nullptr;
  }

  std::lock_guard<std::mutex> lock(mutex_);

  // 创建 EnTT 实体
  entt::entity entity = registry.create();

  // 分配怪物ID
  uint64_t monster_id = next_monster_id_++;

  // 创建怪物对象
  // 绑定 registry 以访问 ECS AI 状态
  auto monster = std::make_shared<Monster>(entity, monster_id, &registry);

  // 设置怪物数据
  MonsterData& data = monster->Data();
  data.template_id = template_id;
  data.name = "Monster_" + std::to_string(template_id);
  data.map_id = map_id;
  data.x = x;
  data.y = y;
  data.spawn_x = x;
  data.spawn_y = y;
  // AI状态由 ECS 组件维护，默认使用组件初始值

  // TODO: 从配置表加载怪物模板数据
  // LoadMonsterTemplate(template_id, data);

  // 存储到映射表
  monsters_[monster_id] = monster;
  monsters_by_map_[map_id].push_back(monster_id);

  SYSLOG_INFO("Monster {} (template {}) spawned at map {} ({}, {})",
              monster_id, template_id, map_id, x, y);

  return monster;
}

void MonsterManager::RemoveMonster(uint64_t id, entt::registry& registry) {
  std::lock_guard<std::mutex> lock(mutex_);

  auto it = monsters_.find(id);
  if (it == monsters_.end()) {
    SYSLOG_WARN("Monster {} not found for removal", id);
    return;
  }

  auto& monster = it->second;
  uint32_t map_id = monster->GetMapId();

  // 销毁 EnTT 实体
  entt::entity entity = monster->GetEntity();
  if (registry.valid(entity)) {
    registry.destroy(entity);
  }

  // 从地图映射中移除
  auto map_it = monsters_by_map_.find(map_id);
  if (map_it != monsters_by_map_.end()) {
    auto& ids = map_it->second;
    ids.erase(std::remove(ids.begin(), ids.end(), id), ids.end());
    if (ids.empty()) {
      monsters_by_map_.erase(map_it);
    }
  }

  // 移除怪物
  monsters_.erase(it);

  SYSLOG_INFO("Monster {} removed", id);
}

std::shared_ptr<Monster> MonsterManager::GetMonster(uint64_t id) const {
  std::lock_guard<std::mutex> lock(mutex_);

  auto it = monsters_.find(id);
  if (it != monsters_.end()) {
    return it->second;
  }
  return nullptr;
}

std::vector<std::shared_ptr<Monster>> MonsterManager::GetMonstersOnMap(
    uint32_t map_id) const {
  std::lock_guard<std::mutex> lock(mutex_);

  std::vector<std::shared_ptr<Monster>> result;

  auto map_it = monsters_by_map_.find(map_id);
  if (map_it == monsters_by_map_.end()) {
    return result;
  }

  result.reserve(map_it->second.size());
  for (uint64_t id : map_it->second) {
    auto it = monsters_.find(id);
    if (it != monsters_.end()) {
      result.push_back(it->second);
    }
  }

  return result;
}

size_t MonsterManager::TotalCount() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return monsters_.size();
}

size_t MonsterManager::CountOnMap(uint32_t map_id) const {
  std::lock_guard<std::mutex> lock(mutex_);

  auto it = monsters_by_map_.find(map_id);
  if (it != monsters_by_map_.end()) {
    return it->second.size();
  }
  return 0;
}

void MonsterManager::Update(float delta_time) {
  std::lock_guard<std::mutex> lock(mutex_);

  for (auto& [id, monster] : monsters_) {
    if (!monster || monster->IsDead()) {
      continue;
    }
    // TODO: 实现怪物AI更新逻辑
    (void)delta_time;
  }
}

void MonsterManager::Clear(entt::registry& registry) {
  std::lock_guard<std::mutex> lock(mutex_);

  // 销毁所有 EnTT 实体
  for (const auto& [id, monster] : monsters_) {
    entt::entity entity = monster->GetEntity();
    if (registry.valid(entity)) {
      registry.destroy(entity);
    }
  }

  monsters_.clear();
  monsters_by_map_.clear();
  next_monster_id_ = 1;

  SYSLOG_INFO("All monsters cleared");
}

}  // namespace mir2::game::entity
