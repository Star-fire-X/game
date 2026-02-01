#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

#include <entt/entt.hpp>

#include "ecs/event_bus.h"
#include "ecs/events/npc_events.h"
#include "game/npc/npc_script_engine.h"
#include "game/npc/npc_types.h"

namespace {

using mir2::ecs::EventBus;
using mir2::ecs::events::NpcDialogEvent;
using mir2::ecs::events::NpcGiveItemEvent;
using mir2::ecs::events::NpcGuardAlertEvent;
using mir2::ecs::events::NpcOpenGuildEvent;
using mir2::ecs::events::NpcOpenMerchantEvent;
using mir2::ecs::events::NpcOpenStorageEvent;
using mir2::ecs::events::NpcRepairEvent;
using mir2::ecs::events::NpcStartQuestEvent;
using mir2::ecs::events::NpcTakeGoldEvent;
using mir2::ecs::events::NpcTeleportEvent;
using mir2::game::npc::NpcScriptCommand;
using mir2::game::npc::NpcScriptContext;
using mir2::game::npc::NpcScriptEngine;
using mir2::game::npc::NpcScriptOp;
using mir2::game::npc::NpcType;

std::filesystem::path MakeTempScriptPath() {
    static std::atomic<int> counter{0};
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() /
           ("npc_script_test_" + std::to_string(stamp) + "_" +
            std::to_string(counter.fetch_add(1)) + ".json");
}

struct TempScriptFile {
    explicit TempScriptFile(const std::string& contents) {
        path = MakeTempScriptPath();
        std::ofstream output(path);
        output << contents;
    }

    ~TempScriptFile() {
        std::error_code ec;
        std::filesystem::remove(path, ec);
    }

    std::filesystem::path path;
};

TEST(NpcScriptEngineTest, LoadFromFileAndExecuteScriptPublishesEvents) {
    const std::string script_json = R"json(
{
  "scripts": [
    {
      "id": "npc_script",
      "commands": [
        {"op": "SAY", "text": "Welcome"},
        {"op": "GIVE_ITEM", "item_id": 200, "count": 2},
        {"op": "TAKE_GOLD", "amount": 50},
        {"op": "START_QUEST", "quest_id": 7},
        {"op": "TELEPORT", "map_id": 3, "x": 11, "y": 22},
        {"op": "OPEN_MERCHANT", "store_id": 9},
        {"op": "OPEN_STORAGE"},
        {"op": "REPAIR"},
        {"op": "OPEN_GUILD", "guild_id": 6},
        {"op": "GUARD_ALERT", "level": 2}
      ]
    }
  ]
}
)json";

    TempScriptFile temp_file(script_json);
    ASSERT_TRUE(std::filesystem::exists(temp_file.path));

    NpcScriptEngine engine;
    ASSERT_TRUE(engine.LoadFromFile(temp_file.path.string()));

    const auto* script = engine.GetScript("npc_script");
    ASSERT_NE(script, nullptr);
    EXPECT_EQ(script->commands.size(), 10u);

    entt::registry registry;
    EventBus event_bus(registry);

    NpcDialogEvent dialog_event{};
    NpcGiveItemEvent give_item_event{};
    NpcTakeGoldEvent take_gold_event{};
    NpcStartQuestEvent start_quest_event{};
    NpcTeleportEvent teleport_event{};
    NpcOpenMerchantEvent open_merchant_event{};
    NpcOpenStorageEvent open_storage_event{};
    NpcRepairEvent repair_event{};
    NpcOpenGuildEvent open_guild_event{};
    NpcGuardAlertEvent guard_alert_event{};

    int dialog_count = 0;
    int give_item_count = 0;
    int take_gold_count = 0;
    int start_quest_count = 0;
    int teleport_count = 0;
    int open_merchant_count = 0;
    int open_storage_count = 0;
    int repair_count = 0;
    int open_guild_count = 0;
    int guard_alert_count = 0;

    event_bus.Subscribe<NpcDialogEvent>([&](const NpcDialogEvent& event) {
        dialog_event = event;
        ++dialog_count;
    });
    event_bus.Subscribe<NpcGiveItemEvent>([&](const NpcGiveItemEvent& event) {
        give_item_event = event;
        ++give_item_count;
    });
    event_bus.Subscribe<NpcTakeGoldEvent>([&](const NpcTakeGoldEvent& event) {
        take_gold_event = event;
        ++take_gold_count;
    });
    event_bus.Subscribe<NpcStartQuestEvent>([&](const NpcStartQuestEvent& event) {
        start_quest_event = event;
        ++start_quest_count;
    });
    event_bus.Subscribe<NpcTeleportEvent>([&](const NpcTeleportEvent& event) {
        teleport_event = event;
        ++teleport_count;
    });
    event_bus.Subscribe<NpcOpenMerchantEvent>([&](const NpcOpenMerchantEvent& event) {
        open_merchant_event = event;
        ++open_merchant_count;
    });
    event_bus.Subscribe<NpcOpenStorageEvent>([&](const NpcOpenStorageEvent& event) {
        open_storage_event = event;
        ++open_storage_count;
    });
    event_bus.Subscribe<NpcRepairEvent>([&](const NpcRepairEvent& event) {
        repair_event = event;
        ++repair_count;
    });
    event_bus.Subscribe<NpcOpenGuildEvent>([&](const NpcOpenGuildEvent& event) {
        open_guild_event = event;
        ++open_guild_count;
    });
    event_bus.Subscribe<NpcGuardAlertEvent>([&](const NpcGuardAlertEvent& event) {
        guard_alert_event = event;
        ++guard_alert_count;
    });

    NpcScriptContext context;
    context.player = registry.create();
    context.npc_id = 500;
    context.npc_type = NpcType::kMerchant;
    context.npc_name = "Trader";
    context.event_bus = &event_bus;

    EXPECT_TRUE(engine.ExecuteScript("npc_script", context));

    EXPECT_EQ(dialog_count, 1);
    EXPECT_EQ(dialog_event.player, context.player);
    EXPECT_EQ(dialog_event.npc_id, context.npc_id);
    EXPECT_EQ(dialog_event.text, "Welcome");

    EXPECT_EQ(give_item_count, 1);
    EXPECT_EQ(give_item_event.item_id, 200u);
    EXPECT_EQ(give_item_event.count, 2);
    EXPECT_EQ(give_item_event.player, context.player);
    EXPECT_EQ(give_item_event.npc_id, context.npc_id);

    EXPECT_EQ(take_gold_count, 1);
    EXPECT_EQ(take_gold_event.amount, 50);
    EXPECT_EQ(take_gold_event.player, context.player);
    EXPECT_EQ(take_gold_event.npc_id, context.npc_id);

    EXPECT_EQ(start_quest_count, 1);
    EXPECT_EQ(start_quest_event.quest_id, 7u);
    EXPECT_EQ(start_quest_event.player, context.player);
    EXPECT_EQ(start_quest_event.npc_id, context.npc_id);

    EXPECT_EQ(teleport_count, 1);
    EXPECT_EQ(teleport_event.map_id, 3);
    EXPECT_EQ(teleport_event.x, 11);
    EXPECT_EQ(teleport_event.y, 22);
    EXPECT_EQ(teleport_event.player, context.player);
    EXPECT_EQ(teleport_event.npc_id, context.npc_id);

    EXPECT_EQ(open_merchant_count, 1);
    EXPECT_EQ(open_merchant_event.store_id, 9u);
    EXPECT_EQ(open_merchant_event.player, context.player);
    EXPECT_EQ(open_merchant_event.npc_id, context.npc_id);

    EXPECT_EQ(open_storage_count, 1);
    EXPECT_EQ(open_storage_event.player, context.player);
    EXPECT_EQ(open_storage_event.npc_id, context.npc_id);

    EXPECT_EQ(repair_count, 1);
    EXPECT_EQ(repair_event.player, context.player);
    EXPECT_EQ(repair_event.npc_id, context.npc_id);

    EXPECT_EQ(open_guild_count, 1);
    EXPECT_EQ(open_guild_event.guild_id, 6u);
    EXPECT_EQ(open_guild_event.player, context.player);
    EXPECT_EQ(open_guild_event.npc_id, context.npc_id);

    EXPECT_EQ(guard_alert_count, 1);
    EXPECT_EQ(guard_alert_event.level, 2);
    EXPECT_EQ(guard_alert_event.player, context.player);
    EXPECT_EQ(guard_alert_event.npc_id, context.npc_id);
}

TEST(NpcScriptEngineTest, ExecuteScriptReturnsFalseWhenEventBusMissing) {
    NpcScriptEngine engine;
    NpcScriptEngine::Script script;
    script.id = "no_bus";

    NpcScriptCommand command;
    command.op = NpcScriptOp::kSay;
    command.text = "Hello";
    script.commands.push_back(command);

    ASSERT_TRUE(engine.RegisterScript(std::move(script)));

    NpcScriptContext context;
    context.player = entt::null;
    context.npc_id = 1;
    context.event_bus = nullptr;

    EXPECT_FALSE(engine.ExecuteScript("no_bus", context));
}

}  // namespace
