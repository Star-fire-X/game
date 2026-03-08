#include <gtest/gtest.h>

#include <array>

#include <entt/entt.hpp>

#include "ecs/components/character_components.h"
#include "ecs/components/effect_component.h"
#include "ecs/systems/effect_system.h"

namespace {

using mir2::ecs::ActiveEffect;
using mir2::ecs::CharacterAttributesComponent;
using mir2::ecs::EffectCategory;
using mir2::ecs::EffectListComponent;
using mir2::ecs::EffectSystem;

entt::entity CreateEntityWithAttributes(entt::registry& registry,
                                        int max_hp,
                                        int hp,
                                        int attack,
                                        int defense) {
    auto entity = registry.create();
    auto& attributes = registry.emplace<CharacterAttributesComponent>(entity);
    attributes.max_hp = max_hp;
    attributes.hp = hp;
    attributes.attack = attack;
    attributes.defense = defense;
    return entity;
}

ActiveEffect MakeEffect(EffectCategory category) {
    ActiveEffect effect;
    effect.category = category;
    return effect;
}

}  // namespace

TEST(EffectSystemTest, PoisonDamageUsesMaxHpPercent) {
    entt::registry registry;
    EffectSystem system(registry);
    const auto entity = CreateEntityWithAttributes(registry, 200, 150, 0, 0);

    ActiveEffect poison = MakeEffect(EffectCategory::POISON);
    poison.poison_percent = 5;
    poison.tick_interval_ms = 1000;
    poison.last_tick_ms = 0;

    system.apply_effect(entity, poison);
    system.update(1000);

    const auto& attributes = registry.get<CharacterAttributesComponent>(entity);
    const int expected_damage = 200 * 5 / 100;
    EXPECT_EQ(attributes.hp, 150 - expected_damage);
}

TEST(EffectSystemTest, InvisibilityTogglesOnAndOff) {
    entt::registry registry;
    EffectSystem system(registry);
    const auto entity = registry.create();

    ActiveEffect poison = MakeEffect(EffectCategory::POISON);
    ActiveEffect invisible = MakeEffect(EffectCategory::INVISIBLE);

    system.apply_effect(entity, poison);
    EXPECT_FALSE(system.is_invisible(entity));

    system.apply_effect(entity, invisible);
    EXPECT_TRUE(system.is_invisible(entity));

    system.break_invisibility(entity);
    EXPECT_FALSE(system.is_invisible(entity));

    const auto* effects = registry.try_get<EffectListComponent>(entity);
    ASSERT_NE(effects, nullptr);
    ASSERT_EQ(effects->effects.size(), 1u);
    EXPECT_EQ(effects->effects[0].category, EffectCategory::POISON);
}

TEST(EffectSystemTest, ApplyEffectPreservesSourceEntity) {
    entt::registry registry;
    EffectSystem system(registry);
    const auto source = registry.create();
    const auto target = registry.create();

    ActiveEffect buff = MakeEffect(EffectCategory::STAT_BUFF);
    buff.source_entity = source;
    buff.value = 2;

    system.apply_effect(target, buff);

    const auto* effects = registry.try_get<EffectListComponent>(target);
    ASSERT_NE(effects, nullptr);
    ASSERT_EQ(effects->effects.size(), 1u);
    EXPECT_EQ(effects->effects.front().source_entity, source);
}

TEST(EffectSystemTest, ExpiredEffectsAreRemovedAndStatsRecalculated) {
    entt::registry registry;
    EffectSystem system(registry);
    const auto entity = CreateEntityWithAttributes(registry, 100, 100, 10, 5);

    ActiveEffect buff = MakeEffect(EffectCategory::STAT_BUFF);
    buff.value = 5;
    buff.end_time_ms = 500;

    system.apply_effect(entity, buff);

    auto& attributes = registry.get<CharacterAttributesComponent>(entity);
    EXPECT_EQ(attributes.attack, 15);

    system.update(1000);

    const auto* effects = registry.try_get<EffectListComponent>(entity);
    ASSERT_NE(effects, nullptr);
    EXPECT_TRUE(effects->effects.empty());
    EXPECT_EQ(attributes.attack, 10);
}

TEST(EffectSystemTest, ImmobilizedReturnsTrueForControlEffects) {
    entt::registry registry;
    EffectSystem system(registry);

    const std::array<EffectCategory, 3> immobilized = {
        EffectCategory::STUN,
        EffectCategory::HOLY_SEIZE,
        EffectCategory::PARALYSIS
    };

    for (const auto category : immobilized) {
        const auto entity = registry.create();
        ActiveEffect effect = MakeEffect(category);
        system.apply_effect(entity, effect);
        EXPECT_TRUE(system.is_immobilized(entity));
    }

    const auto slow_entity = registry.create();
    ActiveEffect slow = MakeEffect(EffectCategory::SLOW);
    system.apply_effect(slow_entity, slow);
    EXPECT_FALSE(system.is_immobilized(slow_entity));
}

TEST(EffectSystemTest, FrenzyMultipliersAndPresence) {
    entt::registry registry;
    EffectSystem system(registry);
    const auto entity = CreateEntityWithAttributes(registry, 100, 100, 100, 50);

    ActiveEffect frenzy_a = MakeEffect(EffectCategory::FRENZY);
    frenzy_a.attack_multiplier = 1.5f;
    frenzy_a.defense_multiplier = 0.7f;

    ActiveEffect frenzy_b = MakeEffect(EffectCategory::FRENZY);
    frenzy_b.attack_multiplier = 1.2f;
    frenzy_b.defense_multiplier = 0.9f;

    system.apply_effect(entity, frenzy_a);
    system.apply_effect(entity, frenzy_b);

    EXPECT_TRUE(system.has_frenzy(entity));
    EXPECT_FLOAT_EQ(system.get_attack_multiplier(entity), 1.5f * 1.2f);
    EXPECT_FLOAT_EQ(system.get_defense_multiplier(entity), 0.7f * 0.9f);

    system.update(1000);

    const auto& attributes = registry.get<CharacterAttributesComponent>(entity);
    EXPECT_EQ(attributes.attack, 170);
    EXPECT_EQ(attributes.defense, 30);
}

TEST(EffectSystemTest, EffectListCacheTracksCategoryAndExpiry) {
    EffectListComponent effects;

    ActiveEffect invisible = MakeEffect(EffectCategory::INVISIBLE);
    invisible.end_time_ms = 500;
    effects.add_effect(invisible);

    EXPECT_TRUE(effects.has_category(EffectCategory::INVISIBLE));
    EXPECT_FALSE(effects.has_expired(499));
    EXPECT_TRUE(effects.has_expired(500));

    effects.remove_expired(500);

    EXPECT_FALSE(effects.has_category(EffectCategory::INVISIBLE));
    EXPECT_FALSE(effects.has_expired(500));
}
