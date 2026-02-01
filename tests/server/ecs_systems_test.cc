#include <gtest/gtest.h>

#include <entt/entt.hpp>

#include "common/types.h"
#include "ecs/components/character_components.h"
#include "ecs/systems/character_utils.h"
#include "ecs/systems/combat_system.h"
#include "ecs/systems/level_up_system.h"
#include "ecs/systems/movement_system.h"

namespace {

using mir2::ecs::CharacterAttributesComponent;
using mir2::ecs::CharacterIdentityComponent;
using mir2::ecs::CharacterStateComponent;

struct GrowthStats {
    int max_hp = 0;
    int max_mp = 0;
    int attack = 0;
    int defense = 0;
    int magic_attack = 0;
    int magic_defense = 0;
};

CharacterAttributesComponent MakeAttributesFromStats(const mir2::common::CharacterStats& stats) {
    CharacterAttributesComponent attributes;
    attributes.level = stats.level;
    attributes.experience = stats.experience;
    attributes.hp = stats.hp;
    attributes.max_hp = stats.max_hp;
    attributes.mp = stats.mp;
    attributes.max_mp = stats.max_mp;
    attributes.attack = stats.attack;
    attributes.defense = stats.defense;
    attributes.magic_attack = stats.magic_attack;
    attributes.magic_defense = stats.magic_defense;
    attributes.speed = stats.speed;
    attributes.gold = stats.gold;
    return attributes;
}

entt::entity CreateTestCharacter(entt::registry& registry,
                                 mir2::common::CharacterClass char_class =
                                     mir2::common::CharacterClass::WARRIOR) {
    auto entity = registry.create();

    CharacterIdentityComponent identity;
    identity.id = 1;
    identity.name = "Test";
    identity.char_class = char_class;
    identity.gender = mir2::common::Gender::MALE;
    registry.emplace<CharacterIdentityComponent>(entity, identity);

    auto stats = mir2::common::get_class_base_stats(char_class);
    registry.emplace<CharacterAttributesComponent>(entity, MakeAttributesFromStats(stats));
    registry.emplace<CharacterStateComponent>(entity, CharacterStateComponent{});
    return entity;
}

mir2::common::CharacterStats MakeBonusStats(int max_hp,
                                       int max_mp,
                                       int attack,
                                       int defense,
                                       int magic_attack,
                                       int magic_defense) {
    mir2::common::CharacterStats bonus{};
    bonus.level = 0;
    bonus.hp = 0;
    bonus.max_hp = max_hp;
    bonus.mp = 0;
    bonus.max_mp = max_mp;
    bonus.attack = attack;
    bonus.defense = defense;
    bonus.magic_attack = magic_attack;
    bonus.magic_defense = magic_defense;
    bonus.speed = 0;
    bonus.experience = 0;
    bonus.gold = 0;
    return bonus;
}

}  // namespace

TEST(LevelUpSystemTest, BasicLevelUp) {
    entt::registry registry;
    auto entity = CreateTestCharacter(registry, mir2::common::CharacterClass::WARRIOR);
    auto& attributes = registry.get<CharacterAttributesComponent>(entity);
    const auto base_stats = mir2::common::get_class_base_stats(mir2::common::CharacterClass::WARRIOR);

    EXPECT_EQ(attributes.GetExpForNextLevel(), 100);

    attributes.experience = attributes.GetExpForNextLevel();
    attributes.hp = attributes.max_hp - 10;
    attributes.mp = attributes.max_mp - 3;

    auto incomplete = registry.create();
    auto& incomplete_attributes = registry.emplace<CharacterAttributesComponent>(incomplete);
    incomplete_attributes.level = 1;
    incomplete_attributes.experience = 999;

    mir2::ecs::LevelUpSystem system;
    system.Update(registry, 0.0f);

    EXPECT_EQ(attributes.level, 2);
    EXPECT_EQ(attributes.experience, 0);
    EXPECT_EQ(attributes.max_hp, base_stats.max_hp + 20);
    EXPECT_EQ(attributes.max_mp, base_stats.max_mp + 5);
    EXPECT_EQ(attributes.attack, base_stats.attack + 3);
    EXPECT_EQ(attributes.defense, base_stats.defense + 2);
    EXPECT_EQ(attributes.magic_attack, base_stats.magic_attack + 1);
    EXPECT_EQ(attributes.magic_defense, base_stats.magic_defense + 1);
    EXPECT_EQ(attributes.hp, attributes.max_hp);
    EXPECT_EQ(attributes.mp, attributes.max_mp);
    EXPECT_EQ(incomplete_attributes.level, 1);
    EXPECT_EQ(incomplete_attributes.experience, 999);
}

TEST(LevelUpSystemTest, MultiLevelUp) {
    entt::registry registry;
    auto entity = CreateTestCharacter(registry, mir2::common::CharacterClass::WARRIOR);
    auto& attributes = registry.get<CharacterAttributesComponent>(entity);
    const auto base_stats = mir2::common::get_class_base_stats(mir2::common::CharacterClass::WARRIOR);

    const int exp_level1 = attributes.GetExpForNextLevel();
    CharacterAttributesComponent exp_level2;
    exp_level2.level = 2;
    const int exp_level2_needed = exp_level2.GetExpForNextLevel();
    EXPECT_EQ(exp_level2_needed, 400);

    attributes.experience = exp_level1 + exp_level2_needed + 50;
    attributes.hp = 1;
    attributes.mp = 1;

    mir2::ecs::LevelUpSystem system;
    system.Update(registry, 0.0f);

    EXPECT_EQ(attributes.level, 3);
    EXPECT_EQ(attributes.experience, 50);
    EXPECT_EQ(attributes.GetExpForNextLevel(), 900);
    EXPECT_EQ(attributes.max_hp, base_stats.max_hp + 20 * 2);
    EXPECT_EQ(attributes.max_mp, base_stats.max_mp + 5 * 2);
    EXPECT_EQ(attributes.attack, base_stats.attack + 3 * 2);
    EXPECT_EQ(attributes.defense, base_stats.defense + 2 * 2);
    EXPECT_EQ(attributes.magic_attack, base_stats.magic_attack + 1 * 2);
    EXPECT_EQ(attributes.magic_defense, base_stats.magic_defense + 1 * 2);
    EXPECT_EQ(attributes.hp, attributes.max_hp);
    EXPECT_EQ(attributes.mp, attributes.max_mp);
}

TEST(LevelUpSystemTest, ClassGrowth) {
    const struct {
        mir2::common::CharacterClass char_class;
        GrowthStats growth;
    } cases[] = {
        {mir2::common::CharacterClass::WARRIOR, {20, 5, 3, 2, 1, 1}},
        {mir2::common::CharacterClass::MAGE, {8, 15, 1, 1, 4, 2}},
        {mir2::common::CharacterClass::TAOIST, {12, 10, 2, 1, 2, 2}},
    };

    mir2::ecs::LevelUpSystem system;
    for (const auto& test_case : cases) {
        entt::registry registry;
        auto entity = CreateTestCharacter(registry, test_case.char_class);
        auto& attributes = registry.get<CharacterAttributesComponent>(entity);
        const auto base_stats = mir2::common::get_class_base_stats(test_case.char_class);

        attributes.experience = attributes.GetExpForNextLevel();
        attributes.hp = attributes.max_hp - 1;
        attributes.mp = attributes.max_mp - 1;

        system.Update(registry, 0.0f);

        EXPECT_EQ(attributes.level, 2);
        EXPECT_EQ(attributes.experience, 0);
        EXPECT_EQ(attributes.max_hp, base_stats.max_hp + test_case.growth.max_hp);
        EXPECT_EQ(attributes.max_mp, base_stats.max_mp + test_case.growth.max_mp);
        EXPECT_EQ(attributes.attack, base_stats.attack + test_case.growth.attack);
        EXPECT_EQ(attributes.defense, base_stats.defense + test_case.growth.defense);
        EXPECT_EQ(attributes.magic_attack, base_stats.magic_attack + test_case.growth.magic_attack);
        EXPECT_EQ(attributes.magic_defense, base_stats.magic_defense + test_case.growth.magic_defense);
        EXPECT_EQ(attributes.hp, attributes.max_hp);
        EXPECT_EQ(attributes.mp, attributes.max_mp);
    }
}

TEST(CombatSystemTest, TakeDamage) {
    entt::registry registry;
    auto entity = CreateTestCharacter(registry);
    auto& attributes = registry.get<CharacterAttributesComponent>(entity);
    attributes.max_hp = 10;
    attributes.hp = 10;

    int damage = mir2::ecs::CombatSystem::TakeDamage(registry, entity, 1);
    EXPECT_EQ(damage, 1);
    EXPECT_EQ(attributes.hp, 9);

    damage = mir2::ecs::CombatSystem::TakeDamage(registry, entity, 50);
    EXPECT_EQ(damage, 50);
    EXPECT_EQ(attributes.hp, 0);

    damage = mir2::ecs::CombatSystem::TakeDamage(registry, entity, 5);
    EXPECT_EQ(damage, 0);
    EXPECT_EQ(attributes.hp, 0);

    auto no_attributes = registry.create();
    EXPECT_EQ(mir2::ecs::CombatSystem::TakeDamage(registry, no_attributes, 5), 0);
}

TEST(CombatSystemTest, Heal) {
    entt::registry registry;
    auto entity = CreateTestCharacter(registry);
    auto& attributes = registry.get<CharacterAttributesComponent>(entity);
    attributes.max_hp = 10;
    attributes.hp = 5;

    int healed = mir2::ecs::CombatSystem::Heal(registry, entity, 20);
    EXPECT_EQ(healed, 5);
    EXPECT_EQ(attributes.hp, 10);

    attributes.hp = 0;
    healed = mir2::ecs::CombatSystem::Heal(registry, entity, 5);
    EXPECT_EQ(healed, 0);
    EXPECT_EQ(attributes.hp, 0);
}

TEST(CombatSystemTest, MPManagement) {
    entt::registry registry;
    auto entity = CreateTestCharacter(registry);
    auto& attributes = registry.get<CharacterAttributesComponent>(entity);
    attributes.max_mp = 5;
    attributes.mp = 3;

    int restored = mir2::ecs::CombatSystem::RestoreMP(registry, entity, 10);
    EXPECT_EQ(restored, 2);
    EXPECT_EQ(attributes.mp, 5);

    bool consumed = mir2::ecs::CombatSystem::ConsumeMP(registry, entity, 6);
    EXPECT_FALSE(consumed);
    EXPECT_EQ(attributes.mp, 5);

    consumed = mir2::ecs::CombatSystem::ConsumeMP(registry, entity, 4);
    EXPECT_TRUE(consumed);
    EXPECT_EQ(attributes.mp, 1);
}

TEST(CombatSystemTest, Respawn) {
    entt::registry registry;
    auto entity = CreateTestCharacter(registry);
    auto& attributes = registry.get<CharacterAttributesComponent>(entity);
    attributes.max_hp = 100;
    attributes.max_mp = 40;
    attributes.hp = 0;
    attributes.mp = 0;

    const mir2::common::Position first_pos{10, 20};
    mir2::ecs::CombatSystem::Respawn(registry, entity, first_pos, 0.5f, 0.25f);

    const auto& state = registry.get<CharacterStateComponent>(entity);
    EXPECT_EQ(state.position.x, 10);
    EXPECT_EQ(state.position.y, 20);
    EXPECT_EQ(attributes.hp, 50);
    EXPECT_EQ(attributes.mp, 10);

    mir2::ecs::CombatSystem::Respawn(registry, entity, {1, 2}, 0.0f, 0.0f);
    EXPECT_EQ(registry.get<CharacterStateComponent>(entity).position.x, 1);
    EXPECT_EQ(registry.get<CharacterStateComponent>(entity).position.y, 2);
    EXPECT_EQ(attributes.hp, 1);
    EXPECT_EQ(attributes.mp, 0);
}

TEST(MovementSystemTest, SetPosition) {
    entt::registry registry;
    auto entity = registry.create();

    mir2::ecs::MovementSystem::SetPosition(registry, entity, 7, 9);

    const auto& state = registry.get<CharacterStateComponent>(entity);
    EXPECT_EQ(state.position.x, 7);
    EXPECT_EQ(state.position.y, 9);
}

TEST(MovementSystemTest, SetMapId) {
    entt::registry registry;
    auto entity = registry.create();

    mir2::ecs::MovementSystem::SetMapId(registry, entity, 5);

    const auto& state = registry.get<CharacterStateComponent>(entity);
    EXPECT_EQ(state.map_id, 5u);
}

TEST(MovementSystemTest, SetDirection) {
    entt::registry registry;
    auto entity = registry.create();

    mir2::ecs::MovementSystem::SetDirection(registry, entity, mir2::common::Direction::UP);

    const auto& state = registry.get<CharacterStateComponent>(entity);
    EXPECT_EQ(state.direction, mir2::common::Direction::UP);
}

TEST(CharacterUtilsTest, GoldOperations) {
    entt::registry registry;
    auto entity = CreateTestCharacter(registry);
    auto& wealth = registry.get<CharacterAttributesComponent>(entity);
    wealth.gold = 100;

    mir2::ecs::CharacterUtils::AddGold(registry, entity, 50);
    EXPECT_EQ(wealth.gold, 150);

    mir2::ecs::CharacterUtils::AddGold(registry, entity, -10);
    EXPECT_EQ(wealth.gold, 150);

    EXPECT_FALSE(mir2::ecs::CharacterUtils::SpendGold(registry, entity, 200));
    EXPECT_EQ(wealth.gold, 150);

    EXPECT_TRUE(mir2::ecs::CharacterUtils::SpendGold(registry, entity, 30));
    EXPECT_EQ(wealth.gold, 120);

    auto no_wealth = registry.create();
    mir2::ecs::CharacterUtils::AddGold(registry, no_wealth, 10);
    EXPECT_EQ(mir2::ecs::CharacterUtils::GetGold(registry, no_wealth), 0);
    EXPECT_FALSE(mir2::ecs::CharacterUtils::SpendGold(registry, no_wealth, 1));
}

TEST(CharacterUtilsTest, StatsModification) {
    entt::registry registry;
    auto entity = CreateTestCharacter(registry);
    auto& attributes = registry.get<CharacterAttributesComponent>(entity);
    attributes.max_hp = 100;
    attributes.hp = 150;
    attributes.max_mp = 60;
    attributes.mp = 80;
    attributes.attack = 10;
    attributes.defense = 5;
    attributes.magic_attack = 7;
    attributes.magic_defense = 3;

    const auto bonus = MakeBonusStats(25, 10, 3, 2, 4, 1);
    mir2::ecs::CharacterUtils::AddStats(registry, entity, bonus);

    EXPECT_EQ(attributes.max_hp, 125);
    EXPECT_EQ(attributes.max_mp, 70);
    EXPECT_EQ(attributes.attack, 13);
    EXPECT_EQ(attributes.defense, 7);
    EXPECT_EQ(attributes.magic_attack, 11);
    EXPECT_EQ(attributes.magic_defense, 4);
    EXPECT_EQ(attributes.hp, 125);
    EXPECT_EQ(attributes.mp, 70);

    auto min_entity = registry.create();
    auto& min_attributes = registry.emplace<CharacterAttributesComponent>(min_entity);
    min_attributes.max_hp = 5;
    min_attributes.hp = 5;
    min_attributes.max_mp = 1;
    min_attributes.mp = 1;
    min_attributes.attack = 1;
    min_attributes.defense = 0;
    min_attributes.magic_attack = 0;
    min_attributes.magic_defense = 0;

    const auto large_bonus = MakeBonusStats(10, 10, 5, 5, 5, 5);
    mir2::ecs::CharacterUtils::RemoveStats(registry, min_entity, large_bonus);

    EXPECT_EQ(min_attributes.max_hp, 1);
    EXPECT_EQ(min_attributes.max_mp, 0);
    EXPECT_EQ(min_attributes.attack, 1);
    EXPECT_EQ(min_attributes.defense, 0);
    EXPECT_EQ(min_attributes.magic_attack, 0);
    EXPECT_EQ(min_attributes.magic_defense, 0);
    EXPECT_EQ(min_attributes.hp, 1);
    EXPECT_EQ(min_attributes.mp, 0);
}
