/**
 * @file monster_ai_system_test.cc
 * @brief 怪物AI系统单元测试
 */

#include <gtest/gtest.h>
#include <entt/entt.hpp>

#include "ecs/components/character_components.h"
#include "ecs/components/effect_component.h"
#include "ecs/components/monster_component.h"
#include "ecs/components/transform_component.h"
#include "ecs/event_bus.h"
#include "ecs/events/monster_events.h"
#include "ecs/systems/effect_system.h"

#define private public
#include "ecs/systems/monster_ai_system.h"
#undef private

namespace mir2::ecs {
namespace {

class MonsterAISystemTest : public ::testing::Test {
protected:
    void SetUp() override {
        registry_.clear();
    }

    void TearDown() override {
        registry_.clear();
    }

    entt::registry registry_;
};

template <typename Transform>
void SetTransformPosition(Transform& transform, int32_t x, int32_t y) {
    if constexpr (requires { transform.position.x; transform.position.y; }) {
        transform.position.x = x;
        transform.position.y = y;
    } else {
        transform.x = x;
        transform.y = y;
    }
}

entt::entity CreateMonster(entt::registry& registry, int32_t x = 0, int32_t y = 0) {
    const auto entity = registry.create();

    auto& ai = registry.emplace<MonsterAIComponent>(entity);
    ai.current_state = game::entity::MonsterState::kIdle;

    registry.emplace<MonsterAggroComponent>(entity);

    auto& transform = registry.emplace<TransformComponent>(entity);
    transform.map_id = 1;
    SetTransformPosition(transform, x, y);

    return entity;
}

entt::entity CreateTarget(entt::registry& registry,
                          int32_t x,
                          int32_t y,
                          int hp = 10) {
    const auto entity = registry.create();

    auto& attributes = registry.emplace<CharacterAttributesComponent>(entity);
    attributes.hp = hp;
    attributes.max_hp = hp;

    auto& transform = registry.emplace<TransformComponent>(entity);
    transform.map_id = 1;
    SetTransformPosition(transform, x, y);

    return entity;
}

entt::entity CreateBossCowKing(entt::registry& registry,
                               int hp,
                               int max_hp,
                               int32_t x = 0,
                               int32_t y = 0) {
    const auto entity = registry.create();

    auto& ai = registry.emplace<MonsterAIComponent>(entity);
    ai.ai_type = MonsterAIType::kBossCowKing;
    ai.current_state = game::entity::MonsterState::kIdle;
    ai.return_position = {x, y};

    registry.emplace<MonsterAggroComponent>(entity);

    auto& attributes = registry.emplace<CharacterAttributesComponent>(entity);
    attributes.hp = hp;
    attributes.max_hp = max_hp;

    auto& transform = registry.emplace<TransformComponent>(entity);
    transform.map_id = 1;
    SetTransformPosition(transform, x, y);

    return entity;
}

template <typename Transform>
int32_t GetTransformX(const Transform& transform) {
    if constexpr (requires { transform.position.x; }) {
        return transform.position.x;
    } else {
        return transform.x;
    }
}

template <typename Transform>
int32_t GetTransformY(const Transform& transform) {
    if constexpr (requires { transform.position.y; }) {
        return transform.position.y;
    } else {
        return transform.y;
    }
}

TEST_F(MonsterAISystemTest, AggroComponentAddHatred) {
    MonsterAggroComponent aggro;
    auto entity = registry_.create();
    
    aggro.AddHatred(entity, 100);
    
    EXPECT_EQ(aggro.hate_list[entity], 150);  // 100 * 1.5
}

TEST_F(MonsterAISystemTest, AggroComponentGetTarget) {
    MonsterAggroComponent aggro;
    auto e1 = registry_.create();
    auto e2 = registry_.create();
    
    aggro.AddHatred(e1, 100);
    aggro.AddHatred(e2, 200);
    
    EXPECT_EQ(aggro.GetTargetByHatred(), e2);
}

TEST_F(MonsterAISystemTest, AggroComponentDecayHatred) {
    MonsterAggroComponent aggro;
    auto e1 = registry_.create();
    auto e2 = registry_.create();

    aggro.AddHatred(e1, 10);  // 15
    aggro.AddHatred(e2, 1);   // 1

    aggro.DecayHatred(1.0f);

    auto it = aggro.hate_list.find(e1);
    ASSERT_NE(it, aggro.hate_list.end());
    EXPECT_EQ(it->second, 10);
    EXPECT_EQ(aggro.hate_list.count(e2), 0u);
    EXPECT_EQ(aggro.cached_top_target_, e1);
}

TEST_F(MonsterAISystemTest, AggroComponentClearsAfterTimeout) {
    MonsterAggroComponent aggro;
    auto target = registry_.create();

    aggro.hate_decay_rate = 0.0f;
    aggro.hate_clear_time = 1.0f;
    aggro.AddHatred(target, 100);

    ASSERT_FALSE(aggro.hate_list.empty());
    aggro.DecayHatred(0.5f);
    EXPECT_FALSE(aggro.hate_list.empty());

    aggro.DecayHatred(0.6f);
    EXPECT_TRUE(aggro.hate_list.empty());
    EXPECT_EQ(aggro.cached_top_target_, static_cast<entt::entity>(entt::null));
}

TEST_F(MonsterAISystemTest, AggroComponentClear) {
    MonsterAggroComponent aggro;
    auto e1 = registry_.create();

    aggro.AddHatred(e1, 10);
    ASSERT_FALSE(aggro.hate_list.empty());

    aggro.Clear();

    EXPECT_TRUE(aggro.hate_list.empty());
    EXPECT_EQ(aggro.cached_top_target_, static_cast<entt::entity>(entt::null));
}

TEST_F(MonsterAISystemTest, AggroComponentCachedTarget) {
    MonsterAggroComponent aggro;
    auto e1 = registry_.create();
    auto e2 = registry_.create();

    aggro.AddHatred(e1, 10);
    aggro.AddHatred(e2, 20);

    EXPECT_EQ(aggro.cached_top_target_, e2);
    EXPECT_EQ(aggro.GetTargetByHatred(), e2);

    aggro.hate_list.erase(e2);

    EXPECT_EQ(aggro.GetTargetByHatred(), e1);
    EXPECT_EQ(aggro.cached_top_target_, e1);
}

TEST_F(MonsterAISystemTest, AIStateTransition_IdleToChase) {
    MonsterAISystem system;
    auto monster = CreateMonster(registry_, 0, 0);
    auto& ai = registry_.get<MonsterAIComponent>(monster);
    auto& aggro = registry_.get<MonsterAggroComponent>(monster);

    auto target = CreateTarget(registry_, 5, 5, 10);
    aggro.AddHatred(target, 10);

    system.Update(registry_, 0.1f);

    EXPECT_EQ(ai.current_state, game::entity::MonsterState::kChase);
    EXPECT_EQ(ai.target_entity, target);
}

TEST_F(MonsterAISystemTest, AIStateTransition_ChaseToAttack) {
    MonsterAISystem system;
    auto monster = CreateMonster(registry_, 0, 0);
    auto& ai = registry_.get<MonsterAIComponent>(monster);
    auto& aggro = registry_.get<MonsterAggroComponent>(monster);

    ai.current_state = game::entity::MonsterState::kChase;
    aggro.attack_range = 3;

    auto target = CreateTarget(registry_, 2, 1, 10);
    ai.target_entity = target;

    system.Update(registry_, 0.1f);

    EXPECT_EQ(ai.current_state, game::entity::MonsterState::kAttack);
}

TEST_F(MonsterAISystemTest, AIStateTransition_AttackToReturn) {
    MonsterAISystem system;
    auto monster = CreateMonster(registry_, 0, 0);
    auto& ai = registry_.get<MonsterAIComponent>(monster);
    auto& aggro = registry_.get<MonsterAggroComponent>(monster);

    ai.current_state = game::entity::MonsterState::kAttack;
    aggro.hate_list.clear();

    auto target = CreateTarget(registry_, 1, 1, 0);
    ai.target_entity = target;

    system.Update(registry_, 0.1f);
    EXPECT_EQ(ai.current_state, game::entity::MonsterState::kChase);

    system.Update(registry_, 0.1f);
    EXPECT_EQ(ai.current_state, game::entity::MonsterState::kReturn);
}

TEST_F(MonsterAISystemTest, IsTargetValid_NullEntity) {
    MonsterAISystem system;

    EXPECT_FALSE(system.IsTargetValid(registry_, entt::null));
}

TEST_F(MonsterAISystemTest, IsTargetValid_DeadTarget) {
    MonsterAISystem system;
    auto target = registry_.create();
    auto& attributes = registry_.emplace<CharacterAttributesComponent>(target);
    attributes.hp = 0;

    EXPECT_FALSE(system.IsTargetValid(registry_, target));
}

TEST_F(MonsterAISystemTest, UpdateBossCowKingAI_EntersCrazyModeWhenHpBelow30Percent) {
    MonsterAISystem system;
    auto boss = CreateBossCowKing(registry_, 200, 1000, 10, 12);
    auto& ai = registry_.get<MonsterAIComponent>(boss);

    system.UpdateBossCowKingAI(registry_, boss, 0.1f, 1000);

    EXPECT_TRUE(ai.is_crazy_mode);
    EXPECT_EQ(ai.current_phase, BossPhase::PHASE_3);

    EffectSystem effect_system(registry_);
    EXPECT_TRUE(effect_system.has_frenzy(boss));
}

TEST_F(MonsterAISystemTest, UpdateBossCowKingAI_PhaseTransitionsCorrectly) {
    MonsterAISystem system;
    auto boss = CreateBossCowKing(registry_, 800, 1000);
    auto& ai = registry_.get<MonsterAIComponent>(boss);
    auto& attributes = registry_.get<CharacterAttributesComponent>(boss);

    system.UpdateBossCowKingAI(registry_, boss, 0.1f, 1000);
    EXPECT_EQ(ai.current_phase, BossPhase::PHASE_1);

    attributes.hp = 500;
    system.UpdateBossCowKingAI(registry_, boss, 0.1f, 2000);
    EXPECT_EQ(ai.current_phase, BossPhase::PHASE_2);

    attributes.hp = 200;
    system.UpdateBossCowKingAI(registry_, boss, 0.1f, 3000);
    EXPECT_EQ(ai.current_phase, BossPhase::PHASE_3);
}

TEST_F(MonsterAISystemTest, UpdateBossCowKingAI_SummonsInPhase2) {
    EventBus event_bus(registry_);
    MonsterAISystem system(registry_, event_bus);

    int summon_events = 0;
    events::MonsterSummonEvent last_event;
    event_bus.Subscribe<events::MonsterSummonEvent>(
        [&](events::MonsterSummonEvent& event) {
            ++summon_events;
            last_event = event;
        });

    auto boss = CreateBossCowKing(registry_, 500, 1000, 77, 88);
    auto& ai = registry_.get<MonsterAIComponent>(boss);

    system.UpdateBossCowKingAI(registry_, boss, 0.1f, 1000);

    EXPECT_EQ(ai.current_phase, BossPhase::PHASE_2);
    EXPECT_EQ(summon_events, 1);
    EXPECT_EQ(ai.summon_count, 1);
    EXPECT_FLOAT_EQ(ai.summon_cooldown, 30.0f);
    EXPECT_EQ(last_event.summoner, boss);

    const auto& transform = registry_.get<TransformComponent>(boss);
    EXPECT_EQ(last_event.map_id, transform.map_id);
    EXPECT_EQ(last_event.position.x, GetTransformX(transform));
    EXPECT_EQ(last_event.position.y, GetTransformY(transform));
}

TEST_F(MonsterAISystemTest, IsTargetValid_ReturnsFalseForInvisibleTarget) {
    MonsterAISystem system;
    auto target = registry_.create();
    auto& attributes = registry_.emplace<CharacterAttributesComponent>(target);
    attributes.hp = 10;
    attributes.max_hp = 10;

    EffectSystem effect_system(registry_);
    ActiveEffect invisible_effect;
    invisible_effect.category = EffectCategory::INVISIBLE;
    effect_system.apply_effect(target, invisible_effect);

    EXPECT_FALSE(system.IsTargetValid(registry_, target));
}

TEST_F(MonsterAISystemTest, UpdateBossCowKingAI_CrazyModeExpiresAfterDuration) {
    MonsterAISystem system;
    auto boss = CreateBossCowKing(registry_, 200, 1000);
    auto& ai = registry_.get<MonsterAIComponent>(boss);
    auto& attributes = registry_.get<CharacterAttributesComponent>(boss);

    // 进入疯狂模式
    system.UpdateBossCowKingAI(registry_, boss, 0.1f, 1000);
    ASSERT_TRUE(ai.is_crazy_mode);

    EffectSystem effect_system(registry_);
    ASSERT_TRUE(effect_system.has_frenzy(boss));

    // 模拟时间流逝，疯狂模式结束（同时把血线抬到>=30%，避免立即再次触发疯狂）
    attributes.hp = 400;
    ai.crazy_mode_timer = 0.5f;
    system.UpdateBossCowKingAI(registry_, boss, 1.0f, 16000);

    EXPECT_FALSE(ai.is_crazy_mode);
    EXPECT_FALSE(effect_system.has_frenzy(boss));
}

TEST_F(MonsterAISystemTest, UpdateBossCowKingAI_SummonCountLimited) {
    EventBus event_bus(registry_);
    MonsterAISystem system(registry_, event_bus);

    int summon_events = 0;
    event_bus.Subscribe<events::MonsterSummonEvent>(
        [&](events::MonsterSummonEvent&) { ++summon_events; });

    auto boss = CreateBossCowKing(registry_, 500, 1000);
    auto& ai = registry_.get<MonsterAIComponent>(boss);

    // 设置已达到召唤上限
    ai.summon_count = 3;
    ai.summon_cooldown = 0.0f;

    system.UpdateBossCowKingAI(registry_, boss, 0.1f, 1000);

    EXPECT_EQ(summon_events, 0);
    EXPECT_EQ(ai.summon_count, 3);
}

TEST_F(MonsterAISystemTest, UpdateBossCowKingAI_TeleportCooldownDecreases) {
    MonsterAISystem system;
    auto boss = CreateBossCowKing(registry_, 400, 1000);
    auto& ai = registry_.get<MonsterAIComponent>(boss);

    ai.teleport_cooldown = 5.0f;

    system.UpdateBossCowKingAI(registry_, boss, 2.0f, 1000);

    EXPECT_FLOAT_EQ(ai.teleport_cooldown, 3.0f);
}

}  // namespace
}  // namespace mir2::ecs
