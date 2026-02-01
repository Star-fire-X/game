/**
 * @file monster_ai_system_test.cc
 * @brief 怪物AI系统单元测试
 */

#include <gtest/gtest.h>
#include <entt/entt.hpp>

#include "ecs/components/character_components.h"
#include "ecs/components/monster_component.h"
#include "ecs/components/transform_component.h"

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

TEST_F(MonsterAISystemTest, AggroComponentClear) {
    MonsterAggroComponent aggro;
    auto e1 = registry_.create();

    aggro.AddHatred(e1, 10);
    ASSERT_FALSE(aggro.hate_list.empty());

    aggro.Clear();

    EXPECT_TRUE(aggro.hate_list.empty());
    EXPECT_EQ(aggro.cached_top_target_, entt::null);
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

}  // namespace
}  // namespace mir2::ecs
