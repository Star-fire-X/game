/**
 * @file npc_script_engine.cc
 * @brief NPC script engine implementation.
 */

#include "game/npc/npc_script_engine.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <system_error>
#include <utility>

#include <nlohmann/json.hpp>

#include "ecs/event_bus.h"
#include "ecs/events/npc_events.h"
#include "log/logger.h"

namespace mir2::game::npc {

namespace {

std::string to_upper(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
  return value;
}

NpcScriptOp parse_op(const std::string& op) {
  const std::string upper = to_upper(op);
  if (upper == "SAY" || upper == "DIALOG" || upper == "TALK") {
    return NpcScriptOp::kSay;
  }
  if (upper == "GIVE_ITEM") {
    return NpcScriptOp::kGiveItem;
  }
  if (upper == "TAKE_GOLD") {
    return NpcScriptOp::kTakeGold;
  }
  if (upper == "START_QUEST") {
    return NpcScriptOp::kStartQuest;
  }
  if (upper == "TELEPORT") {
    return NpcScriptOp::kTeleport;
  }
  if (upper == "OPEN_MERCHANT" || upper == "OPEN_STORE") {
    return NpcScriptOp::kOpenMerchant;
  }
  if (upper == "OPEN_STORAGE") {
    return NpcScriptOp::kOpenStorage;
  }
  if (upper == "REPAIR") {
    return NpcScriptOp::kRepair;
  }
  if (upper == "OPEN_GUILD") {
    return NpcScriptOp::kOpenGuild;
  }
  if (upper == "GUARD_ALERT") {
    return NpcScriptOp::kGuardAlert;
  }
  return NpcScriptOp::kInvalid;
}

}  // namespace

bool NpcScriptEngine::LoadFromFile(const std::string& path) {
  std::error_code ec;
  auto file_size = std::filesystem::file_size(path, ec);
  if (ec) {
    SYSLOG_WARN("NpcScriptEngine: failed to get file size for {}: {}", path, ec.message());
    return false;
  }
  if (file_size > kMaxScriptFileSize) {
    SYSLOG_ERROR("NpcScriptEngine: script file {} too large: {} bytes (max {})",
                 path, file_size, kMaxScriptFileSize);
    return false;
  }

  std::ifstream input(path);
  if (!input.is_open()) {
    SYSLOG_WARN("NpcScriptEngine: failed to open script file {}", path);
    return false;
  }

  try {
    nlohmann::json root;
    input >> root;
    return LoadFromJson(root);
  } catch (const std::exception& ex) {
    SYSLOG_ERROR("NpcScriptEngine: failed to parse script file {}: {}", path, ex.what());
    return false;
  }
}

bool NpcScriptEngine::RegisterScript(Script script) {
  if (script.id.empty()) {
    SYSLOG_WARN("NpcScriptEngine: script id is empty");
    return false;
  }
  if (scripts_.size() >= kMaxScripts && scripts_.find(script.id) == scripts_.end()) {
    SYSLOG_ERROR("NpcScriptEngine: max scripts limit {} reached", kMaxScripts);
    return false;
  }
  scripts_[script.id] = std::move(script);
  return true;
}

const NpcScriptEngine::Script* NpcScriptEngine::GetScript(const std::string& script_id) const {
  auto it = scripts_.find(script_id);
  if (it == scripts_.end()) {
    return nullptr;
  }
  return &it->second;
}

bool NpcScriptEngine::ExecuteScript(const std::string& script_id,
                                   const NpcScriptContext& context) const {
  const auto* script = GetScript(script_id);
  if (!script) {
    SYSLOG_WARN("NpcScriptEngine: script {} not found for npc {} ({})",
                script_id, context.npc_id, context.npc_name);
    return false;
  }
  return ExecuteScript(*script, context);
}

bool NpcScriptEngine::ExecuteScript(const Script& script,
                                   const NpcScriptContext& context) const {
  if (!context.event_bus) {
    SYSLOG_WARN("NpcScriptEngine: event bus is null for script {}", script.id);
    return false;
  }

  bool executed = false;
  for (const auto& command : script.commands) {
    executed |= ExecuteCommand(command, context);
  }

  return executed;
}

bool NpcScriptEngine::LoadFromJson(const nlohmann::json& root) {
  if (!root.contains("scripts") || !root["scripts"].is_array()) {
    SYSLOG_WARN("NpcScriptEngine: json missing scripts array");
    return false;
  }

  bool loaded_any = false;
  for (const auto& script_json : root["scripts"]) {
    if (!script_json.contains("id")) {
      continue;
    }

    Script script;
    script.id = script_json.value("id", "");
    if (script.id.empty()) {
      continue;
    }

    if (script_json.contains("commands") && script_json["commands"].is_array()) {
      for (const auto& command_json : script_json["commands"]) {
        if (script.commands.size() >= kMaxScriptCommands) {
          SYSLOG_WARN("NpcScriptEngine: script {} truncated at {} commands",
                      script.id, kMaxScriptCommands);
          break;
        }
        script.commands.push_back(ParseCommand(command_json));
      }
    }

    if (RegisterScript(std::move(script))) {
      loaded_any = true;
    }
  }

  if (!loaded_any) {
    SYSLOG_WARN("NpcScriptEngine: no scripts loaded from json");
  }

  return loaded_any;
}

NpcScriptCommand NpcScriptEngine::ParseCommand(const nlohmann::json& command_json) {
  NpcScriptCommand command;
  const std::string op = command_json.value("op", "");
  command.op = parse_op(op);

  switch (command.op) {
    case NpcScriptOp::kSay:
      command.text = command_json.value("text", "");
      if (command.text.empty()) {
        command.text = command_json.value("message", "");
      }
      break;
    case NpcScriptOp::kGiveItem:
      command.item_id = command_json.value("item_id", 0u);
      command.count = command_json.value("count", 1);
      if (command.item_id == 0) {
        SYSLOG_WARN("NpcScriptEngine: invalid item_id=0 in GIVE_ITEM");
        command.op = NpcScriptOp::kInvalid;
      }
      if (command.count <= 0 || command.count > 10000) {
        SYSLOG_WARN("NpcScriptEngine: invalid count={} in GIVE_ITEM", command.count);
        command.op = NpcScriptOp::kInvalid;
      }
      break;
    case NpcScriptOp::kTakeGold:
      command.amount = command_json.value("amount", 0);
      if (command.amount <= 0) {
        SYSLOG_WARN("NpcScriptEngine: invalid amount={} in TAKE_GOLD", command.amount);
        command.op = NpcScriptOp::kInvalid;
      }
      break;
    case NpcScriptOp::kStartQuest:
      command.quest_id = command_json.value("quest_id", 0u);
      if (command.quest_id == 0) {
        SYSLOG_WARN("NpcScriptEngine: invalid quest_id=0 in START_QUEST");
        command.op = NpcScriptOp::kInvalid;
      }
      break;
    case NpcScriptOp::kTeleport:
      command.map_id = command_json.value("map_id", 0);
      command.x = command_json.value("x", 0);
      command.y = command_json.value("y", 0);
      if (command.map_id < 0) {
        SYSLOG_WARN("NpcScriptEngine: invalid map_id={} in TELEPORT", command.map_id);
        command.op = NpcScriptOp::kInvalid;
      }
      if (command.x < 0 || command.y < 0 || command.x > 10000 || command.y > 10000) {
        SYSLOG_WARN("NpcScriptEngine: invalid coords ({},{}) in TELEPORT", command.x, command.y);
        command.op = NpcScriptOp::kInvalid;
      }
      break;
    case NpcScriptOp::kOpenMerchant:
      command.store_id = command_json.value("store_id", 0u);
      break;
    case NpcScriptOp::kOpenStorage:
      break;
    case NpcScriptOp::kRepair:
      break;
    case NpcScriptOp::kOpenGuild:
      command.guild_id = command_json.value("guild_id", 0u);
      break;
    case NpcScriptOp::kGuardAlert:
      command.guard_level = command_json.value("level", 1);
      break;
    case NpcScriptOp::kInvalid:
    default:
      break;
  }

  return command;
}

bool NpcScriptEngine::ExecuteCommand(const NpcScriptCommand& command,
                                     const NpcScriptContext& context) const {
  if (!context.event_bus) {
    return false;
  }

  switch (command.op) {
    case NpcScriptOp::kSay: {
      ecs::events::NpcDialogEvent event;
      event.player = context.player;
      event.npc_id = context.npc_id;
      event.text = command.text;
      context.event_bus->Publish(event);
      return true;
    }
    case NpcScriptOp::kGiveItem: {
      if (command.item_id == 0 || command.count <= 0) {
        SYSLOG_WARN("NpcScriptEngine: invalid GIVE_ITEM params item_id={} count={}",
                    command.item_id, command.count);
        return false;
      }
      ecs::events::NpcGiveItemEvent event;
      event.player = context.player;
      event.npc_id = context.npc_id;
      event.item_id = command.item_id;
      event.count = command.count;
      context.event_bus->Publish(event);
      return true;
    }
    case NpcScriptOp::kTakeGold: {
      if (command.amount <= 0) {
        SYSLOG_WARN("NpcScriptEngine: invalid TAKE_GOLD amount={}", command.amount);
        return false;
      }
      ecs::events::NpcTakeGoldEvent event;
      event.player = context.player;
      event.npc_id = context.npc_id;
      event.amount = command.amount;
      context.event_bus->Publish(event);
      return true;
    }
    case NpcScriptOp::kStartQuest: {
      if (command.quest_id == 0) {
        SYSLOG_WARN("NpcScriptEngine: invalid START_QUEST quest_id=0");
        return false;
      }
      ecs::events::NpcStartQuestEvent event;
      event.player = context.player;
      event.npc_id = context.npc_id;
      event.quest_id = command.quest_id;
      context.event_bus->Publish(event);
      return true;
    }
    case NpcScriptOp::kTeleport: {
      ecs::events::NpcTeleportEvent event;
      event.player = context.player;
      event.npc_id = context.npc_id;
      event.map_id = command.map_id;
      event.x = command.x;
      event.y = command.y;
      context.event_bus->Publish(event);
      return true;
    }
    case NpcScriptOp::kOpenMerchant: {
      ecs::events::NpcOpenMerchantEvent event;
      event.player = context.player;
      event.npc_id = context.npc_id;
      event.store_id = command.store_id;
      context.event_bus->Publish(event);
      return true;
    }
    case NpcScriptOp::kOpenStorage: {
      ecs::events::NpcOpenStorageEvent event;
      event.player = context.player;
      event.npc_id = context.npc_id;
      context.event_bus->Publish(event);
      return true;
    }
    case NpcScriptOp::kRepair: {
      ecs::events::NpcRepairEvent event;
      event.player = context.player;
      event.npc_id = context.npc_id;
      context.event_bus->Publish(event);
      return true;
    }
    case NpcScriptOp::kOpenGuild: {
      ecs::events::NpcOpenGuildEvent event;
      event.player = context.player;
      event.npc_id = context.npc_id;
      event.guild_id = command.guild_id;
      context.event_bus->Publish(event);
      return true;
    }
    case NpcScriptOp::kGuardAlert: {
      ecs::events::NpcGuardAlertEvent event;
      event.player = context.player;
      event.npc_id = context.npc_id;
      event.level = command.guard_level > 0 ? command.guard_level : 1;
      context.event_bus->Publish(event);
      return true;
    }
    case NpcScriptOp::kInvalid:
    default:
      SYSLOG_WARN("NpcScriptEngine: unsupported script op");
      return false;
  }
}

}  // namespace mir2::game::npc
