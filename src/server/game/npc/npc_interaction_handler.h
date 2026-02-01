/**
 * @file npc_interaction_handler.h
 * @brief NPC interaction handler definition.
 */

#ifndef MIR2_GAME_NPC_NPC_INTERACTION_HANDLER_H_
#define MIR2_GAME_NPC_NPC_INTERACTION_HANDLER_H_

#include <string>

#include <entt/entt.hpp>

#include "game/npc/npc_entity.h"
#include "game/npc/npc_script_engine.h"

namespace mir2::ecs {
class EventBus;
}  // namespace mir2::ecs

namespace mir2::game::npc {

/**
 * @brief NPC interaction handler.
 *
 * Handles interactions between player and NPC (dialog, trade, quests, etc.).
 */
class NpcInteractionHandler {
 public:
  NpcInteractionHandler(ecs::EventBus& event_bus, NpcScriptEngine& script_engine);

  /**
   * @brief Handle interaction.
   *
   * @param player Player entity
   * @param npc NPC entity
   * @param action Interaction action (TALK/BUY/QUEST)
   * @return True if handled
   */
  bool HandleInteraction(entt::entity player, const NpcEntity& npc,
                         const std::string& action = "TALK");

  /**
   * @brief Handle script interaction.
   */
  bool HandleScript(entt::entity player, const NpcEntity& npc,
                    const std::string& script_id);

 private:
  bool HandleDefaultAction(entt::entity player, const NpcEntity& npc);

  ecs::EventBus& event_bus_;
  NpcScriptEngine& script_engine_;
};

}  // namespace mir2::game::npc

#endif  // MIR2_GAME_NPC_NPC_INTERACTION_HANDLER_H_
