/**
 * @file boss_manager.cc
 * @brief BOSS管理器实现
 */

#include "game/entity/boss_manager.h"

#include "log/logger.h"

namespace mir2::game::entity {

std::shared_ptr<BossBehavior> BossManager::CreateBoss(uint32_t monster_id) {
  if (monster_id == 0) {
    SYSLOG_WARN("Boss monster_id is 0");
    return nullptr;
  }

  std::lock_guard<std::mutex> lock(mutex_);

  BossConfig config{};
  auto it = boss_configs_.find(monster_id);
  if (it != boss_configs_.end()) {
    config = it->second;
  }
  config.monster_id = monster_id;

  uint32_t boss_id = next_boss_id_++;
  auto boss = std::make_shared<BossBehavior>(boss_id, config, event_bus_);
  bosses_[boss_id] = boss;

  SYSLOG_INFO("Boss {} (monster {}) created", boss_id, monster_id);
  return boss;
}

void BossManager::UpdateAll(float delta_time) {
  std::lock_guard<std::mutex> lock(mutex_);
  for (auto& [id, boss] : bosses_) {
    if (!boss || boss->IsDead()) {
      continue;
    }
    boss->Update(delta_time);
  }
}

void BossManager::DestroyBoss(uint32_t boss_id) {
  std::lock_guard<std::mutex> lock(mutex_);

  auto it = bosses_.find(boss_id);
  if (it == bosses_.end()) {
    SYSLOG_WARN("Boss {} not found for removal", boss_id);
    return;
  }

  bosses_.erase(it);
  SYSLOG_INFO("Boss {} removed", boss_id);
}

std::shared_ptr<BossBehavior> BossManager::GetBoss(uint32_t boss_id) const {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = bosses_.find(boss_id);
  if (it != bosses_.end()) {
    return it->second;
  }
  return nullptr;
}

void BossManager::RegisterBossConfig(const BossConfig& config) {
  if (config.monster_id == 0) {
    SYSLOG_WARN("Boss config missing monster_id");
    return;
  }

  std::lock_guard<std::mutex> lock(mutex_);
  boss_configs_[config.monster_id] = config;
}

void BossManager::SetEventBus(ecs::EventBus* event_bus) {
  std::lock_guard<std::mutex> lock(mutex_);
  event_bus_ = event_bus;
  for (auto& [id, boss] : bosses_) {
    if (boss) {
      boss->SetEventBus(event_bus_);
    }
  }
}

}  // namespace mir2::game::entity
