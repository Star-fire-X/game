/**
 * @file npc_events.h
 * @brief NPC event definitions.
 */

#ifndef MIR2_ECS_EVENTS_NPC_EVENTS_H
#define MIR2_ECS_EVENTS_NPC_EVENTS_H

#include <cstdint>
#include <string>
#include <vector>

#include <entt/entt.hpp>

#include "common/types/error_codes.h"

namespace mir2::ecs::events {

/**
 * @brief NPC interaction event.
 */
struct NpcInteractionEvent {
  entt::entity player;
  uint64_t npc_id;
  std::string action;
};

/**
 * @brief NPC dialog event.
 */
struct NpcDialogEvent {
  entt::entity player;
  uint64_t npc_id;
  std::string text;
};

/**
 * @brief NPC menu options event.
 */
struct NpcMenuEvent {
  entt::entity player;
  uint64_t npc_id;
  std::vector<std::string> options;
};

/**
 * @brief NPC dialog close event.
 */
struct NpcCloseDialogEvent {
  entt::entity player;
  uint64_t npc_id;
};

/**
 * @brief NPC gives item event.
 */
struct NpcGiveItemEvent {
  entt::entity player;
  uint64_t npc_id;
  uint32_t item_id;
  int count;
};

/**
 * @brief NPC takes item event.
 */
struct NpcTakeItemEvent {
  entt::entity player;
  uint64_t npc_id;
  uint32_t item_id;
  int count;
};

/**
 * @brief NPC has item query event.
 */
struct NpcHasItemEvent {
  entt::entity player;
  uint64_t npc_id;
  uint64_t request_id = 0;
  uint32_t item_id;
  int count;
  bool* result = nullptr;
  bool* handled = nullptr;
};

/**
 * @brief NPC has item query result event.
 */
struct NpcHasItemResultEvent {
  entt::entity player;
  uint64_t npc_id;
  uint64_t request_id = 0;
  uint32_t item_id = 0;
  int count = 0;
  bool has_item = false;
  bool success = false;
  mir2::common::ErrorCode error_code = mir2::common::ErrorCode::SUCCESS;
};

/**
 * @brief NPC takes gold event.
 */
struct NpcTakeGoldEvent {
  entt::entity player;
  uint64_t npc_id;
  int amount;
};

/**
 * @brief NPC gives gold event.
 */
struct NpcGiveGoldEvent {
  entt::entity player;
  uint64_t npc_id;
  int amount;
};

/**
 * @brief NPC get gold query event.
 */
struct NpcGetGoldEvent {
  entt::entity player;
  uint64_t npc_id;
  uint64_t request_id = 0;
  int* amount = nullptr;
  bool* handled = nullptr;
};

/**
 * @brief NPC get gold query result event.
 */
struct NpcGetGoldResultEvent {
  entt::entity player;
  uint64_t npc_id;
  uint64_t request_id = 0;
  int amount = 0;
  bool success = false;
  mir2::common::ErrorCode error_code = mir2::common::ErrorCode::SUCCESS;
};

/**
 * @brief NPC starts quest event.
 */
struct NpcStartQuestEvent {
  entt::entity player;
  uint64_t npc_id;
  uint32_t quest_id;
};

/**
 * @brief NPC completes quest event.
 */
struct NpcCompleteQuestEvent {
  entt::entity player;
  uint64_t npc_id;
  uint32_t quest_id;
};

/**
 * @brief NPC has quest query event.
 */
struct NpcHasQuestEvent {
  entt::entity player;
  uint64_t npc_id;
  uint32_t quest_id;
  bool* result = nullptr;
  bool* handled = nullptr;
};

/**
 * @brief NPC teleport event.
 */
struct NpcTeleportEvent {
  entt::entity player;
  uint64_t npc_id;
  int32_t map_id;
  int32_t x;
  int32_t y;
};

/**
 * @brief NPC open merchant event.
 */
struct NpcOpenMerchantEvent {
  entt::entity player;
  uint64_t npc_id;
  uint32_t store_id;
};

/**
 * @brief NPC open storage event.
 */
struct NpcOpenStorageEvent {
  entt::entity player;
  uint64_t npc_id;
};

/**
 * @brief NPC repair event.
 */
struct NpcRepairEvent {
  entt::entity player;
  uint64_t npc_id;
};

/**
 * @brief NPC get player name query event.
 */
struct NpcGetPlayerNameEvent {
  entt::entity player;
  uint64_t npc_id;
  uint64_t request_id = 0;
  std::string* name = nullptr;
  bool* handled = nullptr;
};

/**
 * @brief NPC get player name query result event.
 */
struct NpcGetPlayerNameResultEvent {
  entt::entity player;
  uint64_t npc_id;
  uint64_t request_id = 0;
  std::string name;
  bool success = false;
  mir2::common::ErrorCode error_code = mir2::common::ErrorCode::SUCCESS;
};

/**
 * @brief NPC get player level query event.
 */
struct NpcGetPlayerLevelEvent {
  entt::entity player;
  uint64_t npc_id;
  uint64_t request_id = 0;
  int* level = nullptr;
  bool* handled = nullptr;
};

/**
 * @brief NPC get player level query result event.
 */
struct NpcGetPlayerLevelResultEvent {
  entt::entity player;
  uint64_t npc_id;
  uint64_t request_id = 0;
  int level = 0;
  bool success = false;
  mir2::common::ErrorCode error_code = mir2::common::ErrorCode::SUCCESS;
};

/**
 * @brief NPC open guild event.
 */
struct NpcOpenGuildEvent {
  entt::entity player;
  uint64_t npc_id;
  uint32_t guild_id;
};

/**
 * @brief NPC guard alert event.
 */
struct NpcGuardAlertEvent {
  entt::entity player;
  uint64_t npc_id;
  int level;
};

}  // namespace mir2::ecs::events

#endif  // MIR2_ECS_EVENTS_NPC_EVENTS_H
