#include <gtest/gtest.h>

#include "client/game/monster/monster_manager.h"
#include "client/game/actor_data.h"

namespace {

using mir2::common::Position;
using mir2::game::EntityManager;
using mir2::game::ActorRace;
using mir2::game::monster::ClientMonster;
using mir2::game::monster::ClientMonsterState;
using mir2::game::monster::MonsterManager;

ClientMonster make_monster(uint64_t id, const Position& pos) {
    ClientMonster monster;
    monster.id = id;
    monster.position = pos;
    monster.direction = 2;
    monster.hp = 10;
    monster.max_hp = 10;
    monster.render_config.race = ActorRace::CHICKEN_DOG;
    monster.render_config.appearance = 0;
    return monster;
}

TEST(MonsterManagerTest, AddAndRemoveLifecycle) {
    EntityManager entity_manager;
    MonsterManager manager(entity_manager);

    ClientMonster monster = make_monster(1, {5, 6});

    EXPECT_TRUE(manager.add_monster(monster));
    EXPECT_FALSE(manager.add_monster(monster));
    EXPECT_NE(manager.get_monster(1), nullptr);
    EXPECT_TRUE(entity_manager.contains(1));

    EXPECT_TRUE(manager.remove_monster(1));
    EXPECT_EQ(manager.get_monster(1), nullptr);
    EXPECT_FALSE(entity_manager.contains(1));
}

TEST(MonsterManagerTest, UpdatePositionSyncsEntity) {
    EntityManager entity_manager;
    MonsterManager manager(entity_manager);

    ClientMonster monster = make_monster(10, {1, 1});
    ASSERT_TRUE(manager.add_monster(monster));

    Position next_pos{3, 4};
    EXPECT_TRUE(manager.update_position(10, next_pos, 5));

    const ClientMonster* updated = manager.get_monster(10);
    ASSERT_NE(updated, nullptr);
    EXPECT_EQ(updated->position, next_pos);
    EXPECT_EQ(updated->direction, 5);

    const auto* entity = entity_manager.get_entity(10);
    ASSERT_NE(entity, nullptr);
    EXPECT_EQ(entity->position, next_pos);
    EXPECT_EQ(entity->direction, 5);
}

TEST(MonsterManagerTest, DamageAndDeathTransitions) {
    EntityManager entity_manager;
    MonsterManager manager(entity_manager);

    ClientMonster monster = make_monster(42, {2, 2});
    ASSERT_TRUE(manager.add_monster(monster));

    EXPECT_TRUE(manager.apply_damage(42, 3));
    ClientMonster* damaged = manager.get_monster(42);
    ASSERT_NE(damaged, nullptr);
    EXPECT_EQ(damaged->hp, 7);
    EXPECT_EQ(damaged->state, ClientMonsterState::Struck);

    EXPECT_TRUE(manager.handle_death(42, 0));
    EXPECT_EQ(damaged->state, ClientMonsterState::Death);
    EXPECT_EQ(damaged->hp, 0);
}

TEST(MonsterManagerTest, QueryRangeFindsMonsters) {
    EntityManager entity_manager;
    MonsterManager manager(entity_manager);

    ASSERT_TRUE(manager.add_monster(make_monster(1, {0, 0})));
    ASSERT_TRUE(manager.add_monster(make_monster(2, {5, 5})));
    ASSERT_TRUE(manager.add_monster(make_monster(3, {10, 10})));

    auto near_origin = manager.query_range({0, 0}, 2);
    EXPECT_EQ(near_origin.size(), 1u);
    EXPECT_EQ(near_origin[0]->id, 1u);

    auto mid_range = manager.query_range({8, 8}, 4);
    EXPECT_EQ(mid_range.size(), 2u);
}

TEST(MonsterManagerTest, CorpseExpiresAndRemovesMonster) {
    EntityManager entity_manager;
    MonsterManager manager(entity_manager);

    ClientMonster monster = make_monster(77, {8, 8});
    ASSERT_TRUE(manager.add_monster(monster));

    manager.set_corpse_duration_ms(0);
    EXPECT_TRUE(manager.handle_death(77, 0));

    manager.update(0.0f);

    EXPECT_EQ(manager.get_monster(77), nullptr);
    EXPECT_FALSE(entity_manager.contains(77));
}

}  // namespace
