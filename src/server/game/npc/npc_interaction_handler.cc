/**
 * @file npc_interaction_handler.cc
 * @brief NPC interaction handler implementation.
 */

#include "game/npc/npc_interaction_handler.h"

#include "ecs/event_bus.h"
#include "ecs/events/npc_events.h"
#include "log/logger.h"

namespace mir2::game::npc {

NpcInteractionHandler::NpcInteractionHandler(ecs::EventBus& event_bus,
                                             NpcScriptEngine& script_engine)
    : event_bus_(event_bus), script_engine_(script_engine) {}

bool NpcInteractionHandler::HandleInteraction(entt::entity player,
                                              const NpcEntity& npc,
                                              const std::string& action) {
  ecs::events::NpcInteractionEvent interaction_event;
  interaction_event.player = player;
  interaction_event.npc_id = npc.GetId();
  interaction_event.action = action;
  event_bus_.Publish(interaction_event);

  if (!npc.IsEnabled()) {
    ecs::events::NpcDialogEvent dialog_event;
    dialog_event.player = player;
    dialog_event.npc_id = npc.GetId();
    dialog_event.text = "NPC is unavailable.";
    event_bus_.Publish(dialog_event);
    return false;
  }

  bool handled = false;
  if (!npc.GetScriptId().empty()) {
    handled = HandleScript(player, npc, npc.GetScriptId());
  }

  if (!handled) {
    handled = HandleDefaultAction(player, npc);
  }

  if (!handled) {
    SYSLOG_WARN("NpcInteractionHandler: action {} not handled for npc {}",
                action, npc.GetId());
  }

  return handled;
}

bool NpcInteractionHandler::HandleScript(entt::entity player,
                                         const NpcEntity& npc,
                                         const std::string& script_id) {
  NpcScriptContext context;
  context.player = player;
  context.npc_id = npc.GetId();
  context.npc_type = npc.GetType();
  context.npc_name = npc.GetName();
  context.event_bus = &event_bus_;

  return script_engine_.ExecuteScript(script_id, context);
}

bool NpcInteractionHandler::HandleDefaultAction(entt::entity player,
                                                const NpcEntity& npc) {
  switch (npc.GetType()) {
    case NpcType::kMerchant: {
      ecs::events::NpcOpenMerchantEvent event;
      event.player = player;
      event.npc_id = npc.GetId();
      event.store_id = npc.GetStoreId();
      event_bus_.Publish(event);
      return true;
    }
    case NpcType::kStorage: {
      ecs::events::NpcOpenStorageEvent event;
      event.player = player;
      event.npc_id = npc.GetId();
      event_bus_.Publish(event);
      return true;
    }
    case NpcType::kRepair: {
      ecs::events::NpcRepairEvent event;
      event.player = player;
      event.npc_id = npc.GetId();
      event_bus_.Publish(event);
      return true;
    }
    case NpcType::kGuild: {
      ecs::events::NpcOpenGuildEvent event;
      event.player = player;
      event.npc_id = npc.GetId();
      event.guild_id = npc.GetGuildId();
      event_bus_.Publish(event);
      return true;
    }
    case NpcType::kTeleport: {
      if (npc.HasTeleportTarget()) {
        auto target = npc.GetTeleportTarget().value();
        ecs::events::NpcTeleportEvent event;
        event.player = player;
        event.npc_id = npc.GetId();
        event.map_id = target.map_id;
        event.x = target.x;
        event.y = target.y;
        event_bus_.Publish(event);
        return true;
      }

      ecs::events::NpcDialogEvent dialog_event;
      dialog_event.player = player;
      dialog_event.npc_id = npc.GetId();
      dialog_event.text = "No teleport target configured.";
      event_bus_.Publish(dialog_event);
      return false;
    }
    case NpcType::kQuest: {
      ecs::events::NpcDialogEvent dialog_event;
      dialog_event.player = player;
      dialog_event.npc_id = npc.GetId();
      dialog_event.text = "No quest available.";
      event_bus_.Publish(dialog_event);
      return true;
    }
    case NpcType::kGuard: {
      ecs::events::NpcGuardAlertEvent event;
      event.player = player;
      event.npc_id = npc.GetId();
      event.level = 1;
      event_bus_.Publish(event);
      return true;
    }
    default:
      break;
  }

  return false;
}

}  // namespace mir2::game::npc
