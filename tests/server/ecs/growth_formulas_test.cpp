#include <gtest/gtest.h>

#include "ecs/systems/growth_formulas.h"
#include "common/types/growth_constants.h"

namespace {

using mir2::ecs::GrowthFormulas;
using mir2::ecs::NakedAbility;
using mir2::common::CharacterClass;

// ===== Experience Table Tests =====

TEST(GrowthFormulasTest, ExpTableLevel1) {
    EXPECT_EQ(GrowthFormulas::GetExpForLevel(1), 100);
}

TEST(GrowthFormulasTest, ExpTableLevel10) {
    EXPECT_EQ(GrowthFormulas::GetExpForLevel(10), 6000);
}

TEST(GrowthFormulasTest, ExpTableLevel61) {
    EXPECT_EQ(GrowthFormulas::GetExpForLevel(61), 1500000000LL);
}

TEST(GrowthFormulasTest, ExpTableLevel0ReturnsMax) {
    EXPECT_EQ(GrowthFormulas::GetExpForLevel(0), std::numeric_limits<int64_t>::max());
}

TEST(GrowthFormulasTest, ExpTableLevelOverMaxReturnsMax) {
    EXPECT_EQ(GrowthFormulas::GetExpForLevel(62), std::numeric_limits<int64_t>::max());
}

// ===== Warrior Attribute Tests (Level 35) =====

TEST(GrowthFormulasTest, WarriorLevel35HP) {
    NakedAbility ability = GrowthFormulas::CalcLevelAbility(CharacterClass::WARRIOR, 35);
    // HP = DEFHP + level * (level * 0.18 + 10.0)
    // = 14 + 35 * (35 * 0.18 + 10.0) = 14 + 35 * (6.3 + 10.0) = 14 + 35 * 16.3 = 14 + 570 = 584
    // Approximate — test that it's in the expected range for a warrior
    EXPECT_GT(ability.max_hp, 400);
    EXPECT_LT(ability.max_hp, 700);
}

TEST(GrowthFormulasTest, WarriorLevel35MP) {
    NakedAbility ability = GrowthFormulas::CalcLevelAbility(CharacterClass::WARRIOR, 35);
    // MP is low for warriors
    EXPECT_GT(ability.max_mp, 50);
    EXPECT_LT(ability.max_mp, 150);
}

TEST(GrowthFormulasTest, WarriorLevel35Weight) {
    NakedAbility ability = GrowthFormulas::CalcLevelAbility(CharacterClass::WARRIOR, 35);
    // Warriors have highest weight capacity
    EXPECT_GT(ability.max_weight, 200);
}

// ===== Mage Attribute Tests (Level 35) =====

TEST(GrowthFormulasTest, MageLevel35HP) {
    NakedAbility ability = GrowthFormulas::CalcLevelAbility(CharacterClass::MAGE, 35);
    // Mages have lowest HP
    EXPECT_GT(ability.max_hp, 100);
    EXPECT_LT(ability.max_hp, 300);
}

TEST(GrowthFormulasTest, MageLevel35MP) {
    NakedAbility ability = GrowthFormulas::CalcLevelAbility(CharacterClass::MAGE, 35);
    // Mages have highest MP
    EXPECT_GT(ability.max_mp, 400);
}

TEST(GrowthFormulasTest, MageLevel35MagicAttack) {
    NakedAbility ability = GrowthFormulas::CalcLevelAbility(CharacterClass::MAGE, 35);
    // Mages have highest MC
    EXPECT_GT(ability.mc_hi, 15);
}

TEST(GrowthFormulasTest, MageLevel35Weight) {
    NakedAbility ability = GrowthFormulas::CalcLevelAbility(CharacterClass::MAGE, 35);
    // Mages have lowest weight capacity
    EXPECT_LT(ability.max_weight, 200);
}

// ===== Taoist Attribute Tests (Level 35) =====

TEST(GrowthFormulasTest, TaoistLevel35HP) {
    NakedAbility ability = GrowthFormulas::CalcLevelAbility(CharacterClass::TAOIST, 35);
    // Taoists have mid HP (between warrior and mage)
    EXPECT_GT(ability.max_hp, 150);
    EXPECT_LT(ability.max_hp, 500);
}

TEST(GrowthFormulasTest, TaoistLevel35MP) {
    NakedAbility ability = GrowthFormulas::CalcLevelAbility(CharacterClass::TAOIST, 35);
    // Taoists have mid MP
    EXPECT_GT(ability.max_mp, 150);
}

TEST(GrowthFormulasTest, TaoistLevel35SC) {
    NakedAbility ability = GrowthFormulas::CalcLevelAbility(CharacterClass::TAOIST, 35);
    // Taoists have highest SC
    EXPECT_GT(ability.sc_hi, 15);
}

// ===== Class Attribute Ordering =====

TEST(GrowthFormulasTest, WarriorHasHighestHP) {
    NakedAbility warrior = GrowthFormulas::CalcLevelAbility(CharacterClass::WARRIOR, 35);
    NakedAbility mage = GrowthFormulas::CalcLevelAbility(CharacterClass::MAGE, 35);
    NakedAbility taoist = GrowthFormulas::CalcLevelAbility(CharacterClass::TAOIST, 35);

    EXPECT_GT(warrior.max_hp, taoist.max_hp);
    EXPECT_GT(taoist.max_hp, mage.max_hp);
}

TEST(GrowthFormulasTest, MageHasHighestMP) {
    NakedAbility warrior = GrowthFormulas::CalcLevelAbility(CharacterClass::WARRIOR, 35);
    NakedAbility mage = GrowthFormulas::CalcLevelAbility(CharacterClass::MAGE, 35);
    NakedAbility taoist = GrowthFormulas::CalcLevelAbility(CharacterClass::TAOIST, 35);

    EXPECT_GT(mage.max_mp, taoist.max_mp);
    EXPECT_GT(taoist.max_mp, warrior.max_mp);
}

TEST(GrowthFormulasTest, WarriorHasHighestWeight) {
    NakedAbility warrior = GrowthFormulas::CalcLevelAbility(CharacterClass::WARRIOR, 35);
    NakedAbility mage = GrowthFormulas::CalcLevelAbility(CharacterClass::MAGE, 35);
    NakedAbility taoist = GrowthFormulas::CalcLevelAbility(CharacterClass::TAOIST, 35);

    EXPECT_GT(warrior.max_weight, taoist.max_weight);
    EXPECT_GT(taoist.max_weight, mage.max_weight);
}

// ===== Level Diff Penalty Tests =====

TEST(GrowthFormulasTest, CalcGetExpNoPenaltyWhenLowerLevel) {
    int exp = GrowthFormulas::CalcGetExp(20, 30, 100);
    EXPECT_EQ(exp, 100);
}

TEST(GrowthFormulasTest, CalcGetExpNoPenaltyWhenEqualLevel) {
    int exp = GrowthFormulas::CalcGetExp(30, 30, 100);
    EXPECT_EQ(exp, 100);
}

TEST(GrowthFormulasTest, CalcGetExpPenaltyWhenHigherLevel) {
    int exp = GrowthFormulas::CalcGetExp(40, 20, 100);
    // 20 level difference -> factor = 1.0 - 20*0.05 = 0.0 -> minimum 0.01
    EXPECT_LT(exp, 100);
    EXPECT_GE(exp, 1);
}

TEST(GrowthFormulasTest, CalcGetExpMinimum1) {
    // Huge level difference should still return at least 1
    int exp = GrowthFormulas::CalcGetExp(60, 1, 100);
    EXPECT_GE(exp, 1);
}

TEST(GrowthFormulasTest, CalcGetExpModestPenalty) {
    // 5 level difference -> factor = 1.0 - 5*0.05 = 0.75
    int exp = GrowthFormulas::CalcGetExp(25, 20, 100);
    EXPECT_EQ(exp, 75);
}

// ===== Bonus Point Tests =====

TEST(GrowthFormulasTest, NoBonusPointsBelowLevel21) {
    EXPECT_EQ(GrowthFormulas::GetBonusPoint(CharacterClass::WARRIOR, 1), 0);
    EXPECT_EQ(GrowthFormulas::GetBonusPoint(CharacterClass::WARRIOR, 20), 0);
}

TEST(GrowthFormulasTest, WarriorBonusPointsAtLevel25) {
    // 25 - 20 = 5 levels * 4 points = 20
    EXPECT_EQ(GrowthFormulas::GetBonusPoint(CharacterClass::WARRIOR, 25), 20);
}

TEST(GrowthFormulasTest, MageBonusPointsAtLevel35) {
    // 35 - 20 = 15 levels * 1 point = 15
    EXPECT_EQ(GrowthFormulas::GetBonusPoint(CharacterClass::MAGE, 35), 15);
}

TEST(GrowthFormulasTest, TaoistBonusPointsAtLevel35) {
    EXPECT_EQ(GrowthFormulas::GetBonusPoint(CharacterClass::TAOIST, 35), 15);
}

// ===== Party Exp Bonus Tests =====

TEST(GrowthFormulasTest, PartyExpBonusSolo) {
    EXPECT_DOUBLE_EQ(GrowthFormulas::GetPartyExpBonus(1), 1.0);
}

TEST(GrowthFormulasTest, PartyExpBonus3Members) {
    EXPECT_DOUBLE_EQ(GrowthFormulas::GetPartyExpBonus(3), 1.3);
}

TEST(GrowthFormulasTest, PartyExpBonusMax) {
    EXPECT_DOUBLE_EQ(GrowthFormulas::GetPartyExpBonus(11), 2.1);
}

TEST(GrowthFormulasTest, PartyExpBonusZeroMembers) {
    EXPECT_DOUBLE_EQ(GrowthFormulas::GetPartyExpBonus(0), 0.0);
}

// ===== Level 1 Tests =====

TEST(GrowthFormulasTest, Level1WarriorNotZero) {
    NakedAbility ability = GrowthFormulas::CalcLevelAbility(CharacterClass::WARRIOR, 1);
    EXPECT_GT(ability.max_hp, 0);
    EXPECT_GT(ability.max_mp, 0);
    EXPECT_GT(ability.max_weight, 0);
}

}  // namespace
