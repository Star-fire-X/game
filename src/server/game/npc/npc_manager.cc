/**
 * @file npc_manager.cc
 * @brief NPC manager implementation.
 */

#include "game/npc/npc_manager.h"

#include <algorithm>
#include <mutex>
#include <shared_mutex>

#include "log/logger.h"

namespace mir2::game::npc {

std::shared_ptr<NpcEntity> NpcManager::CreateNpc(const NpcConfig& config) {
  std::unique_lock<std::shared_mutex> lock(mutex_);

  uint64_t npc_id = config.id != 0 ? config.id : next_npc_id_++;
  if (npcs_.find(npc_id) != npcs_.end()) {
    SYSLOG_WARN("NpcManager: npc {} already exists", npc_id);
    return nullptr;
  }

  auto npc = std::make_shared<NpcEntity>(npc_id);
  npc->ApplyConfig(config);
  npc->Data().id = npc_id;

  npcs_[npc_id] = npc;
  npcs_by_map_[npc->GetMapId()].push_back(npc_id);

  if (npc_id >= next_npc_id_) {
    next_npc_id_ = npc_id + 1;
  }

  SYSLOG_INFO("NpcManager: npc {} created on map {} ({}, {})",
              npc_id, npc->GetMapId(), npc->GetX(), npc->GetY());

  return npc;
}

void NpcManager::RemoveNpc(uint64_t id) {
  std::unique_lock<std::shared_mutex> lock(mutex_);

  auto it = npcs_.find(id);
  if (it == npcs_.end()) {
    SYSLOG_DEBUG("NpcManager: npc {} not found for removal", id);
    return;
  }

  uint32_t map_id = it->second->GetMapId();
  auto map_it = npcs_by_map_.find(map_id);
  if (map_it != npcs_by_map_.end()) {
    auto& ids = map_it->second;
    ids.erase(std::remove(ids.begin(), ids.end(), id), ids.end());
    if (ids.empty()) {
      npcs_by_map_.erase(map_it);
    }
  }

  npcs_.erase(it);

  SYSLOG_INFO("NpcManager: npc {} removed", id);
}

std::optional<NpcData> NpcManager::GetNpcData(uint64_t id) const {
  std::shared_lock<std::shared_mutex> lock(mutex_);

  auto it = npcs_.find(id);
  if (it != npcs_.end()) {
    return it->second->Data();
  }
  return std::nullopt;
}

std::shared_ptr<NpcEntity> NpcManager::GetNpc(uint64_t id) const {
  std::shared_lock<std::shared_mutex> lock(mutex_);

  auto it = npcs_.find(id);
  if (it != npcs_.end()) {
    return it->second;
  }
  return nullptr;
}

std::vector<NpcData> NpcManager::GetNpcsOnMap(uint32_t map_id) const {
  std::shared_lock<std::shared_mutex> lock(mutex_);

  std::vector<NpcData> result;
  auto map_it = npcs_by_map_.find(map_id);
  if (map_it == npcs_by_map_.end()) {
    return result;
  }

  result.reserve(map_it->second.size());
  for (uint64_t id : map_it->second) {
    auto it = npcs_.find(id);
    if (it != npcs_.end()) {
      result.push_back(it->second->Data());
    }
  }

  return result;
}

size_t NpcManager::TotalCount() const {
  std::shared_lock<std::shared_mutex> lock(mutex_);
  return npcs_.size();
}

void NpcManager::Clear() {
  std::unique_lock<std::shared_mutex> lock(mutex_);
  npcs_.clear();
  npcs_by_map_.clear();
  next_npc_id_ = 1;
  SYSLOG_INFO("NpcManager: all NPCs cleared");
}

}  // namespace mir2::game::npc
