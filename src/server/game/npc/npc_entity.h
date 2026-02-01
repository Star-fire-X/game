/**
 * @file npc_entity.h
 * @brief NPC entity definition.
 */

#ifndef MIR2_GAME_NPC_NPC_ENTITY_H_
#define MIR2_GAME_NPC_NPC_ENTITY_H_

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

#include "game/npc/npc_types.h"

namespace mir2::game::npc {

/**
 * @brief NPC data structure.
 */
struct NpcData {
  uint64_t id = 0;                      // NPC instance ID
  uint32_t template_id = 0;             // Template ID
  std::string name;                     // NPC name
  NpcType type = NpcType::kQuest;       // Type
  uint32_t map_id = 0;                  // Map ID
  int32_t x = 0;                        // X position
  int32_t y = 0;                        // Y position
  uint8_t direction = 0;                // Facing direction
  bool enabled = true;                  // Interactable
  std::string script_id;                // Script ID
  uint32_t store_id = 0;                // Store ID
  uint32_t guild_id = 0;                // Guild ID
  std::optional<NpcTeleportTarget> teleport_target; // Teleport target
};

/**
 * @brief NPC entity.
 *
 * Represents a single NPC with its data and state.
 * This entity does not own an EnTT entity.
 *
 * @note Thread safety: not thread-safe, use in logic thread.
 */
class NpcEntity : public std::enable_shared_from_this<NpcEntity> {
 public:
  /**
   * @brief Construct NPC entity.
   *
   * @param id NPC unique ID
   */
  explicit NpcEntity(uint64_t id);

  ~NpcEntity() = default;

  // Disable copy
  NpcEntity(const NpcEntity&) = delete;
  NpcEntity& operator=(const NpcEntity&) = delete;

  // Allow move
  NpcEntity(NpcEntity&&) noexcept = default;
  NpcEntity& operator=(NpcEntity&&) noexcept = default;

  // ---- Getters ----

  uint64_t GetId() const { return data_.id; }
  const std::string& GetName() const { return data_.name; }
  NpcType GetType() const { return data_.type; }
  uint32_t GetMapId() const { return data_.map_id; }
  int32_t GetX() const { return data_.x; }
  int32_t GetY() const { return data_.y; }
  uint8_t GetDirection() const { return data_.direction; }
  const std::string& GetScriptId() const { return data_.script_id; }
  bool IsEnabled() const { return data_.enabled; }
  uint32_t GetStoreId() const { return data_.store_id; }
  uint32_t GetGuildId() const { return data_.guild_id; }
  bool HasTeleportTarget() const { return data_.teleport_target.has_value(); }
  const std::optional<NpcTeleportTarget>& GetTeleportTarget() const {
    return data_.teleport_target;
  }

  NpcData& Data() { return data_; }
  const NpcData& Data() const { return data_; }

  // ---- Actions ----

  /**
   * @brief Apply configuration.
   */
  void ApplyConfig(const NpcConfig& config);

  /**
   * @brief Set NPC position.
   */
  void SetPosition(int32_t x, int32_t y);

  /**
   * @brief Set NPC map and position.
   */
  void SetMap(uint32_t map_id, int32_t x, int32_t y);

  /**
   * @brief Set facing direction.
   */
  void SetDirection(uint8_t direction);

  /**
   * @brief Set script ID.
   */
  void SetScriptId(const std::string& script_id);

  /**
   * @brief Set interactable state.
   */
  void SetEnabled(bool enabled);

  /**
   * @brief Set NPC type.
   */
  void SetType(NpcType type);

 private:
  NpcData data_;
};

}  // namespace mir2::game::npc

#endif  // MIR2_GAME_NPC_NPC_ENTITY_H_
