#include <gtest/gtest.h>

#include <entt/entt.hpp>

#include "ecs/event_bus.h"
#include "ecs/events/boss_events.h"
#include "game/entity/boss_behavior.h"

namespace {

using mir2::ecs::EventBus;
using mir2::ecs::events::BossDeadEvent;
using mir2::ecs::events::BossEnterRageEvent;
using mir2::ecs::events::BossSpecialSkillEvent;
using mir2::ecs::events::BossSummonEvent;
using mir2::game::entity::BossBehavior;
using mir2::game::entity::BossConfig;
using mir2::game::entity::BossPhase;

BossConfig MakeBaseConfig() {
    BossConfig config;
    config.monster_id = 1001;
    config.name = "TestBoss";
    config.max_hp = 1000;
    config.attack = 100;
    config.attack_speed = 1.0f;
    config.rage_attack_multiplier = 2.0f;
    config.rage_attack_speed_multiplier = 1.5f;
    config.summon_count = 3;
    config.summon_monster_ids = {11, 12};
    config.special_skill_interval = 1.0f;
    config.special_skill_damage = 250;
    return config;
}

}  // namespace

TEST(BossBehaviorTest, InitializesDefaultsWhenConfigMissing) {
    BossConfig config;
    config.max_hp = 0;
    config.attack = -5;

    BossBehavior behavior(42, config, nullptr);

    EXPECT_EQ(behavior.GetMonsterId(), 42u);
    EXPECT_EQ(behavior.GetName(), "Boss_42");
    EXPECT_EQ(behavior.GetMaxHp(), 1);
    EXPECT_EQ(behavior.GetCurrentHp(), 1);
    EXPECT_EQ(behavior.GetAttack(), 0);
    EXPECT_EQ(behavior.GetPhase(), BossPhase::kNormal);
    EXPECT_FALSE(behavior.IsDead());
}

TEST(BossBehaviorTest, EnterRageModeUpdatesStatsAndPublishesEvent) {
    entt::registry registry;
    EventBus event_bus(registry);

    BossConfig config = MakeBaseConfig();
    BossBehavior behavior(7, config, &event_bus);

    BossEnterRageEvent captured{};
    int rage_count = 0;
    event_bus.Subscribe<BossEnterRageEvent>([&](const BossEnterRageEvent& event) {
        captured = event;
        ++rage_count;
    });

    behavior.EnterRageMode();

    EXPECT_EQ(rage_count, 1);
    EXPECT_EQ(behavior.GetPhase(), BossPhase::kRage);
    EXPECT_EQ(behavior.GetAttack(),
              static_cast<int32_t>(config.attack * config.rage_attack_multiplier));
    EXPECT_FLOAT_EQ(behavior.GetAttackSpeed(),
                    config.attack_speed * config.rage_attack_speed_multiplier);
    EXPECT_EQ(captured.boss_id, behavior.GetBossId());
    EXPECT_EQ(captured.monster_id, config.monster_id);
    EXPECT_EQ(captured.attack, behavior.GetAttack());
    EXPECT_FLOAT_EQ(captured.attack_speed, behavior.GetAttackSpeed());
}

TEST(BossBehaviorTest, OnHpChangeTriggersRageAt30Percent) {
    entt::registry registry;
    EventBus event_bus(registry);

    BossConfig config = MakeBaseConfig();
    config.rage_hp_threshold = 0.3f;
    config.summon_hp_threshold = 0.0f;

    BossBehavior behavior(8, config, &event_bus);

    int rage_count = 0;
    int summon_count = 0;
    event_bus.Subscribe<BossEnterRageEvent>([&](const BossEnterRageEvent&) {
        ++rage_count;
    });
    event_bus.Subscribe<BossSummonEvent>([&](const BossSummonEvent&) {
        ++summon_count;
    });

    behavior.OnHpChange(300, 1000);

    EXPECT_EQ(rage_count, 1);
    EXPECT_EQ(summon_count, 0);
    EXPECT_EQ(behavior.GetPhase(), BossPhase::kRage);
}

TEST(BossBehaviorTest, SummonMinionsPublishesEvent) {
    entt::registry registry;
    EventBus event_bus(registry);

    BossConfig config = MakeBaseConfig();
    BossBehavior behavior(9, config, &event_bus);

    BossSummonEvent captured{};
    int summon_count = 0;
    event_bus.Subscribe<BossSummonEvent>([&](const BossSummonEvent& event) {
        captured = event;
        ++summon_count;
    });

    behavior.SummonMinions(4);

    EXPECT_EQ(summon_count, 1);
    EXPECT_EQ(behavior.GetPhase(), BossPhase::kSummon);
    EXPECT_EQ(captured.boss_id, behavior.GetBossId());
    EXPECT_EQ(captured.monster_id, config.monster_id);
    EXPECT_EQ(captured.summon_count, 4u);
    EXPECT_EQ(captured.summon_monster_ids, config.summon_monster_ids);
}

TEST(BossBehaviorTest, OnHpChangeTriggersSummonAt50Percent) {
    entt::registry registry;
    EventBus event_bus(registry);

    BossConfig config = MakeBaseConfig();
    config.summon_hp_threshold = 0.5f;
    config.rage_hp_threshold = 0.0f;

    BossBehavior behavior(10, config, &event_bus);

    int rage_count = 0;
    int summon_count = 0;
    event_bus.Subscribe<BossEnterRageEvent>([&](const BossEnterRageEvent&) {
        ++rage_count;
    });
    event_bus.Subscribe<BossSummonEvent>([&](const BossSummonEvent&) {
        ++summon_count;
    });

    behavior.OnHpChange(500, 1000);

    EXPECT_EQ(summon_count, 1);
    EXPECT_EQ(rage_count, 0);
    EXPECT_EQ(behavior.GetPhase(), BossPhase::kSummon);
}

TEST(BossBehaviorTest, CastSpecialSkillPublishesEvent) {
    entt::registry registry;
    EventBus event_bus(registry);

    BossConfig config = MakeBaseConfig();
    config.special_skill_damage = 300;
    BossBehavior behavior(11, config, &event_bus);

    BossSpecialSkillEvent captured{};
    int skill_count = 0;
    event_bus.Subscribe<BossSpecialSkillEvent>([&](const BossSpecialSkillEvent& event) {
        captured = event;
        ++skill_count;
    });

    behavior.CastSpecialSkill();

    EXPECT_EQ(skill_count, 1);
    EXPECT_EQ(captured.boss_id, behavior.GetBossId());
    EXPECT_EQ(captured.monster_id, config.monster_id);
    EXPECT_EQ(captured.damage, config.special_skill_damage);
}

TEST(BossBehaviorTest, OnHpChangeMarksDeadAndPublishesEvent) {
    entt::registry registry;
    EventBus event_bus(registry);

    BossConfig config = MakeBaseConfig();
    BossBehavior behavior(12, config, &event_bus);

    BossDeadEvent captured{};
    int dead_count = 0;
    event_bus.Subscribe<BossDeadEvent>([&](const BossDeadEvent& event) {
        captured = event;
        ++dead_count;
    });

    behavior.OnHpChange(0, config.max_hp);

    EXPECT_TRUE(behavior.IsDead());
    EXPECT_EQ(dead_count, 1);
    EXPECT_EQ(captured.boss_id, behavior.GetBossId());
    EXPECT_EQ(captured.monster_id, config.monster_id);

    behavior.OnHpChange(-10, config.max_hp);
    EXPECT_EQ(dead_count, 1);
}
