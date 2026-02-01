#include <gtest/gtest.h>

#include "common/enums.h"
#include "server/combat/combat_core.h"

namespace {

TEST(DamageCalculatorTest, MissRollReturnsMiss) {
    legend2::CombatConfig config;
    legend2::combat::DamageInput input{10, 0, 0.0f, 1.0f};
    legend2::combat::DamageRolls rolls;
    rolls.miss_roll = 0.0f;

    auto result = legend2::combat::DamageCalculator::calculate(input, config, rolls);

    EXPECT_TRUE(result.is_miss);
    EXPECT_EQ(result.final_damage, 0);
}

TEST(DamageCalculatorTest, CriticalHitAppliesMultiplier) {
    legend2::CombatConfig config;
    config.critical_multiplier = 2.0f;

    legend2::combat::DamageInput input{10, 2, 1.0f, 0.0f};
    legend2::combat::DamageRolls rolls;
    rolls.critical_roll = 0.0f;
    rolls.miss_roll = 1.0f;

    auto result = legend2::combat::DamageCalculator::calculate(input, config, rolls);

    EXPECT_FALSE(result.is_miss);
    EXPECT_TRUE(result.is_critical);
    EXPECT_EQ(result.final_damage, 16);
}

TEST(DamageCalculatorTest, VarianceIsAppliedWithinRange) {
    legend2::CombatConfig config;
    config.min_variance_percent = -10;
    config.max_variance_percent = 10;

    legend2::combat::DamageInput input{100, 0, 0.0f, 0.0f};
    legend2::combat::DamageRolls rolls;
    rolls.variance = -5;
    rolls.critical_roll = 1.0f;
    rolls.miss_roll = 1.0f;

    auto result = legend2::combat::DamageCalculator::calculate(input, config, rolls);

    EXPECT_EQ(result.variance, -5);
    EXPECT_EQ(result.final_damage, 95);
}

TEST(DamageCalculatorTest, MinimumDamageApplied) {
    legend2::CombatConfig config;
    config.minimum_damage = 1;

    legend2::combat::DamageInput input{1, 10, 0.0f, 0.0f};
    legend2::combat::DamageRolls rolls;
    rolls.critical_roll = 1.0f;
    rolls.miss_roll = 1.0f;

    auto result = legend2::combat::DamageCalculator::calculate(input, config, rolls);

    EXPECT_FALSE(result.is_miss);
    EXPECT_EQ(result.final_damage, 1);
}

TEST(DamageCalculatorTest, ChanceValuesAreClamped) {
    legend2::CombatConfig config;

    legend2::combat::DamageInput input{10, 0, 2.0f, -1.0f};
    legend2::combat::DamageRolls rolls;
    rolls.critical_roll = 0.5f;
    rolls.miss_roll = 0.0f;

    auto result = legend2::combat::DamageCalculator::calculate(input, config, rolls);

    EXPECT_TRUE(result.is_critical);
    EXPECT_FALSE(result.is_miss);
}

TEST(RangeCheckerTest, InRangeIncludesBoundary) {
    const mir2::common::Position attacker{0, 0};
    const mir2::common::Position target{1, 0};

    EXPECT_TRUE(legend2::combat::RangeChecker::is_in_range(attacker, target, 1));
}

TEST(RangeCheckerTest, OutOfRange) {
    const mir2::common::Position attacker{0, 0};
    const mir2::common::Position target{2, 0};

    EXPECT_FALSE(legend2::combat::RangeChecker::is_in_range(attacker, target, 1));
}

TEST(RangeCheckerTest, DistanceSquaredMatchesPythagoras) {
    const mir2::common::Position a{0, 0};
    const mir2::common::Position b{3, 4};

    EXPECT_EQ(legend2::combat::RangeChecker::distance_squared(a, b), 25);
}

TEST(LootGeneratorTest, AlwaysDropWithFixedQuantity) {
    legend2::combat::LootTable table;
    table.entries.push_back({42, 2, 2, 1.0f});

    legend2::combat::CombatRandom random(1);
    const mir2::common::Position drop_pos{5, 7};
    auto drops = legend2::combat::LootGenerator::generate(table, drop_pos, random);

    ASSERT_EQ(drops.size(), 1u);
    EXPECT_EQ(drops[0].item_template_id, 42u);
    EXPECT_EQ(drops[0].quantity, 2);
    EXPECT_EQ(drops[0].position, drop_pos);
}

TEST(LootGeneratorTest, NeverDropWhenRateIsZero) {
    legend2::combat::LootTable table;
    table.entries.push_back({7, 1, 3, 0.0f});

    legend2::combat::CombatRandom random(2);
    const mir2::common::Position drop_pos{1, 1};
    auto drops = legend2::combat::LootGenerator::generate(table, drop_pos, random);

    EXPECT_TRUE(drops.empty());
}

TEST(LootGeneratorTest, QuantityWithinConfiguredRange) {
    legend2::combat::LootTable table;
    table.entries.push_back({9, 1, 3, 1.0f});

    legend2::combat::CombatRandom random(123);
    const mir2::common::Position drop_pos{0, 0};
    auto drops = legend2::combat::LootGenerator::generate(table, drop_pos, random);

    ASSERT_EQ(drops.size(), 1u);
    EXPECT_GE(drops[0].quantity, 1);
    EXPECT_LE(drops[0].quantity, 3);
}

// ============================================================================
// AttackType Modifier Tests
// ============================================================================

TEST(AttackTypeModifierTest, HitReturnsDefaultModifier) {
    auto modifier = legend2::combat::get_attack_modifier(mir2::common::AttackType::kHit);
    EXPECT_FLOAT_EQ(modifier.damage_multiplier, 1.0f);
    EXPECT_EQ(modifier.range, 1);
    EXPECT_EQ(modifier.hit_count, 1);
    EXPECT_FALSE(modifier.is_aoe);
}

TEST(AttackTypeModifierTest, HeavyHitHas150PercentDamage) {
    auto modifier = legend2::combat::get_attack_modifier(mir2::common::AttackType::kHeavyHit);
    EXPECT_FLOAT_EQ(modifier.damage_multiplier, 1.5f);
}

TEST(AttackTypeModifierTest, BigHitUsesHitPlusMultiplier) {
    auto modifier = legend2::combat::get_attack_modifier(mir2::common::AttackType::kBigHit);
    EXPECT_EQ(modifier.hit_plus_multiplier, 1);
}

TEST(AttackTypeModifierTest, PowerHitHas200PercentDamage) {
    auto modifier = legend2::combat::get_attack_modifier(mir2::common::AttackType::kPowerHit);
    EXPECT_FLOAT_EQ(modifier.damage_multiplier, 2.0f);
}

TEST(AttackTypeModifierTest, LongHitHasExtendedRange) {
    auto modifier = legend2::combat::get_attack_modifier(mir2::common::AttackType::kLongHit);
    EXPECT_EQ(modifier.range, 3);
}

TEST(AttackTypeModifierTest, TwnHitHasTwoHits) {
    auto modifier = legend2::combat::get_attack_modifier(mir2::common::AttackType::kTwnHit);
    EXPECT_EQ(modifier.hit_count, 2);
}

TEST(AttackTypeModifierTest, WideHitIsAoeWithRadius1) {
    auto modifier = legend2::combat::get_attack_modifier(mir2::common::AttackType::kWideHit);
    EXPECT_TRUE(modifier.is_aoe);
    EXPECT_EQ(modifier.aoe_radius, 1);
}

TEST(AttackTypeModifierTest, FireHitHasFireDamageBonus) {
    auto modifier = legend2::combat::get_attack_modifier(mir2::common::AttackType::kFireHit);
    EXPECT_EQ(modifier.fire_damage_bonus, 20);
}

// ============================================================================
// apply_attack_modifier Tests
// ============================================================================

TEST(ApplyAttackModifierTest, DefaultModifierReturnsBaseDamage) {
    legend2::combat::AttackTypeModifier modifier;
    int result = legend2::combat::apply_attack_modifier(100, modifier);
    EXPECT_EQ(result, 100);
}

TEST(ApplyAttackModifierTest, DamageMultiplierApplied) {
    legend2::combat::AttackTypeModifier modifier;
    modifier.damage_multiplier = 1.5f;
    int result = legend2::combat::apply_attack_modifier(100, modifier);
    EXPECT_EQ(result, 150);
}

TEST(ApplyAttackModifierTest, HitPlusAdded) {
    legend2::combat::AttackTypeModifier modifier;
    modifier.hit_plus_multiplier = 1;
    int result = legend2::combat::apply_attack_modifier(100, modifier, 25);
    EXPECT_EQ(result, 125);
}

TEST(ApplyAttackModifierTest, FireDamageBonusAdded) {
    legend2::combat::AttackTypeModifier modifier;
    modifier.fire_damage_bonus = 20;
    int result = legend2::combat::apply_attack_modifier(100, modifier);
    EXPECT_EQ(result, 120);
}

TEST(ApplyAttackModifierTest, NegativeResultClampedToZero) {
    legend2::combat::AttackTypeModifier modifier;
    modifier.damage_multiplier = 0.0f;
    int result = legend2::combat::apply_attack_modifier(100, modifier);
    EXPECT_EQ(result, 0);
}

TEST(ApplyAttackModifierTest, CombinedModifiers) {
    legend2::combat::AttackTypeModifier modifier;
    modifier.damage_multiplier = 2.0f;
    modifier.hit_plus_multiplier = 1;
    modifier.fire_damage_bonus = 10;
    int result = legend2::combat::apply_attack_modifier(50, modifier, 20);
    // 50 * 2.0 + 20 * 1 + 10 = 130
    EXPECT_EQ(result, 130);
}

}  // namespace
