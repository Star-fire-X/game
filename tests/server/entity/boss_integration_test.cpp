#include <gtest/gtest.h>

#include <entt/entt.hpp>
#include <vector>

#include "ecs/event_bus.h"
#include "ecs/events/boss_events.h"
#include "game/entity/boss_manager.h"

namespace {

using mir2::ecs::EventBus;
using mir2::ecs::events::BossDeadEvent;
using mir2::ecs::events::BossEnterRageEvent;
using mir2::ecs::events::BossSpecialSkillEvent;
using mir2::ecs::events::BossSummonEvent;
using mir2::game::entity::BossConfig;
using mir2::game::entity::BossManager;
using mir2::game::entity::BossPhase;

}  // namespace

TEST(BossIntegrationTest, BossLifecyclePublishesEventsAndTransitionsPhases) {
    entt::registry registry;
    EventBus event_bus(registry);

    auto& manager = BossManager::Instance();
    manager.SetEventBus(&event_bus);

    BossConfig config;
    config.monster_id = 4001;
    config.name = "Abyss Lord";
    config.max_hp = 1000;
    config.attack = 120;
    config.attack_speed = 1.0f;
    config.rage_hp_threshold = 0.3f;
    config.summon_hp_threshold = 0.5f;
    config.summon_count = 2;
    config.summon_monster_ids = {7001, 7002};
    config.special_skill_interval = 1.0f;
    config.special_skill_damage = 180;
    manager.RegisterBossConfig(config);

    auto boss = manager.CreateBoss(config.monster_id);
    ASSERT_NE(boss, nullptr);
    EXPECT_EQ(boss->GetPhase(), BossPhase::kNormal);

    std::vector<int> event_order;
    int summon_count = 0;
    int rage_count = 0;
    int skill_count = 0;
    int dead_count = 0;

    event_bus.Subscribe<BossSummonEvent>([&](const BossSummonEvent&) {
        ++summon_count;
        event_order.push_back(1);
    });
    event_bus.Subscribe<BossEnterRageEvent>([&](const BossEnterRageEvent&) {
        ++rage_count;
        event_order.push_back(2);
    });
    event_bus.Subscribe<BossSpecialSkillEvent>([&](const BossSpecialSkillEvent&) {
        ++skill_count;
        event_order.push_back(3);
    });
    event_bus.Subscribe<BossDeadEvent>([&](const BossDeadEvent&) {
        ++dead_count;
        event_order.push_back(4);
    });

    boss->OnHpChange(500, config.max_hp);
    EXPECT_EQ(boss->GetPhase(), BossPhase::kSummon);

    boss->OnHpChange(300, config.max_hp);
    EXPECT_EQ(boss->GetPhase(), BossPhase::kRage);

    manager.UpdateAll(1.0f);

    boss->OnHpChange(0, config.max_hp);
    EXPECT_TRUE(boss->IsDead());

    EXPECT_EQ(summon_count, 1);
    EXPECT_EQ(rage_count, 1);
    EXPECT_EQ(skill_count, 1);
    EXPECT_EQ(dead_count, 1);
    ASSERT_EQ(event_order.size(), 4u);
    EXPECT_EQ(event_order[0], 1);
    EXPECT_EQ(event_order[1], 2);
    EXPECT_EQ(event_order[2], 3);
    EXPECT_EQ(event_order[3], 4);

    manager.DestroyBoss(boss->GetBossId());
    manager.SetEventBus(nullptr);
}

TEST(BossIntegrationTest, MultiBossUpdatePublishesEventsForEachBoss) {
    entt::registry registry;
    EventBus event_bus(registry);

    auto& manager = BossManager::Instance();
    manager.SetEventBus(&event_bus);

    BossConfig config;
    config.max_hp = 800;
    config.attack = 60;
    config.attack_speed = 1.0f;
    config.special_skill_interval = 0.5f;
    config.special_skill_damage = 50;

    config.monster_id = 5001;
    manager.RegisterBossConfig(config);
    config.monster_id = 5002;
    manager.RegisterBossConfig(config);
    config.monster_id = 5003;
    manager.RegisterBossConfig(config);

    auto boss_one = manager.CreateBoss(5001);
    auto boss_two = manager.CreateBoss(5002);
    auto boss_three = manager.CreateBoss(5003);
    ASSERT_NE(boss_one, nullptr);
    ASSERT_NE(boss_two, nullptr);
    ASSERT_NE(boss_three, nullptr);

    int skill_count = 0;
    event_bus.Subscribe<BossSpecialSkillEvent>([&](const BossSpecialSkillEvent&) {
        ++skill_count;
    });

    manager.UpdateAll(0.5f);
    EXPECT_EQ(skill_count, 3);

    skill_count = 0;
    boss_two->OnHpChange(0, boss_two->GetMaxHp());
    manager.UpdateAll(0.5f);
    EXPECT_EQ(skill_count, 2);

    manager.DestroyBoss(boss_one->GetBossId());
    manager.DestroyBoss(boss_two->GetBossId());
    manager.DestroyBoss(boss_three->GetBossId());
    manager.SetEventBus(nullptr);
}
