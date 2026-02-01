/**
 * @file npc_script_engine.h
 * @brief NPC script engine definition.
 */

#ifndef MIR2_GAME_NPC_NPC_SCRIPT_ENGINE_H_
#define MIR2_GAME_NPC_NPC_SCRIPT_ENGINE_H_

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include <entt/entt.hpp>
#include <nlohmann/json_fwd.hpp>

#include "game/npc/npc_types.h"

namespace mir2::ecs {
class EventBus;
}  // namespace mir2::ecs

namespace mir2::game::npc {

/**
 * @brief NPC script operation type.
 */
enum class NpcScriptOp : uint8_t {
  kInvalid = 0,
  kSay,
  kGiveItem,
  kTakeGold,
  kStartQuest,
  kTeleport,
  kOpenMerchant,
  kOpenStorage,
  kRepair,
  kOpenGuild,
  kGuardAlert
};

/**
 * @brief NPC script command.
 */
struct NpcScriptCommand {
  NpcScriptOp op = NpcScriptOp::kInvalid;
  std::string text;
  uint32_t item_id = 0;
  int count = 0;
  int amount = 0;
  uint32_t quest_id = 0;
  int32_t map_id = 0;
  int32_t x = 0;
  int32_t y = 0;
  uint32_t store_id = 0;
  uint32_t guild_id = 0;
  int guard_level = 0;
};

/**
 * @brief Script execution context.
 */
struct NpcScriptContext {
  entt::entity player = entt::null;
  uint64_t npc_id = 0;
  NpcType npc_type = NpcType::kQuest;
  std::string npc_name;
  ecs::EventBus* event_bus = nullptr;
};

/**
 * @brief NPC script engine.
 */
class NpcScriptEngine {
 public:
  /**
   * @brief Script definition.
   */
  struct Script {
    std::string id;
    std::vector<NpcScriptCommand> commands;
  };

  /**
   * @brief Load scripts from JSON file.
   */
  bool LoadFromFile(const std::string& path);

  /**
   * @brief Register a script.
   */
  bool RegisterScript(Script script);

  /**
   * @brief Get script by ID.
   */
  const Script* GetScript(const std::string& script_id) const;

  /**
   * @brief Execute script by ID.
   */
  bool ExecuteScript(const std::string& script_id, const NpcScriptContext& context) const;

  /**
   * @brief Execute script by instance.
   */
  bool ExecuteScript(const Script& script, const NpcScriptContext& context) const;

 private:
  bool LoadFromJson(const nlohmann::json& root);
  static NpcScriptCommand ParseCommand(const nlohmann::json& command_json);
  bool ExecuteCommand(const NpcScriptCommand& command, const NpcScriptContext& context) const;

  std::unordered_map<std::string, Script> scripts_;
};

}  // namespace mir2::game::npc

#endif  // MIR2_GAME_NPC_NPC_SCRIPT_ENGINE_H_
