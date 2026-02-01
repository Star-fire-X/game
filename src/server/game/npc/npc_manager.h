/**
 * @file npc_manager.h
 * @brief NPC manager definition.
 */

#ifndef MIR2_GAME_NPC_NPC_MANAGER_H_
#define MIR2_GAME_NPC_NPC_MANAGER_H_

#include <cstdint>
#include <memory>
#include <optional>
#include <shared_mutex>
#include <unordered_map>
#include <vector>

#include "core/singleton.h"
#include "game/npc/npc_entity.h"
#include "game/npc/npc_types.h"

namespace mir2::game::npc {

/**
 * @brief NPC manager.
 *
 * Singleton responsible for NPC lifecycle management.
 *
 * @note Thread safety: all public methods are thread-safe.
 */
class NpcManager : public core::Singleton<NpcManager> {
  friend class core::Singleton<NpcManager>;

 public:
  /**
   * @brief Create NPC.
   *
   * @param config NPC configuration
   * @return NPC instance
   */
  std::shared_ptr<NpcEntity> CreateNpc(const NpcConfig& config);

  /**
   * @brief Remove NPC.
   *
   * @param id NPC ID
   */
  void RemoveNpc(uint64_t id);

  /**
   * @brief Get NPC data by ID.
   *
   * @return NPC data copy if found.
   */
  std::optional<NpcData> GetNpcData(uint64_t id) const;

  /**
   * @brief Get NPC by ID.
   *
   * @note UNSAFE: returns shared_ptr which may lead to deadlocks if caller
   *       invokes NpcManager methods while holding the entity. For internal use only.
   */
  std::shared_ptr<NpcEntity> GetNpc(uint64_t id) const;

  /**
   * @brief Get NPCs on map.
   *
   * @return NPC data copies.
   */
  std::vector<NpcData> GetNpcsOnMap(uint32_t map_id) const;

  /**
   * @brief Total NPC count.
   */
  size_t TotalCount() const;

  /**
   * @brief Iterate all NPCs.
   */
  template <typename Func>
  void ForEach(Func&& func) const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    for (const auto& [id, npc] : npcs_) {
      NpcData data = npc->Data();
      func(data);
    }
  }

  /**
   * @brief Clear all NPCs.
   */
  void Clear();

 private:
  NpcManager() = default;
  ~NpcManager() = default;

  std::unordered_map<uint64_t, std::shared_ptr<NpcEntity>> npcs_;
  std::unordered_map<uint32_t, std::vector<uint64_t>> npcs_by_map_;
  mutable std::shared_mutex mutex_;
  uint64_t next_npc_id_ = 1;
};

}  // namespace mir2::game::npc

#endif  // MIR2_GAME_NPC_NPC_MANAGER_H_
