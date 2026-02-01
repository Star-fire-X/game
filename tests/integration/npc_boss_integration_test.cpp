#include <gtest/gtest.h>

#include <entt/entt.hpp>

#include <algorithm>
#include <string>
#include <vector>

#include "ecs/components/character_components.h"
#include "ecs/components/item_component.h"
#include "ecs/event_bus.h"
#include "ecs/events/attribute_events.h"
#include "ecs/events/boss_events.h"
#include "ecs/events/inventory_events.h"
#include "ecs/events/npc_events.h"
#include "ecs/systems/inventory_system.h"
#include "ecs/systems/level_up_system.h"
#include "game/entity/boss_manager.h"
#include "game/npc/npc_entity.h"
#include "game/npc/npc_interaction_handler.h"
#include "game/npc/npc_script_engine.h"
#include "game/npc/npc_types.h"

namespace {

using mir2::ecs::CharacterAttributesComponent;
using mir2::ecs::CharacterIdentityComponent;
using mir2::ecs::EventBus;
using mir2::ecs::ItemComponent;
using mir2::ecs::LevelUpSystem;
using mir2::ecs::events::BossDeadEvent;
using mir2::ecs::events::BossEnterRageEvent;
using mir2::ecs::events::BossSummonEvent;
using mir2::ecs::events::ExperienceGainedEvent;
using mir2::ecs::events::ItemEquippedEvent;
using mir2::ecs::events::ItemUsedEvent;
using mir2::ecs::events::NpcInteractionEvent;
using mir2::ecs::events::NpcOpenMerchantEvent;
using mir2::game::entity::BossConfig;
using mir2::game::entity::BossManager;
using mir2::game::npc::NpcConfig;
using mir2::game::npc::NpcEntity;
using mir2::game::npc::NpcInteractionHandler;
using mir2::game::npc::NpcScriptEngine;
using mir2::game::npc::NpcType;

constexpr uint32_t kMerchantStoreId = 501;
constexpr uint32_t kSwordItemId = 21001;
constexpr uint32_t kPotionItemId = 22001;
constexpr uint32_t kBossRewardItemId = 23001;
constexpr int kSwordPrice = 200;
constexpr int kPotionPrice = 35;
constexpr int kPotionHeal = 60;

entt::entity CreatePlayer(entt::registry& registry, int gold, int hp, int max_hp) {
    const auto entity = registry.create();

    auto& identity = registry.emplace<CharacterIdentityComponent>(entity);
    identity.id = 1001;
    identity.name = "Hero";
    identity.char_class = mir2::common::CharacterClass::WARRIOR;
    identity.gender = mir2::common::Gender::MALE;

    auto& attributes = registry.emplace<CharacterAttributesComponent>(entity);
    attributes.level = 1;
    attributes.experience = 0;
    attributes.max_hp = max_hp;
    attributes.hp = hp;
    attributes.attack = 10;
    attributes.defense = 5;
    attributes.gold = gold;

    return entity;
}

}  // namespace

TEST(NpcBossIntegrationTest, MerchantEquipEnhanceBossRewardFlow) {
    entt::registry registry;
    EventBus event_bus(registry);

    const auto player = CreatePlayer(registry, 800, 120, 120);
    auto& attributes = registry.get<CharacterAttributesComponent>(player);

    NpcScriptEngine script_engine;
    NpcInteractionHandler npc_handler(event_bus, script_engine);

    NpcEntity merchant(1);
    NpcConfig merchant_config;
    merchant_config.id = 1;
    merchant_config.name = "Arms Dealer";
    merchant_config.type = NpcType::kMerchant;
    merchant_config.store_id = kMerchantStoreId;
    merchant.ApplyConfig(merchant_config);

    std::string last_action;
    entt::entity purchased_item = entt::null;
    bool purchase_called = false;

    event_bus.Subscribe<NpcInteractionEvent>([&](const NpcInteractionEvent& event) {
        if (event.player == player) {
            last_action = event.action;
        }
    });
    event_bus.Subscribe<NpcOpenMerchantEvent>([&](const NpcOpenMerchantEvent& event) {
        if (event.player != player) {
            return;
        }
        purchase_called = true;
        auto* attrs = registry.try_get<CharacterAttributesComponent>(player);
        if (!attrs || attrs->gold < kSwordPrice) {
            return;
        }
        attrs->gold -= kSwordPrice;
        auto item = mir2::ecs::InventorySystem::AddItem(
            registry, player, kSwordItemId, 1, &event_bus);
        if (item.has_value()) {
            purchased_item = *item;
            auto* item_component = registry.try_get<ItemComponent>(purchased_item);
            if (item_component) {
                item_component->equip_slot = static_cast<int>(mir2::common::EquipSlot::WEAPON);
                item_component->enhancement_level = 0;
            }
        }
    });

    EXPECT_TRUE(npc_handler.HandleInteraction(player, merchant, "BUY_EQUIP"));
    EXPECT_EQ(last_action, "BUY_EQUIP");
    EXPECT_TRUE(purchase_called);
    ASSERT_TRUE(registry.valid(purchased_item));
    EXPECT_EQ(attributes.gold, 800 - kSwordPrice);

    auto* item_component = registry.try_get<ItemComponent>(purchased_item);
    ASSERT_NE(item_component, nullptr);
    const int base_attack = attributes.attack;
    item_component->enhancement_level = 2;
    attributes.attack += item_component->enhancement_level * 5;

    int equipped_events = 0;
    event_bus.Subscribe<ItemEquippedEvent>([&](const ItemEquippedEvent&) {
        ++equipped_events;
    });

    EXPECT_TRUE(mir2::ecs::InventorySystem::EquipItem(
        registry, player, purchased_item, &event_bus));
    EXPECT_EQ(equipped_events, 1);
    EXPECT_GT(attributes.attack, base_attack);

    auto& boss_manager = BossManager::Instance();
    boss_manager.SetEventBus(&event_bus);

    BossConfig boss_config;
    boss_config.monster_id = 6101;
    boss_config.name = "Dread King";
    boss_config.max_hp = 1000;
    boss_config.attack = 120;
    boss_config.attack_speed = 1.0f;
    boss_config.rage_hp_threshold = 0.3f;
    boss_config.summon_hp_threshold = 0.5f;
    boss_config.summon_count = 3;
    boss_config.summon_monster_ids = {7101, 7102, 7103};
    boss_manager.RegisterBossConfig(boss_config);

    auto boss = boss_manager.CreateBoss(boss_config.monster_id);
    ASSERT_NE(boss, nullptr);

    int summon_events = 0;
    int rage_events = 0;
    int dead_events = 0;
    entt::entity reward_item = entt::null;
    int exp_gained = 0;

    event_bus.Subscribe<BossSummonEvent>([&](const BossSummonEvent&) {
        ++summon_events;
    });
    event_bus.Subscribe<BossEnterRageEvent>([&](const BossEnterRageEvent&) {
        ++rage_events;
    });
    event_bus.Subscribe<ExperienceGainedEvent>([&](const ExperienceGainedEvent& event) {
        exp_gained += event.amount;
    });
    event_bus.Subscribe<BossDeadEvent>([&](const BossDeadEvent&) {
        ++dead_events;
        auto reward = mir2::ecs::InventorySystem::AddItem(
            registry, player, kBossRewardItemId, 1, &event_bus);
        if (reward.has_value()) {
            reward_item = *reward;
        }
        LevelUpSystem::GainExperience(registry, player, 120, &event_bus);
    });

    boss->OnHpChange(500, boss_config.max_hp);
    boss->OnHpChange(250, boss_config.max_hp);
    boss->OnHpChange(0, boss_config.max_hp);

    EXPECT_EQ(summon_events, 1);
    EXPECT_EQ(rage_events, 1);
    EXPECT_EQ(dead_events, 1);
    EXPECT_TRUE(registry.valid(reward_item));
    EXPECT_EQ(exp_gained, 120);

    boss_manager.DestroyBoss(boss->GetBossId());
    boss_manager.SetEventBus(nullptr);
}

TEST(NpcBossIntegrationTest, MerchantPotionPurchaseAndUseRestoresHealth) {
    entt::registry registry;
    EventBus event_bus(registry);

    const auto player = CreatePlayer(registry, 120, 35, 120);
    auto& attributes = registry.get<CharacterAttributesComponent>(player);

    NpcScriptEngine script_engine;
    NpcInteractionHandler npc_handler(event_bus, script_engine);

    NpcEntity merchant(2);
    NpcConfig merchant_config;
    merchant_config.id = 2;
    merchant_config.name = "Healer";
    merchant_config.type = NpcType::kMerchant;
    merchant_config.store_id = kMerchantStoreId;
    merchant.ApplyConfig(merchant_config);

    std::string last_action;
    entt::entity potion_item = entt::null;
    int purchase_calls = 0;

    event_bus.Subscribe<NpcInteractionEvent>([&](const NpcInteractionEvent& event) {
        if (event.player == player) {
            last_action = event.action;
        }
    });
    event_bus.Subscribe<NpcOpenMerchantEvent>([&](const NpcOpenMerchantEvent& event) {
        if (event.player != player) {
            return;
        }
        if (last_action != "BUY_POTION") {
            return;
        }
        ++purchase_calls;
        auto* attrs = registry.try_get<CharacterAttributesComponent>(player);
        if (!attrs || attrs->gold < kPotionPrice) {
            return;
        }
        attrs->gold -= kPotionPrice;
        auto item = mir2::ecs::InventorySystem::AddItem(
            registry, player, kPotionItemId, 1, &event_bus);
        if (item.has_value()) {
            potion_item = *item;
        }
    });

    int used_events = 0;
    event_bus.Subscribe<ItemUsedEvent>([&](const ItemUsedEvent& event) {
        if (event.character != player || event.item_id != kPotionItemId) {
            return;
        }
        ++used_events;
        attributes.hp = std::min(attributes.max_hp, attributes.hp + kPotionHeal);
    });

    EXPECT_TRUE(npc_handler.HandleInteraction(player, merchant, "TALK"));
    EXPECT_EQ(last_action, "TALK");
    EXPECT_EQ(purchase_calls, 0);

    EXPECT_TRUE(npc_handler.HandleInteraction(player, merchant, "BUY_POTION"));
    EXPECT_EQ(last_action, "BUY_POTION");
    EXPECT_EQ(purchase_calls, 1);
    ASSERT_TRUE(registry.valid(potion_item));
    EXPECT_EQ(attributes.gold, 120 - kPotionPrice);

    const int hp_before = attributes.hp;
    EXPECT_TRUE(mir2::ecs::InventorySystem::UseItem(
        registry, player, potion_item, 1, &event_bus));
    EXPECT_EQ(used_events, 1);
    EXPECT_GT(attributes.hp, hp_before);
}
