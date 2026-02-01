#include <gtest/gtest.h>

#include <entt/entt.hpp>

#include <chrono>
#include <ctime>
#include <memory>
#include <string>
#include <vector>

#include "ecs/components/character_components.h"
#include "ecs/event_bus.h"
#include "ecs/events/boss_events.h"
#include "ecs/events/npc_events.h"
#include "ecs/systems/inventory_system.h"
#include "game/entity/boss_manager.h"
#include "game/event/event_handler.h"
#include "game/event/event_types.h"
#include "game/event/timed_event_scheduler.h"
#include "game/npc/npc_entity.h"
#include "game/npc/npc_interaction_handler.h"
#include "game/npc/npc_script_engine.h"
#include "game/npc/npc_types.h"

namespace {

using mir2::ecs::CharacterAttributesComponent;
using mir2::ecs::CharacterIdentityComponent;
using mir2::ecs::EventBus;
using mir2::ecs::events::BossDeadEvent;
using mir2::ecs::events::NpcInteractionEvent;
using mir2::ecs::events::NpcOpenMerchantEvent;
using mir2::game::entity::BossBehavior;
using mir2::game::entity::BossConfig;
using mir2::game::entity::BossManager;
using mir2::game::npc::NpcConfig;
using mir2::game::npc::NpcEntity;
using mir2::game::npc::NpcInteractionHandler;
using mir2::game::npc::NpcScriptEngine;
using mir2::game::npc::NpcType;
using legend2::game::event::EventHandler;
using legend2::game::event::EventTriggerType;
using legend2::game::event::TimedEventConfig;
using legend2::game::event::TimedEventScheduler;

constexpr uint32_t kDailyBossEventId = 9901;
constexpr uint32_t kDailyBossMonsterId = 8801;
constexpr uint32_t kRewardItemId = 99001;
constexpr int kRewardSellPrice = 150;

entt::entity CreatePlayer(entt::registry& registry, int gold) {
    const auto entity = registry.create();

    auto& identity = registry.emplace<CharacterIdentityComponent>(entity);
    identity.id = 3001;
    identity.name = "Adventurer";
    identity.char_class = mir2::common::CharacterClass::WARRIOR;
    identity.gender = mir2::common::Gender::MALE;

    auto& attributes = registry.emplace<CharacterAttributesComponent>(entity);
    attributes.level = 1;
    attributes.experience = 0;
    attributes.max_hp = 150;
    attributes.hp = 150;
    attributes.gold = gold;

    return entity;
}

class BossRefreshHandler : public EventHandler {
 public:
    BossRefreshHandler(BossManager& manager, const BossConfig& config)
        : manager_(manager), config_(config) {}

    void OnEventTrigger(uint32_t event_id) override {
        last_event_id = event_id;
        ++trigger_count;
        auto boss = manager_.CreateBoss(config_.monster_id);
        if (boss) {
            last_boss = boss;
            boss_ids.push_back(boss->GetBossId());
        }
    }

    uint32_t last_event_id = 0;
    int trigger_count = 0;
    std::shared_ptr<BossBehavior> last_boss;
    std::vector<uint32_t> boss_ids;

 private:
    BossManager& manager_;
    BossConfig config_;
};

std::tm GetLocalTime() {
    const auto now = std::chrono::system_clock::now();
    const auto now_time_t = std::chrono::system_clock::to_time_t(now);
    std::tm local_time{};
#if defined(_WIN32)
    localtime_s(&local_time, &now_time_t);
#else
    localtime_r(&now_time_t, &local_time);
#endif
    return local_time;
}

}  // namespace

TEST(FullGameplayTest, DailyBossRefreshKillRewardAndNpcSale) {
    entt::registry registry;
    EventBus event_bus(registry);

    const auto player = CreatePlayer(registry, 100);
    auto& attributes = registry.get<CharacterAttributesComponent>(player);

    auto& boss_manager = BossManager::Instance();
    boss_manager.SetEventBus(&event_bus);

    BossConfig boss_config;
    boss_config.monster_id = kDailyBossMonsterId;
    boss_config.name = "Daily Tyrant";
    boss_config.max_hp = 600;
    boss_config.attack = 80;
    boss_config.attack_speed = 1.0f;
    boss_config.rage_hp_threshold = 0.3f;
    boss_config.summon_hp_threshold = 0.5f;
    boss_config.summon_count = 2;
    boss_config.summon_monster_ids = {9301, 9302};
    boss_manager.RegisterBossConfig(boss_config);

    BossRefreshHandler refresh_handler(boss_manager, boss_config);
    auto& scheduler = TimedEventScheduler::Instance();
    scheduler.RegisterHandler(&refresh_handler);

    TimedEventConfig event_config;
    event_config.event_id = kDailyBossEventId;
    event_config.event_name = "DailyBossSpawn";
    event_config.trigger_type = EventTriggerType::kDaily;
    const auto local_time = GetLocalTime();
    event_config.hour = static_cast<uint32_t>(local_time.tm_hour);
    event_config.minute = static_cast<uint32_t>(local_time.tm_min);
    scheduler.RegisterEvent(event_config);

    scheduler.Update(0.1f);
    if (refresh_handler.trigger_count == 0) {
        scheduler.TriggerEvent(kDailyBossEventId);
    }

    ASSERT_EQ(refresh_handler.trigger_count, 1);
    ASSERT_NE(refresh_handler.last_boss, nullptr);

    std::shared_ptr<BossBehavior> boss = refresh_handler.last_boss;

    entt::entity reward_item = entt::null;
    event_bus.Subscribe<BossDeadEvent>([&](const BossDeadEvent&) {
        auto reward = mir2::ecs::InventorySystem::AddItem(
            registry, player, kRewardItemId, 1, &event_bus);
        if (reward.has_value()) {
            reward_item = *reward;
        }
    });

    boss->OnHpChange(0, boss->GetMaxHp());
    ASSERT_TRUE(registry.valid(reward_item));

    NpcScriptEngine script_engine;
    NpcInteractionHandler npc_handler(event_bus, script_engine);

    NpcEntity merchant(9);
    NpcConfig merchant_config;
    merchant_config.id = 9;
    merchant_config.name = "Reward Trader";
    merchant_config.type = NpcType::kMerchant;
    merchant_config.store_id = 400;
    merchant.ApplyConfig(merchant_config);

    std::string last_action;
    int sell_calls = 0;
    const int starting_gold = attributes.gold;

    event_bus.Subscribe<NpcInteractionEvent>([&](const NpcInteractionEvent& event) {
        if (event.player == player) {
            last_action = event.action;
        }
    });
    event_bus.Subscribe<NpcOpenMerchantEvent>([&](const NpcOpenMerchantEvent& event) {
        if (event.player != player) {
            return;
        }
        if (last_action != "SELL") {
            return;
        }
        if (!registry.valid(reward_item)) {
            return;
        }
        registry.destroy(reward_item);
        reward_item = entt::null;
        attributes.gold += kRewardSellPrice;
        ++sell_calls;
    });

    EXPECT_TRUE(npc_handler.HandleInteraction(player, merchant, "SELL"));
    EXPECT_EQ(sell_calls, 1);
    EXPECT_FALSE(registry.valid(reward_item));
    EXPECT_EQ(attributes.gold, starting_gold + kRewardSellPrice);

    const int triggers_after_kill = refresh_handler.trigger_count;
    scheduler.Update(0.1f);
    EXPECT_EQ(refresh_handler.trigger_count, triggers_after_kill);

    scheduler.TriggerEvent(kDailyBossEventId);
    EXPECT_EQ(refresh_handler.trigger_count, triggers_after_kill + 1);

    for (uint32_t boss_id : refresh_handler.boss_ids) {
        boss_manager.DestroyBoss(boss_id);
    }

    scheduler.UnregisterHandler(&refresh_handler);
    boss_manager.SetEventBus(nullptr);
}
