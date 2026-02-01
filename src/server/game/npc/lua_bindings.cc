/**
 * @file lua_bindings.cc
 * @brief Lua bindings for NPC APIs.
 */

#include "game/npc/npc_script_engine.h"

#include <atomic>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#include <sol/sol.hpp>

#include "ecs/event_bus.h"
#include "ecs/events/npc_events.h"
#include "log/logger.h"

namespace mir2::game::npc {

namespace {

struct NpcCallContext {
  entt::entity player = entt::null;
  uint64_t npc_id = 0;
  ecs::EventBus* event_bus = nullptr;
};

bool resolve_context(const sol::object& player_arg,
                     ecs::EventBus* default_bus,
                     NpcCallContext* out) {
  if (!out) {
    return false;
  }

  if (auto ctx = player_arg.as<sol::optional<NpcScriptContext*>>()) {
    if (*ctx) {
      out->player = (*ctx)->player;
      out->npc_id = (*ctx)->npc_id;
      out->event_bus = (*ctx)->event_bus ? (*ctx)->event_bus : default_bus;
      return out->event_bus != nullptr;
    }
  }

  if (auto table = player_arg.as<sol::optional<sol::table>>()) {
    if (table->valid()) {
      sol::optional<uint32_t> player_id = (*table)["player"];
      if (!player_id) {
        player_id = (*table)["player_id"];
      }
      if (!player_id) {
        return false;
      }
      out->player = static_cast<entt::entity>(*player_id);
      sol::optional<uint64_t> npc_id = (*table)["npc_id"];
      if (npc_id) {
        out->npc_id = *npc_id;
      }
      out->event_bus = default_bus;
      return out->event_bus != nullptr;
    }
  }

  if (auto player_id = player_arg.as<sol::optional<uint32_t>>()) {
    out->player = static_cast<entt::entity>(*player_id);
    out->event_bus = default_bus;
    return out->event_bus != nullptr;
  }

  if (auto player_id = player_arg.as<sol::optional<lua_Integer>>()) {
    if (*player_id < 0 ||
        *player_id > static_cast<lua_Integer>(std::numeric_limits<uint32_t>::max())) {
      return false;
    }
    out->player = static_cast<entt::entity>(static_cast<uint32_t>(*player_id));
    out->event_bus = default_bus;
    return out->event_bus != nullptr;
  }

  return false;
}

bool build_menu_options(const sol::table& table,
                        std::vector<std::string>* options) {
  if (!options) {
    return false;
  }

  const std::size_t count = table.size();
  if (count == 0) {
    return false;
  }

  options->clear();
  options->reserve(count);

  for (std::size_t index = 1; index <= count; ++index) {
    sol::optional<std::string> entry = table.get<sol::optional<std::string>>(index);
    if (!entry) {
      return false;
    }
    options->push_back(*entry);
  }

  return true;
}

bool is_valid_count(int value) {
  return value > 0 && value <= 10000;
}

uint64_t next_query_id() {
  static std::atomic<uint64_t> counter{1};
  return counter.fetch_add(1, std::memory_order_relaxed);
}

}  // namespace

void RegisterNpcAPIs(sol::state& lua, ecs::EventBus* event_bus) {
  lua.new_enum("NpcType",
               "Merchant", NpcType::kMerchant,
               "Quest", NpcType::kQuest,
               "Teleport", NpcType::kTeleport,
               "Storage", NpcType::kStorage,
               "Repair", NpcType::kRepair,
               "Guild", NpcType::kGuild,
               "Guard", NpcType::kGuard);

  lua.new_enum("NpcScriptOp",
               "Invalid", NpcScriptOp::kInvalid,
               "Say", NpcScriptOp::kSay,
               "GiveItem", NpcScriptOp::kGiveItem,
               "TakeGold", NpcScriptOp::kTakeGold,
               "StartQuest", NpcScriptOp::kStartQuest,
               "Teleport", NpcScriptOp::kTeleport,
               "OpenMerchant", NpcScriptOp::kOpenMerchant,
               "OpenStorage", NpcScriptOp::kOpenStorage,
               "Repair", NpcScriptOp::kRepair,
               "OpenGuild", NpcScriptOp::kOpenGuild,
               "GuardAlert", NpcScriptOp::kGuardAlert);

  lua.new_usertype<NpcScriptContext>(
      "NpcScriptContext",
      sol::no_constructor,
      "player", sol::readonly_property([](const NpcScriptContext& ctx) {
        return static_cast<uint32_t>(entt::to_integral(ctx.player));
      }),
      "npc_id", sol::readonly(&NpcScriptContext::npc_id),
      "npc_type", sol::readonly(&NpcScriptContext::npc_type),
      "npc_name", sol::readonly(&NpcScriptContext::npc_name));

  sol::table npc = lua["npc"];
  if (!npc.valid()) {
    npc = lua.create_named_table("npc");
  }

  npc.set_function("say", [event_bus](sol::object player_arg,
                                      sol::optional<std::string> text) {
    if (!text) {
      return false;
    }
    NpcCallContext ctx;
    if (!resolve_context(player_arg, event_bus, &ctx)) {
      return false;
    }
    ecs::events::NpcDialogEvent event;
    event.player = ctx.player;
    event.npc_id = ctx.npc_id;
    event.text = *text;
    ctx.event_bus->Publish(event);
    return true;
  });

  npc.set_function("showMenu", [event_bus](sol::object player_arg,
                                           sol::object options_arg) {
    NpcCallContext ctx;
    if (!resolve_context(player_arg, event_bus, &ctx)) {
      return false;
    }

    auto options_table = options_arg.as<sol::optional<sol::table>>();
    if (!options_table || !options_table->valid()) {
      return false;
    }

    std::vector<std::string> options;
    if (!build_menu_options(*options_table, &options)) {
      return false;
    }

    ecs::events::NpcMenuEvent event;
    event.player = ctx.player;
    event.npc_id = ctx.npc_id;
    event.options = std::move(options);
    ctx.event_bus->Publish(event);
    return true;
  });

  npc.set_function("closeDialog", [event_bus](sol::object player_arg) {
    NpcCallContext ctx;
    if (!resolve_context(player_arg, event_bus, &ctx)) {
      return false;
    }
    ecs::events::NpcCloseDialogEvent event;
    event.player = ctx.player;
    event.npc_id = ctx.npc_id;
    ctx.event_bus->Publish(event);
    return true;
  });

  npc.set_function("giveItem", [event_bus](sol::object player_arg,
                                           sol::optional<uint32_t> item_id,
                                           sol::optional<int> count) {
    if (!item_id) {
      return false;
    }
    const int actual_count = count.value_or(1);
    if (*item_id == 0 || !is_valid_count(actual_count)) {
      return false;
    }
    NpcCallContext ctx;
    if (!resolve_context(player_arg, event_bus, &ctx)) {
      return false;
    }
    ecs::events::NpcGiveItemEvent event;
    event.player = ctx.player;
    event.npc_id = ctx.npc_id;
    event.item_id = *item_id;
    event.count = actual_count;
    ctx.event_bus->Publish(event);
    return true;
  });

  npc.set_function("takeItem", [event_bus](sol::object player_arg,
                                           sol::optional<uint32_t> item_id,
                                           sol::optional<int> count) {
    if (!item_id) {
      return false;
    }
    const int actual_count = count.value_or(1);
    if (*item_id == 0 || !is_valid_count(actual_count)) {
      return false;
    }
    NpcCallContext ctx;
    if (!resolve_context(player_arg, event_bus, &ctx)) {
      return false;
    }
    ecs::events::NpcTakeItemEvent event;
    event.player = ctx.player;
    event.npc_id = ctx.npc_id;
    event.item_id = *item_id;
    event.count = actual_count;
    ctx.event_bus->Publish(event);
    return true;
  });

  npc.set_function("hasItem", [event_bus](sol::object player_arg,
                                          sol::optional<uint32_t> item_id,
                                          sol::optional<int> count) {
    if (!item_id) {
      return false;
    }
    const int actual_count = count.value_or(1);
    if (*item_id == 0 || !is_valid_count(actual_count)) {
      return false;
    }
    NpcCallContext ctx;
    if (!resolve_context(player_arg, event_bus, &ctx)) {
      return false;
    }
    const uint64_t request_id = next_query_id();
    bool received = false;
    bool success = false;
    bool has_item = false;
    [[maybe_unused]] auto connection =
        ctx.event_bus->SubscribeScoped<ecs::events::NpcHasItemResultEvent>(
            [request_id, &received, &success, &has_item](
                const ecs::events::NpcHasItemResultEvent& result) {
              if (result.request_id != request_id) {
                return;
              }
              received = true;
              success = result.success;
              has_item = result.has_item;
            });
    ecs::events::NpcHasItemEvent event;
    event.player = ctx.player;
    event.npc_id = ctx.npc_id;
    event.request_id = request_id;
    event.item_id = *item_id;
    event.count = actual_count;
    ctx.event_bus->Publish(event);
    return received && success ? has_item : false;
  });

  npc.set_function("giveGold", [event_bus](sol::object player_arg,
                                           sol::optional<int> amount) {
    if (!amount || *amount <= 0) {
      return false;
    }
    NpcCallContext ctx;
    if (!resolve_context(player_arg, event_bus, &ctx)) {
      return false;
    }
    ecs::events::NpcGiveGoldEvent event;
    event.player = ctx.player;
    event.npc_id = ctx.npc_id;
    event.amount = *amount;
    ctx.event_bus->Publish(event);
    return true;
  });

  npc.set_function("takeGold", [event_bus](sol::object player_arg,
                                           sol::optional<int> amount) {
    if (!amount || *amount <= 0) {
      return false;
    }
    NpcCallContext ctx;
    if (!resolve_context(player_arg, event_bus, &ctx)) {
      return false;
    }
    ecs::events::NpcTakeGoldEvent event;
    event.player = ctx.player;
    event.npc_id = ctx.npc_id;
    event.amount = *amount;
    ctx.event_bus->Publish(event);
    return true;
  });

  npc.set_function("getGold", [event_bus](sol::this_state state,
                                          sol::object player_arg) {
    sol::state_view lua_state(state);
    NpcCallContext ctx;
    if (!resolve_context(player_arg, event_bus, &ctx)) {
      return sol::make_object(lua_state, false);
    }
    const uint64_t request_id = next_query_id();
    bool received = false;
    bool success = false;
    int amount = 0;
    [[maybe_unused]] auto connection =
        ctx.event_bus->SubscribeScoped<ecs::events::NpcGetGoldResultEvent>(
            [request_id, &received, &success, &amount](
                const ecs::events::NpcGetGoldResultEvent& result) {
              if (result.request_id != request_id) {
                return;
              }
              received = true;
              success = result.success;
              amount = result.amount;
            });
    ecs::events::NpcGetGoldEvent event;
    event.player = ctx.player;
    event.npc_id = ctx.npc_id;
    event.request_id = request_id;
    ctx.event_bus->Publish(event);
    if (!received || !success) {
      return sol::make_object(lua_state, false);
    }
    return sol::make_object(lua_state, amount);
  });

  npc.set_function("teleport", [event_bus](sol::object player_arg,
                                           sol::optional<int32_t> map_id,
                                           sol::optional<int32_t> x,
                                           sol::optional<int32_t> y) {
    if (!map_id || !x || !y) {
      return false;
    }
    if (*map_id < 0 || *x < kMinCoordinate || *y < kMinCoordinate ||
        *x > kMaxCoordinate || *y > kMaxCoordinate) {
      return false;
    }
    NpcCallContext ctx;
    if (!resolve_context(player_arg, event_bus, &ctx)) {
      return false;
    }
    ecs::events::NpcTeleportEvent event;
    event.player = ctx.player;
    event.npc_id = ctx.npc_id;
    event.map_id = *map_id;
    event.x = *x;
    event.y = *y;
    ctx.event_bus->Publish(event);
    return true;
  });

  npc.set_function("startQuest", [event_bus](sol::object player_arg,
                                             sol::optional<uint32_t> quest_id) {
    if (!quest_id || *quest_id == 0) {
      return false;
    }
    NpcCallContext ctx;
    if (!resolve_context(player_arg, event_bus, &ctx)) {
      return false;
    }
    ecs::events::NpcStartQuestEvent event;
    event.player = ctx.player;
    event.npc_id = ctx.npc_id;
    event.quest_id = *quest_id;
    ctx.event_bus->Publish(event);
    return true;
  });

  npc.set_function("completeQuest", [event_bus](sol::object player_arg,
                                                sol::optional<uint32_t> quest_id) {
    if (!quest_id || *quest_id == 0) {
      return false;
    }
    NpcCallContext ctx;
    if (!resolve_context(player_arg, event_bus, &ctx)) {
      return false;
    }
    ecs::events::NpcCompleteQuestEvent event;
    event.player = ctx.player;
    event.npc_id = ctx.npc_id;
    event.quest_id = *quest_id;
    ctx.event_bus->Publish(event);
    return true;
  });

  npc.set_function("hasQuest", [event_bus](sol::object player_arg,
                                           sol::optional<uint32_t> quest_id) {
    if (!quest_id || *quest_id == 0) {
      return false;
    }
    NpcCallContext ctx;
    if (!resolve_context(player_arg, event_bus, &ctx)) {
      return false;
    }
    bool result = false;
    bool handled = false;
    ecs::events::NpcHasQuestEvent event;
    event.player = ctx.player;
    event.npc_id = ctx.npc_id;
    event.quest_id = *quest_id;
    event.result = &result;
    event.handled = &handled;
    ctx.event_bus->Publish(event);
    return handled ? result : false;
  });

  npc.set_function("openShop", [event_bus](sol::object player_arg,
                                           sol::optional<uint32_t> shop_id) {
    if (!shop_id) {
      return false;
    }
    NpcCallContext ctx;
    if (!resolve_context(player_arg, event_bus, &ctx)) {
      return false;
    }
    ecs::events::NpcOpenMerchantEvent event;
    event.player = ctx.player;
    event.npc_id = ctx.npc_id;
    event.store_id = *shop_id;
    ctx.event_bus->Publish(event);
    return true;
  });

  npc.set_function("openStorage", [event_bus](sol::object player_arg) {
    NpcCallContext ctx;
    if (!resolve_context(player_arg, event_bus, &ctx)) {
      return false;
    }
    ecs::events::NpcOpenStorageEvent event;
    event.player = ctx.player;
    event.npc_id = ctx.npc_id;
    ctx.event_bus->Publish(event);
    return true;
  });

  npc.set_function("repair", [event_bus](sol::object player_arg) {
    NpcCallContext ctx;
    if (!resolve_context(player_arg, event_bus, &ctx)) {
      return false;
    }
    ecs::events::NpcRepairEvent event;
    event.player = ctx.player;
    event.npc_id = ctx.npc_id;
    ctx.event_bus->Publish(event);
    return true;
  });

  npc.set_function("getPlayerName", [event_bus](sol::this_state state,
                                                sol::object player_arg) {
    sol::state_view lua_state(state);
    NpcCallContext ctx;
    if (!resolve_context(player_arg, event_bus, &ctx)) {
      return sol::make_object(lua_state, false);
    }
    const uint64_t request_id = next_query_id();
    bool received = false;
    bool success = false;
    std::string name;
    [[maybe_unused]] auto connection =
        ctx.event_bus->SubscribeScoped<ecs::events::NpcGetPlayerNameResultEvent>(
            [request_id, &received, &success, &name](
                const ecs::events::NpcGetPlayerNameResultEvent& result) {
              if (result.request_id != request_id) {
                return;
              }
              received = true;
              success = result.success;
              name = result.name;
            });
    ecs::events::NpcGetPlayerNameEvent event;
    event.player = ctx.player;
    event.npc_id = ctx.npc_id;
    event.request_id = request_id;
    ctx.event_bus->Publish(event);
    if (!received || !success) {
      return sol::make_object(lua_state, false);
    }
    return sol::make_object(lua_state, name);
  });

  npc.set_function("getPlayerLevel", [event_bus](sol::this_state state,
                                                 sol::object player_arg) {
    sol::state_view lua_state(state);
    NpcCallContext ctx;
    if (!resolve_context(player_arg, event_bus, &ctx)) {
      return sol::make_object(lua_state, false);
    }
    const uint64_t request_id = next_query_id();
    bool received = false;
    bool success = false;
    int level = 0;
    [[maybe_unused]] auto connection =
        ctx.event_bus->SubscribeScoped<ecs::events::NpcGetPlayerLevelResultEvent>(
            [request_id, &received, &success, &level](
                const ecs::events::NpcGetPlayerLevelResultEvent& result) {
              if (result.request_id != request_id) {
                return;
              }
              received = true;
              success = result.success;
              level = result.level;
            });
    ecs::events::NpcGetPlayerLevelEvent event;
    event.player = ctx.player;
    event.npc_id = ctx.npc_id;
    event.request_id = request_id;
    ctx.event_bus->Publish(event);
    if (!received || !success) {
      return sol::make_object(lua_state, false);
    }
    return sol::make_object(lua_state, level);
  });

  npc.set_function("log", [](sol::optional<std::string> message) {
    if (!message || message->empty()) {
      return false;
    }
    SYSLOG_INFO("NpcLua: {}", *message);
    return true;
  });
}

}  // namespace mir2::game::npc
