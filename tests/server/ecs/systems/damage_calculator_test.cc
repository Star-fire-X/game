/**
 * @file damage_calculator_test.cc
 * @brief Comprehensive tests for DamageCalculator - core combat damage formulas
 *
 * Test Coverage:
 * - Power calculation formulas (get_power, get_random_power, get_power_13)
 * - Attack power with luck-based critical hits
 * - Physical damage calculation
 * - Magic damage calculation (including undead bonus)
 * - Healing calculation
 * - Elemental resistance application
 * - Edge cases: zero values, max values, negative values
 * - Damage minimums (always at least 1)
 *
 * Priority: P0 Ultra-Critical (Score: 48)
 * Risk: Critical - Incorrect damage formulas break game balance
 */

#include <gtest/gtest.h>

#include "ecs/systems/damage_calculator.h"
#include "ecs/components/character_components.h"
#include "ecs/components/skill_template_component.h"

namespace mir2::ecs::test {
namespace {

/**
 * @brief Test fixture for DamageCalculator
 *
 * Provides helper methods to create test characters and skills
 */
class DamageCalculatorTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // Create default test attacker
    attacker_.level = 10;
    attacker_.attack = 100;
    attacker_.magic_attack = 80;
    attacker_.defense = 50;
    attacker_.magic_defense = 40;
    attacker_.luck = 0;
    attacker_.sc = 50;  // 精神力

    // Create default test defender
    defender_.level = 10;
    defender_.attack = 80;
    defender_.magic_attack = 60;
    defender_.defense = 60;
    defender_.magic_defense = 50;
    defender_.luck = 0;
    defender_.sc = 30;

    // Create default test skill
    skill_.id = 1;
    skill_.name = "Test Skill";
    skill_.min_power = 10;
    skill_.max_power = 20;
    skill_.def_power = 5;
    skill_.train_lv = 3;
  }

  /**
   * @brief Create a skill with specific power values
   */
  SkillTemplate CreateSkill(int min_power, int max_power, int def_power, int train_lv) {
    SkillTemplate skill;
    skill.id = 1;
    skill.min_power = min_power;
    skill.max_power = max_power;
    skill.def_power = def_power;
    skill.train_lv = static_cast<uint8_t>(train_lv);
    return skill;
  }

  CharacterAttributesComponent attacker_;
  CharacterAttributesComponent defender_;
  SkillTemplate skill_;
};

// ============================================================================
// Power Calculation Tests
// ============================================================================

/**
 * @brief Test: get_power basic calculation
 * Formula: ROUND(min_power / (train_lv + 1) * (skill_level + 1)) + def_power
 */
TEST_F(DamageCalculatorTest, GetPowerBasicCalculation) {
  // skill_level = 0: 10 / (3 + 1) * (0 + 1) + 5 = 2.5 → 3 + 5 = 8
  int power = DamageCalculator::get_power(skill_, 0);
  EXPECT_EQ(power, 8);

  // skill_level = 1: 10 / (3 + 1) * (1 + 1) + 5 = 5 + 5 = 10
  power = DamageCalculator::get_power(skill_, 1);
  EXPECT_EQ(power, 10);

  // skill_level = 2: 10 / (3 + 1) * (2 + 1) + 5 = 7.5 → 8 + 5 = 13
  power = DamageCalculator::get_power(skill_, 2);
  EXPECT_EQ(power, 13);
}

/**
 * @brief Test: get_power with zero train level
 */
TEST_F(DamageCalculatorTest, GetPowerZeroTrainLevel) {
  skill_.train_lv = 0;
  skill_.min_power = 10;
  skill_.def_power = 5;

  // 10 / (0 + 1) * (2 + 1) + 5 = 30 + 5 = 35
  int power = DamageCalculator::get_power(skill_, 2);
  EXPECT_EQ(power, 35);
}

/**
 * @brief Test: get_power with negative skill level (should be clamped to 0)
 */
TEST_F(DamageCalculatorTest, GetPowerNegativeSkillLevel) {
  // Negative skill levels should be clamped to 0
  int power = DamageCalculator::get_power(skill_, -1);
  EXPECT_GE(power, 0);
}

/**
 * @brief Test: get_random_power returns value in expected range
 */
TEST_F(DamageCalculatorTest, GetRandomPowerInRange) {
  skill_.min_power = 10;
  skill_.max_power = 20;
  skill_.def_power = 5;
  skill_.train_lv = 3;

  // Run multiple times to test randomness
  for (int i = 0; i < 100; ++i) {
    int power = DamageCalculator::get_random_power(skill_, 2);

    // Power should be at least min_scaled + def_power
    // min: 10 / 4 * 3 + 5 = 7.5 → 8 + 5 = 13
    // max: 20 / 4 * 3 + 5 = 15 + 5 = 20
    EXPECT_GE(power, 13);
    EXPECT_LE(power, 20);
  }
}

/**
 * @brief Test: get_random_power with zero range (min == max)
 */
TEST_F(DamageCalculatorTest, GetRandomPowerZeroRange) {
  skill_.min_power = 15;
  skill_.max_power = 15;  // Same as min
  skill_.def_power = 5;
  skill_.train_lv = 0;

  // Should always return the same value
  int power1 = DamageCalculator::get_random_power(skill_, 2);
  int power2 = DamageCalculator::get_random_power(skill_, 2);
  EXPECT_EQ(power1, power2);
}

/**
 * @brief Test: get_power_13 formula (1/3 guarantee + 2/3 scaling)
 * Formula: ROUND((base * 2/3) / (train_lv + 1) * (skill_level + 1) + base / 3)
 */
TEST_F(DamageCalculatorTest, GetPower13Formula) {
  // base=30, skill_level=2, train_level=3
  // (30 * 2/3) / (3 + 1) * (2 + 1) + 30 / 3
  // = (20) / 4 * 3 + 10
  // = 15 + 10 = 25
  int power = DamageCalculator::get_power_13(30, 2, 3);
  EXPECT_EQ(power, 25);
}

/**
 * @brief Test: get_power_13 with zero train level
 */
TEST_F(DamageCalculatorTest, GetPower13ZeroTrainLevel) {
  // base=30, skill_level=2, train_level=0
  // (30 * 2/3) / (0 + 1) * (2 + 1) + 30 / 3
  // = 20 / 1 * 3 + 10 = 60 + 10 = 70
  int power = DamageCalculator::get_power_13(30, 2, 0);
  EXPECT_EQ(power, 70);
}

/**
 * @brief Test: get_power_13 guarantees at least 1/3 of base
 */
TEST_F(DamageCalculatorTest, GetPower13GuaranteesMinimum) {
  int base = 90;
  int power = DamageCalculator::get_power_13(base, 0, 100);  // Very high train level

  // Even with skill_level=0 and very high train_level,
  // power should be at least base/3
  EXPECT_GE(power, base / 3);
}

// ============================================================================
// Attack Power Tests (Luck-based Critical Hits)
// ============================================================================

/**
 * @brief Test: get_attack_power with zero luck (random damage)
 */
TEST_F(DamageCalculatorTest, GetAttackPowerZeroLuck) {
  int base_power = 100;
  int power_range = 20;
  int luck = 0;

  // Run multiple times to ensure it's in range
  for (int i = 0; i < 100; ++i) {
    int attack = DamageCalculator::get_attack_power(base_power, power_range, luck);
    EXPECT_GE(attack, base_power);
    EXPECT_LE(attack, base_power + power_range);
  }
}

/**
 * @brief Test: get_attack_power with high luck (guaranteed max damage)
 */
TEST_F(DamageCalculatorTest, GetAttackPowerHighLuck) {
  int base_power = 100;
  int power_range = 20;
  int luck = 9;  // luck >= roll(0-9) should always trigger max damage

  // With luck=9, should always get max damage
  for (int i = 0; i < 50; ++i) {
    int attack = DamageCalculator::get_attack_power(base_power, power_range, luck);
    EXPECT_EQ(attack, base_power + power_range);
  }
}

/**
 * @brief Test: get_attack_power with zero range
 */
TEST_F(DamageCalculatorTest, GetAttackPowerZeroRange) {
  int base_power = 100;
  int power_range = 0;

  int attack = DamageCalculator::get_attack_power(base_power, power_range, 0);
  EXPECT_EQ(attack, base_power);
}

/**
 * @brief Test: get_attack_power with negative range (should be clamped)
 */
TEST_F(DamageCalculatorTest, GetAttackPowerNegativeRange) {
  int base_power = 100;
  int power_range = -10;  // Invalid, should be clamped to 0

  int attack = DamageCalculator::get_attack_power(base_power, power_range, 0);
  EXPECT_EQ(attack, base_power);
}

// ============================================================================
// Physical Damage Tests
// ============================================================================

/**
 * @brief Test: calculate_physical_damage basic calculation
 */
TEST_F(DamageCalculatorTest, CalculatePhysicalDamageBasic) {
  attacker_.attack = 100;
  attacker_.luck = 0;
  defender_.defense = 50;

  int damage = DamageCalculator::calculate_physical_damage(
      attacker_, defender_, skill_, 0);

  // Damage should be positive
  EXPECT_GT(damage, 0);

  // Damage should be less than attack + skill power
  EXPECT_LT(damage, attacker_.attack + skill_.max_power + skill_.def_power);
}

/**
 * @brief Test: Physical damage respects minimum damage of 1
 */
TEST_F(DamageCalculatorTest, PhysicalDamageMinimumIsOne) {
  // Weak attacker vs strong defender
  attacker_.attack = 10;
  attacker_.luck = 0;
  defender_.defense = 1000;  // Very high defense

  int damage = DamageCalculator::calculate_physical_damage(
      attacker_, defender_, skill_, 0);

  // Should always deal at least 1 damage
  EXPECT_GE(damage, 1);
}

/**
 * @brief Test: Physical damage increases with attacker attack
 */
TEST_F(DamageCalculatorTest, PhysicalDamageScalesWithAttack) {
  defender_.defense = 30;

  attacker_.attack = 50;
  int damage1 = DamageCalculator::calculate_physical_damage(
      attacker_, defender_, skill_, 0);

  attacker_.attack = 100;
  int damage2 = DamageCalculator::calculate_physical_damage(
      attacker_, defender_, skill_, 0);

  // Higher attack should deal more damage
  EXPECT_GT(damage2, damage1);
}

/**
 * @brief Test: Physical damage decreases with defender defense
 */
TEST_F(DamageCalculatorTest, PhysicalDamageReducedByDefense) {
  attacker_.attack = 100;

  defender_.defense = 20;
  int damage1 = DamageCalculator::calculate_physical_damage(
      attacker_, defender_, skill_, 0);

  defender_.defense = 60;
  int damage2 = DamageCalculator::calculate_physical_damage(
      attacker_, defender_, skill_, 0);

  // Higher defense should reduce damage
  EXPECT_LT(damage2, damage1);
}

/**
 * @brief Test: Physical damage increases with skill level
 */
TEST_F(DamageCalculatorTest, PhysicalDamageScalesWithSkillLevel) {
  attacker_.attack = 100;
  defender_.defense = 40;

  int damage_lv0 = DamageCalculator::calculate_physical_damage(
      attacker_, defender_, skill_, 0);

  int damage_lv3 = DamageCalculator::calculate_physical_damage(
      attacker_, defender_, skill_, 3);

  // Higher skill level should deal more damage
  EXPECT_GT(damage_lv3, damage_lv0);
}

/**
 * @brief Test: Luck affects physical damage (critical hits)
 */
TEST_F(DamageCalculatorTest, LuckAffectsPhysicalDamage) {
  attacker_.attack = 100;
  defender_.defense = 40;

  // No luck - variable damage
  attacker_.luck = 0;
  std::vector<int> damages_no_luck;
  for (int i = 0; i < 20; ++i) {
    damages_no_luck.push_back(DamageCalculator::calculate_physical_damage(
        attacker_, defender_, skill_, 2));
  }

  // High luck - should consistently deal max damage
  attacker_.luck = 9;
  std::vector<int> damages_high_luck;
  for (int i = 0; i < 20; ++i) {
    damages_high_luck.push_back(DamageCalculator::calculate_physical_damage(
        attacker_, defender_, skill_, 2));
  }

  // High luck should produce more consistent (max) damage
  int max_damage_no_luck = *std::max_element(damages_no_luck.begin(), damages_no_luck.end());
  int min_damage_high_luck = *std::min_element(damages_high_luck.begin(), damages_high_luck.end());

  EXPECT_GE(min_damage_high_luck, max_damage_no_luck * 0.9);
}

// ============================================================================
// Magic Damage Tests
// ============================================================================

/**
 * @brief Test: calculate_magic_damage basic calculation
 */
TEST_F(DamageCalculatorTest, CalculateMagicDamageBasic) {
  attacker_.magic_attack = 80;
  defender_.magic_defense = 30;

  int damage = DamageCalculator::calculate_magic_damage(
      attacker_, defender_, skill_, 0, false);

  EXPECT_GT(damage, 0);
}

/**
 * @brief Test: Magic damage respects minimum damage of 1
 */
TEST_F(DamageCalculatorTest, MagicDamageMinimumIsOne) {
  attacker_.magic_attack = 10;
  defender_.magic_defense = 1000;  // Very high magic defense

  int damage = DamageCalculator::calculate_magic_damage(
      attacker_, defender_, skill_, 0, false);

  EXPECT_GE(damage, 1);
}

/**
 * @brief Test: Magic damage against undead has 1.5x multiplier
 */
TEST_F(DamageCalculatorTest, MagicDamageUndeadBonus) {
  attacker_.magic_attack = 100;
  defender_.magic_defense = 20;

  // Normal target
  int damage_normal = DamageCalculator::calculate_magic_damage(
      attacker_, defender_, skill_, 2, false);

  // Undead target (1.5x multiplier)
  int damage_undead = DamageCalculator::calculate_magic_damage(
      attacker_, defender_, skill_, 2, true);

  // Undead damage should be approximately 1.5x normal damage
  EXPECT_GT(damage_undead, damage_normal);
  EXPECT_NEAR(damage_undead, damage_normal * 1.5, damage_normal * 0.2);
}

/**
 * @brief Test: Magic damage scales with skill level
 */
TEST_F(DamageCalculatorTest, MagicDamageScalesWithSkillLevel) {
  attacker_.magic_attack = 80;
  defender_.magic_defense = 30;

  int damage_lv0 = DamageCalculator::calculate_magic_damage(
      attacker_, defender_, skill_, 0, false);

  int damage_lv3 = DamageCalculator::calculate_magic_damage(
      attacker_, defender_, skill_, 3, false);

  EXPECT_GT(damage_lv3, damage_lv0);
}

/**
 * @brief Test: Magic damage scales with magic attack
 */
TEST_F(DamageCalculatorTest, MagicDamageScalesWithMagicAttack) {
  defender_.magic_defense = 30;

  attacker_.magic_attack = 50;
  int damage1 = DamageCalculator::calculate_magic_damage(
      attacker_, defender_, skill_, 2, false);

  attacker_.magic_attack = 120;
  int damage2 = DamageCalculator::calculate_magic_damage(
      attacker_, defender_, skill_, 2, false);

  EXPECT_GT(damage2, damage1);
}

/**
 * @brief Test: Magic damage reduced by magic defense
 */
TEST_F(DamageCalculatorTest, MagicDamageReducedByMagicDefense) {
  attacker_.magic_attack = 100;

  defender_.magic_defense = 20;
  int damage1 = DamageCalculator::calculate_magic_damage(
      attacker_, defender_, skill_, 2, false);

  defender_.magic_defense = 70;
  int damage2 = DamageCalculator::calculate_magic_damage(
      attacker_, defender_, skill_, 2, false);

  EXPECT_LT(damage2, damage1);
}

// ============================================================================
// Healing Tests
// ============================================================================

/**
 * @brief Test: calculate_healing basic calculation
 */
TEST_F(DamageCalculatorTest, CalculateHealingBasic) {
  attacker_.sc = 50;  // 精神力

  int healing = DamageCalculator::calculate_healing(attacker_, skill_, 2);
  EXPECT_GT(healing, 0);
}

/**
 * @brief Test: Healing scales with SC (精神力)
 */
TEST_F(DamageCalculatorTest, HealingScalesWithSC) {
  attacker_.sc = 20;
  int healing1 = DamageCalculator::calculate_healing(attacker_, skill_, 2);

  attacker_.sc = 100;
  int healing2 = DamageCalculator::calculate_healing(attacker_, skill_, 2);

  // Higher SC should produce more healing
  EXPECT_GT(healing2, healing1);
}

/**
 * @brief Test: Healing scales with skill level
 */
TEST_F(DamageCalculatorTest, HealingScalesWithSkillLevel) {
  attacker_.sc = 50;

  int healing_lv0 = DamageCalculator::calculate_healing(attacker_, skill_, 0);
  int healing_lv3 = DamageCalculator::calculate_healing(attacker_, skill_, 3);

  EXPECT_GT(healing_lv3, healing_lv0);
}

/**
 * @brief Test: Healing with zero SC still works
 */
TEST_F(DamageCalculatorTest, HealingWithZeroSC) {
  attacker_.sc = 0;

  int healing = DamageCalculator::calculate_healing(attacker_, skill_, 2);

  // Should still heal based on skill power
  EXPECT_GT(healing, 0);
}

// ============================================================================
// Elemental Resistance Tests
// ============================================================================

/**
 * @brief Test: apply_elemental_resistance basic reduction
 */
TEST_F(DamageCalculatorTest, ElementalResistanceBasicReduction) {
  int damage = 100;
  int resistance = 50;  // 50% resistance

  int reduced = DamageCalculator::apply_elemental_resistance(damage, resistance);

  // 100 * (100 - 50) / 100 = 50
  EXPECT_EQ(reduced, 50);
}

/**
 * @brief Test: Elemental resistance with 0% resistance
 */
TEST_F(DamageCalculatorTest, ElementalResistanceZeroResistance) {
  int damage = 100;
  int resistance = 0;

  int reduced = DamageCalculator::apply_elemental_resistance(damage, resistance);
  EXPECT_EQ(reduced, damage);
}

/**
 * @brief Test: Elemental resistance with 100% resistance (capped)
 */
TEST_F(DamageCalculatorTest, ElementalResistance100Percent) {
  int damage = 100;
  int resistance = 100;

  int reduced = DamageCalculator::apply_elemental_resistance(damage, resistance);

  // Even with 100% resistance, minimum damage is 1
  EXPECT_EQ(reduced, 1);
}

/**
 * @brief Test: Elemental resistance clamped at 100% (no healing from damage)
 */
TEST_F(DamageCalculatorTest, ElementalResistanceClampedAt100) {
  int damage = 100;
  int resistance = 150;  // Over 100%, should be clamped to 100%

  int reduced = DamageCalculator::apply_elemental_resistance(damage, resistance);

  // Should be same as 100% resistance (minimum 1 damage)
  EXPECT_EQ(reduced, 1);
}

/**
 * @brief Test: Elemental resistance with negative resistance (no amplification)
 */
TEST_F(DamageCalculatorTest, ElementalResistanceNegative) {
  int damage = 100;
  int resistance = -50;  // Negative resistance should be clamped to 0

  int reduced = DamageCalculator::apply_elemental_resistance(damage, resistance);

  // Negative resistance clamped to 0, so full damage
  EXPECT_EQ(reduced, damage);
}

/**
 * @brief Test: Elemental resistance always returns at least 1 damage
 */
TEST_F(DamageCalculatorTest, ElementalResistanceMinimumDamage) {
  int damage = 5;
  int resistance = 99;  // 99% resistance

  int reduced = DamageCalculator::apply_elemental_resistance(damage, resistance);

  // 5 * (100 - 99) / 100 = 0.05, but should round to 1
  EXPECT_GE(reduced, 1);
}

/**
 * @brief Test: Elemental resistance with zero damage
 */
TEST_F(DamageCalculatorTest, ElementalResistanceZeroDamage) {
  int damage = 0;
  int resistance = 50;

  int reduced = DamageCalculator::apply_elemental_resistance(damage, resistance);
  EXPECT_EQ(reduced, 0);
}

/**
 * @brief Test: Elemental resistance with negative damage
 */
TEST_F(DamageCalculatorTest, ElementalResistanceNegativeDamage) {
  int damage = -10;
  int resistance = 50;

  int reduced = DamageCalculator::apply_elemental_resistance(damage, resistance);
  EXPECT_EQ(reduced, 0);
}

/**
 * @brief Test: Various resistance percentages
 */
TEST_F(DamageCalculatorTest, ElementalResistanceVariousPercentages) {
  int damage = 200;

  // 25% resistance: 200 * 0.75 = 150
  EXPECT_EQ(DamageCalculator::apply_elemental_resistance(damage, 25), 150);

  // 50% resistance: 200 * 0.50 = 100
  EXPECT_EQ(DamageCalculator::apply_elemental_resistance(damage, 50), 100);

  // 75% resistance: 200 * 0.25 = 50
  EXPECT_EQ(DamageCalculator::apply_elemental_resistance(damage, 75), 50);

  // 90% resistance: 200 * 0.10 = 20
  EXPECT_EQ(DamageCalculator::apply_elemental_resistance(damage, 90), 20);
}

// ============================================================================
// Edge Case Tests
// ============================================================================

/**
 * @brief Test: All damage calculations with maximum integer values
 */
TEST_F(DamageCalculatorTest, MaximumIntegerValues) {
  // This tests for integer overflow/underflow
  attacker_.attack = INT_MAX / 1000;
  attacker_.magic_attack = INT_MAX / 1000;
  defender_.defense = 0;
  defender_.magic_defense = 0;

  // Should not crash or produce negative damage
  int phys_damage = DamageCalculator::calculate_physical_damage(
      attacker_, defender_, skill_, 3);
  EXPECT_GT(phys_damage, 0);

  int magic_damage = DamageCalculator::calculate_magic_damage(
      attacker_, defender_, skill_, 3, false);
  EXPECT_GT(magic_damage, 0);
}

/**
 * @brief Test: Skill with zero power values
 */
TEST_F(DamageCalculatorTest, SkillWithZeroPower) {
  skill_.min_power = 0;
  skill_.max_power = 0;
  skill_.def_power = 0;

  attacker_.attack = 50;
  defender_.defense = 20;

  int damage = DamageCalculator::calculate_physical_damage(
      attacker_, defender_, skill_, 0);

  // Should still deal damage based on base attack
  EXPECT_GT(damage, 0);
}

/**
 * @brief Test: Concurrent damage calculations (thread safety)
 */
TEST_F(DamageCalculatorTest, ConcurrentCalculations) {
  // DamageCalculator uses thread_local random, should be thread-safe
  std::vector<std::thread> threads;
  std::atomic<int> total_damage{0};

  for (int i = 0; i < 4; ++i) {
    threads.emplace_back([this, &total_damage]() {
      for (int j = 0; j < 100; ++j) {
        int damage = DamageCalculator::calculate_physical_damage(
            attacker_, defender_, skill_, 2);
        total_damage.fetch_add(damage, std::memory_order_relaxed);
      }
    });
  }

  for (auto& thread : threads) {
    thread.join();
  }

  // All calculations should complete without crashes
  EXPECT_GT(total_damage.load(), 0);
}

}  // namespace
}  // namespace mir2::ecs::test
