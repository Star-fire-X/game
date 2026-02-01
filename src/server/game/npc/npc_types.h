/**
 * @file npc_types.h
 * @brief NPC type definitions and configuration.
 */

#ifndef MIR2_GAME_NPC_NPC_TYPES_H_
#define MIR2_GAME_NPC_NPC_TYPES_H_

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

namespace mir2::game::npc {

// Map coordinate limits.
constexpr int32_t kMinCoordinate = 0;
constexpr int32_t kMaxCoordinate = 10000;

// Script resource limits.
constexpr size_t kMaxScriptCommands = 100;
constexpr size_t kMaxScriptFileSize = 1024 * 1024;  // 1MB
constexpr size_t kMaxScripts = 10000;

/**
 * @brief NPC type definition.
 */
enum class NpcType : uint8_t {
  kMerchant = 0,
  kQuest = 1,
  kTeleport = 2,
  kStorage = 3,
  kRepair = 4,
  kGuild = 5,
  kGuard = 6
};

/**
 * @brief Teleport target for NPC.
 */
struct NpcTeleportTarget {
  int32_t map_id = 0;
  int32_t x = 0;
  int32_t y = 0;
};

/**
 * @brief NPC configuration.
 */
struct NpcConfig {
  uint64_t id = 0;          // NPC unique ID (0 means auto-assign)
  uint32_t template_id = 0; // Optional template ID
  std::string name;         // NPC name
  NpcType type = NpcType::kQuest;
  uint32_t map_id = 0;      // Map ID
  int32_t x = 0;            // X position
  int32_t y = 0;            // Y position
  uint8_t direction = 0;    // Facing direction
  bool enabled = true;      // Interactable
  std::string script_id;    // Script ID
  uint32_t store_id = 0;    // Store ID (merchant)
  uint32_t guild_id = 0;    // Guild ID (guild)
  std::optional<NpcTeleportTarget> teleport_target; // Teleport target
};

}  // namespace mir2::game::npc

#endif  // MIR2_GAME_NPC_NPC_TYPES_H_
