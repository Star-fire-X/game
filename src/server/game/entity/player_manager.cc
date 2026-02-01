/**
 * @file player_manager.cc
 * @brief 玩家管理器实现
 */

#include "game/entity/player_manager.h"

#include "log/logger.h"

namespace mir2::game::entity {

std::shared_ptr<Player> PlayerManager::CreatePlayer(uint64_t id,
                                                     entt::registry& registry) {
  std::lock_guard<std::mutex> lock(mutex_);

  // 检查是否已存在
  if (players_.find(id) != players_.end()) {
    SYSLOG_WARN("Player {} already exists", id);
    return nullptr;
  }

  // 创建 EnTT 实体
  entt::entity entity = registry.create();

  // 创建玩家对象
  auto player = std::make_shared<Player>(entity, id);
  players_[id] = player;

  SYSLOG_INFO("Player {} created, online count: {}", id, players_.size());
  return player;
}

void PlayerManager::RemovePlayer(uint64_t id, entt::registry& registry) {
  std::lock_guard<std::mutex> lock(mutex_);

  auto it = players_.find(id);
  if (it == players_.end()) {
    SYSLOG_WARN("Player {} not found for removal", id);
    return;
  }

  // 销毁 EnTT 实体
  entt::entity entity = it->second->GetEntity();
  if (registry.valid(entity)) {
    registry.destroy(entity);
  }

  // 移除玩家
  players_.erase(it);

  SYSLOG_INFO("Player {} removed, online count: {}", id, players_.size());
}

std::shared_ptr<Player> PlayerManager::GetPlayer(uint64_t id) const {
  std::lock_guard<std::mutex> lock(mutex_);

  auto it = players_.find(id);
  if (it != players_.end()) {
    return it->second;
  }
  return nullptr;
}

bool PlayerManager::IsOnline(uint64_t id) const {
  std::lock_guard<std::mutex> lock(mutex_);
  return players_.find(id) != players_.end();
}

size_t PlayerManager::OnlineCount() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return players_.size();
}

std::vector<std::shared_ptr<Player>> PlayerManager::GetPlayersOnMap(
    uint32_t map_id) const {
  std::lock_guard<std::mutex> lock(mutex_);

  std::vector<std::shared_ptr<Player>> result;
  for (const auto& [id, player] : players_) {
    if (player->GetMapId() == map_id) {
      result.push_back(player);
    }
  }
  return result;
}

void PlayerManager::Clear(entt::registry& registry) {
  std::lock_guard<std::mutex> lock(mutex_);

  // 销毁所有 EnTT 实体
  for (const auto& [id, player] : players_) {
    entt::entity entity = player->GetEntity();
    if (registry.valid(entity)) {
      registry.destroy(entity);
    }
  }

  players_.clear();
  SYSLOG_INFO("All players cleared");
}

}  // namespace mir2::game::entity
