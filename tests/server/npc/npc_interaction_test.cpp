#include <gtest/gtest.h>

#include <string>
#include <vector>

#include <entt/entt.hpp>

#include "ecs/event_bus.h"
#include "ecs/events/npc_events.h"
#include "game/npc/npc_entity.h"
#include "game/npc/npc_interaction_handler.h"
#include "game/npc/npc_script_engine.h"
#include "game/npc/npc_types.h"

namespace {

using mir2::ecs::EventBus;
using mir2::ecs::events::NpcDialogEvent;
using mir2::ecs::events::NpcInteractionEvent;
using mir2::ecs::events::NpcOpenMerchantEvent;
using mir2::ecs::events::NpcTeleportEvent;
using mir2::game::npc::NpcConfig;
using mir2::game::npc::NpcEntity;
using mir2::game::npc::NpcInteractionHandler;
using mir2::game::npc::NpcScriptCommand;
using mir2::game::npc::NpcScriptEngine;
using mir2::game::npc::NpcScriptOp;
using mir2::game::npc::NpcTeleportTarget;
using mir2::game::npc::NpcType;

TEST(NpcInteractionTest, DisabledNpcPublishesDialogAndReturnsFalse) {
    entt::registry registry;
    EventBus event_bus(registry);
    NpcScriptEngine script_engine;
    NpcInteractionHandler handler(event_bus, script_engine);

    NpcEntity npc(1);
    NpcConfig config;
    config.id = 1;
    config.name = "Disabled";
    config.type = NpcType::kQuest;
    config.enabled = false;
    npc.ApplyConfig(config);

    auto player = registry.create();

    NpcInteractionEvent interaction_event{};
    NpcDialogEvent dialog_event{};
    int interaction_count = 0;
    int dialog_count = 0;

    event_bus.Subscribe<NpcInteractionEvent>([&](const NpcInteractionEvent& event) {
        interaction_event = event;
        ++interaction_count;
    });
    event_bus.Subscribe<NpcDialogEvent>([&](const NpcDialogEvent& event) {
        dialog_event = event;
        ++dialog_count;
    });

    EXPECT_FALSE(handler.HandleInteraction(player, npc, "TALK"));

    EXPECT_EQ(interaction_count, 1);
    EXPECT_EQ(interaction_event.player, player);
    EXPECT_EQ(interaction_event.npc_id, npc.GetId());
    EXPECT_EQ(interaction_event.action, "TALK");

    EXPECT_EQ(dialog_count, 1);
    EXPECT_EQ(dialog_event.player, player);
    EXPECT_EQ(dialog_event.npc_id, npc.GetId());
    EXPECT_EQ(dialog_event.text, "NPC is unavailable.");
}

TEST(NpcInteractionTest, ScriptedNpcOverridesDefaultAction) {
    entt::registry registry;
    EventBus event_bus(registry);
    NpcScriptEngine script_engine;

    NpcScriptEngine::Script script;
    script.id = "hello_script";
    NpcScriptCommand command;
    command.op = NpcScriptOp::kSay;
    command.text = "Greetings";
    script.commands.push_back(command);
    ASSERT_TRUE(script_engine.RegisterScript(std::move(script)));

    NpcInteractionHandler handler(event_bus, script_engine);

    NpcEntity npc(2);
    NpcConfig config;
    config.id = 2;
    config.name = "Shopkeeper";
    config.type = NpcType::kMerchant;
    config.store_id = 42;
    config.script_id = "hello_script";
    npc.ApplyConfig(config);

    auto player = registry.create();

    NpcDialogEvent dialog_event{};
    NpcOpenMerchantEvent merchant_event{};
    int dialog_count = 0;
    int merchant_count = 0;

    event_bus.Subscribe<NpcDialogEvent>([&](const NpcDialogEvent& event) {
        dialog_event = event;
        ++dialog_count;
    });
    event_bus.Subscribe<NpcOpenMerchantEvent>([&](const NpcOpenMerchantEvent& event) {
        merchant_event = event;
        ++merchant_count;
    });

    EXPECT_TRUE(handler.HandleInteraction(player, npc, "TALK"));

    EXPECT_EQ(dialog_count, 1);
    EXPECT_EQ(dialog_event.text, "Greetings");
    EXPECT_EQ(dialog_event.player, player);
    EXPECT_EQ(dialog_event.npc_id, npc.GetId());

    EXPECT_EQ(merchant_count, 0);
    EXPECT_EQ(merchant_event.store_id, 0u);
}

TEST(NpcInteractionTest, MerchantBuySellPublishesOpenMerchant) {
    entt::registry registry;
    EventBus event_bus(registry);
    NpcScriptEngine script_engine;
    NpcInteractionHandler handler(event_bus, script_engine);

    NpcEntity npc(3);
    NpcConfig config;
    config.id = 3;
    config.name = "Trader";
    config.type = NpcType::kMerchant;
    config.store_id = 77;
    npc.ApplyConfig(config);

    auto player = registry.create();

    std::vector<std::string> actions;
    int merchant_count = 0;
    NpcOpenMerchantEvent merchant_event{};

    event_bus.Subscribe<NpcInteractionEvent>([&](const NpcInteractionEvent& event) {
        actions.push_back(event.action);
    });
    event_bus.Subscribe<NpcOpenMerchantEvent>([&](const NpcOpenMerchantEvent& event) {
        merchant_event = event;
        ++merchant_count;
    });

    EXPECT_TRUE(handler.HandleInteraction(player, npc, "BUY"));
    EXPECT_EQ(merchant_count, 1);
    EXPECT_EQ(merchant_event.player, player);
    EXPECT_EQ(merchant_event.npc_id, npc.GetId());
    EXPECT_EQ(merchant_event.store_id, 77u);

    EXPECT_TRUE(handler.HandleInteraction(player, npc, "SELL"));
    EXPECT_EQ(merchant_count, 2);
    EXPECT_EQ(merchant_event.player, player);
    EXPECT_EQ(merchant_event.npc_id, npc.GetId());
    EXPECT_EQ(merchant_event.store_id, 77u);

    ASSERT_EQ(actions.size(), 2u);
    EXPECT_EQ(actions[0], "BUY");
    EXPECT_EQ(actions[1], "SELL");
}

TEST(NpcInteractionTest, TeleportNpcPublishesTeleportEvent) {
    entt::registry registry;
    EventBus event_bus(registry);
    NpcScriptEngine script_engine;
    NpcInteractionHandler handler(event_bus, script_engine);

    NpcEntity npc(4);
    NpcConfig config;
    config.id = 4;
    config.name = "Teleporter";
    config.type = NpcType::kTeleport;
    config.teleport_target = NpcTeleportTarget{88, 12, 34};
    npc.ApplyConfig(config);

    auto player = registry.create();

    int teleport_count = 0;
    NpcTeleportEvent teleport_event{};

    event_bus.Subscribe<NpcTeleportEvent>([&](const NpcTeleportEvent& event) {
        teleport_event = event;
        ++teleport_count;
    });

    EXPECT_TRUE(handler.HandleInteraction(player, npc, "TALK"));

    EXPECT_EQ(teleport_count, 1);
    EXPECT_EQ(teleport_event.player, player);
    EXPECT_EQ(teleport_event.npc_id, npc.GetId());
    EXPECT_EQ(teleport_event.map_id, 88);
    EXPECT_EQ(teleport_event.x, 12);
    EXPECT_EQ(teleport_event.y, 34);
}

TEST(NpcInteractionTest, QuestNpcStartsDialog) {
    entt::registry registry;
    EventBus event_bus(registry);
    NpcScriptEngine script_engine;
    NpcInteractionHandler handler(event_bus, script_engine);

    NpcEntity npc(5);
    NpcConfig config;
    config.id = 5;
    config.name = "QuestGiver";
    config.type = NpcType::kQuest;
    npc.ApplyConfig(config);

    auto player = registry.create();

    int dialog_count = 0;
    NpcDialogEvent dialog_event{};

    event_bus.Subscribe<NpcDialogEvent>([&](const NpcDialogEvent& event) {
        dialog_event = event;
        ++dialog_count;
    });

    EXPECT_TRUE(handler.HandleInteraction(player, npc, "TALK"));

    EXPECT_EQ(dialog_count, 1);
    EXPECT_EQ(dialog_event.player, player);
    EXPECT_EQ(dialog_event.npc_id, npc.GetId());
    EXPECT_EQ(dialog_event.text, "No quest available.");
}

}  // namespace
