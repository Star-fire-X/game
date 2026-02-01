#include <gtest/gtest.h>

#include <entt/entt.hpp>

#include "ecs/event_bus.h"
#include "ecs/events/boss_events.h"
#include "game/entity/boss_manager.h"

namespace {

using mir2::ecs::EventBus;
using mir2::ecs::events::BossSpecialSkillEvent;
using mir2::game::entity::BossBehavior;
using mir2::game::entity::BossConfig;
using mir2::game::entity::BossManager;

BossConfig MakeManagerConfig(uint32_t monster_id) {
    BossConfig config;
    config.monster_id = monster_id;
    config.name = "ManagerBoss";
    config.max_hp = 1200;
    config.attack = 80;
    config.attack_speed = 1.1f;
    config.special_skill_interval = 1.0f;
    config.special_skill_damage = 75;
    return config;
}

}  // namespace

TEST(BossManagerTest, CreateBossInitializesFromConfig) {
    entt::registry registry;
    EventBus event_bus(registry);

    auto& manager = BossManager::Instance();
    manager.SetEventBus(&event_bus);

    BossConfig config = MakeManagerConfig(2001);
    config.name = "Kraken";
    manager.RegisterBossConfig(config);

    std::shared_ptr<BossBehavior> boss = manager.CreateBoss(config.monster_id);

    ASSERT_NE(boss, nullptr);
    EXPECT_EQ(boss->GetMonsterId(), config.monster_id);
    EXPECT_EQ(boss->GetName(), config.name);
    EXPECT_EQ(boss->GetCurrentHp(), config.max_hp);
    EXPECT_EQ(boss->GetMaxHp(), config.max_hp);
    EXPECT_EQ(boss->GetAttack(), config.attack);
    EXPECT_GT(boss->GetBossId(), 0u);

    manager.DestroyBoss(boss->GetBossId());
    manager.SetEventBus(nullptr);
}

TEST(BossManagerTest, DestroyBossRemovesInstance) {
    entt::registry registry;
    EventBus event_bus(registry);

    auto& manager = BossManager::Instance();
    manager.SetEventBus(&event_bus);

    BossConfig config = MakeManagerConfig(2002);
    manager.RegisterBossConfig(config);

    std::shared_ptr<BossBehavior> boss = manager.CreateBoss(config.monster_id);
    ASSERT_NE(boss, nullptr);

    const uint32_t boss_id = boss->GetBossId();
    manager.DestroyBoss(boss_id);

    EXPECT_EQ(manager.GetBoss(boss_id), nullptr);

    manager.SetEventBus(nullptr);
}

TEST(BossManagerTest, UpdateAllTriggersSpecialSkillForAliveBosses) {
    entt::registry registry;
    EventBus event_bus(registry);

    auto& manager = BossManager::Instance();
    manager.SetEventBus(&event_bus);

    BossConfig config_a = MakeManagerConfig(3001);
    BossConfig config_b = MakeManagerConfig(3002);
    manager.RegisterBossConfig(config_a);
    manager.RegisterBossConfig(config_b);

    auto boss_a = manager.CreateBoss(config_a.monster_id);
    auto boss_b = manager.CreateBoss(config_b.monster_id);
    ASSERT_NE(boss_a, nullptr);
    ASSERT_NE(boss_b, nullptr);

    int skill_count = 0;
    event_bus.Subscribe<BossSpecialSkillEvent>([&](const BossSpecialSkillEvent&) {
        ++skill_count;
    });

    manager.UpdateAll(1.0f);
    EXPECT_EQ(skill_count, 2);

    skill_count = 0;
    boss_a->OnHpChange(0, boss_a->GetMaxHp());
    manager.UpdateAll(1.0f);
    EXPECT_EQ(skill_count, 1);

    manager.DestroyBoss(boss_a->GetBossId());
    manager.DestroyBoss(boss_b->GetBossId());
    manager.SetEventBus(nullptr);
}
