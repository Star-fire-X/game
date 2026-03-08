/**
 * @file passive_skill_system_test.cc
 * @brief Comprehensive tests for PassiveSkillSystem - P0 High-Critical passive skill bonuses
 *
 * Test Coverage:
 * - get_passive_bonuses on entity with no AttributeModifiers -> zero
 * - get_passive_bonuses on invalid entity -> zero
 * - trigger_on_attack delegates to get_passive_bonuses
 * - recalculate_passives on invalid entity -> no crash
 * - recalculate_passives on entity with no SkillListComponent -> zero modifiers
 * - recalculate_passives with registered passive skills at various levels
 * - Specific passive skill bonus formulas: ONESWORD, BODYGUARD, BLOODLUST,
 *   WUJIZHENQI, TAOIST_WUJI, GHOSTSHIELD, DEJIWONHO
 * - Level clamping to max 3
 * - Multiple passive skills stacking
 * - Non-passive skills in SkillListComponent are skipped
 * - Empty optional slots in SkillListComponent are skipped
 * - Recalculate overwrites previous cached results
 * - All seven passives combined
 * - Recalculate after skill removal yields zero
 * - Entity destroyed between recalculate and query
 * - Unknown skill IDs produce zero bonuses
 * - Zero-level skills produce zero bonuses
 * - Mixed passive and active skill lists
 *
 * Priority: P0 High-Critical
 * Risk: Incorrect passive bonuses silently distort combat balance
 */

#include <gtest/gtest.h>
#include "ecs/systems/passive_skill_system.h"
#include "ecs/components/skill_component.h"
#include "ecs/components/character_components.h"
#include "ecs/skill_registry.h"
#include "common/skill_ids.h"
#include <entt/entt.hpp>

namespace mir2::ecs::test {
namespace {

// ============================================================================
// Helper: Register skill templates in the global SkillRegistry
// ============================================================================

static void RegisterPassiveSkill(uint32_t skill_id, const std::string& name) {
    SkillTemplate tmpl;
    tmpl.id = skill_id;
    tmpl.name = name;
    tmpl.is_passive = true;
    SkillRegistry::instance().register_skill(std::move(tmpl));
}

static void RegisterNonPassiveSkill(uint32_t skill_id, const std::string& name) {
    SkillTemplate tmpl;
    tmpl.id = skill_id;
    tmpl.name = name;
    tmpl.is_passive = false;
    SkillRegistry::instance().register_skill(std::move(tmpl));
}

// ============================================================================
// Helper: Assert all fields of AttributeModifiers are zero
// ============================================================================

static void ExpectAllZero(const AttributeModifiers& mods) {
    EXPECT_EQ(mods.attack_bonus, 0);
    EXPECT_EQ(mods.defense_bonus, 0);
    EXPECT_EQ(mods.magic_attack_bonus, 0);
    EXPECT_EQ(mods.magic_defense_bonus, 0);
    EXPECT_EQ(mods.hit_rate_bonus, 0);
    EXPECT_EQ(mods.dodge_bonus, 0);
    EXPECT_EQ(mods.speed_bonus, 0);
    EXPECT_FLOAT_EQ(mods.critical_bonus, 0.0f);
}

// ============================================================================
// Test Fixture
// ============================================================================

class PassiveSkillSystemTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Clear global singleton to ensure test isolation.
        SkillRegistry::instance().clear();

        // Register all 7 known passive skills used by PassiveSkillSystem.
        RegisterPassiveSkill(mir2::common::SkillId::ONESWORD, "ONESWORD");
        RegisterPassiveSkill(mir2::common::SkillId::BODYGUARD, "BODYGUARD");
        RegisterPassiveSkill(mir2::common::SkillId::BLOODLUST, "BLOODLUST");
        RegisterPassiveSkill(mir2::common::SkillId::WUJIZHENQI, "WUJIZHENQI");
        RegisterPassiveSkill(mir2::common::SkillId::TAOIST_WUJI, "TAOIST_WUJI");
        RegisterPassiveSkill(mir2::common::SkillId::GHOSTSHIELD, "GHOSTSHIELD");
        RegisterPassiveSkill(mir2::common::SkillId::DEJIWONHO, "DEJIWONHO");

        system_ = std::make_unique<PassiveSkillSystem>(registry_);
    }

    void TearDown() override {
        // Restore clean singleton state for other tests in the same process.
        SkillRegistry::instance().clear();
    }

    /**
     * @brief Create an entity with a SkillListComponent, add one learned skill.
     * @return The created entity.
     */
    entt::entity CreateEntityWithSkill(uint32_t skill_id, uint8_t level) {
        auto entity = registry_.create();
        auto& skill_list = registry_.emplace<SkillListComponent>(entity);
        skill_list.add_skill(skill_id);
        LearnedSkill* ls = skill_list.get_skill(skill_id);
        if (ls) {
            ls->level = level;
        }
        return entity;
    }

    /**
     * @brief Create an entity with a SkillListComponent containing multiple
     *        learned skills at given levels.
     * @param skills Vector of (skill_id, level) pairs.
     * @return The created entity.
     */
    entt::entity CreateEntityWithSkills(
        const std::vector<std::pair<uint32_t, uint8_t>>& skills) {
        auto entity = registry_.create();
        auto& skill_list = registry_.emplace<SkillListComponent>(entity);
        for (const auto& [id, level] : skills) {
            skill_list.add_skill(id);
            LearnedSkill* ls = skill_list.get_skill(id);
            if (ls) {
                ls->level = level;
            }
        }
        return entity;
    }

    /**
     * @brief Create a bare entity with no components.
     */
    entt::entity CreateBareEntity() {
        return registry_.create();
    }

    entt::registry registry_;
    std::unique_ptr<PassiveSkillSystem> system_;
};

// ============================================================================
// 1. InvalidEntity - recalculate_passives with invalid entity does nothing
// ============================================================================

TEST_F(PassiveSkillSystemTest, RecalculatePassivesInvalidEntityNoCrash) {
    EXPECT_NO_FATAL_FAILURE(system_->recalculate_passives(entt::null));
}

TEST_F(PassiveSkillSystemTest, RecalculatePassivesDestroyedEntityNoCrash) {
    auto entity = CreateBareEntity();
    registry_.destroy(entity);
    EXPECT_NO_FATAL_FAILURE(system_->recalculate_passives(entity));
}

// ============================================================================
// 2. NoSkillComponent - entity exists but no SkillListComponent
// ============================================================================

TEST_F(PassiveSkillSystemTest, RecalculateNoSkillListStoresZeroModifiers) {
    auto entity = CreateBareEntity();
    system_->recalculate_passives(entity);

    const auto* mods = registry_.try_get<AttributeModifiers>(entity);
    ASSERT_NE(mods, nullptr);
    ExpectAllZero(*mods);
}

// ============================================================================
// 3. EmptySkillList - entity has SkillListComponent but all slots empty
// ============================================================================

TEST_F(PassiveSkillSystemTest, RecalculateEmptySkillListStoresZeroModifiers) {
    auto entity = registry_.create();
    registry_.emplace<SkillListComponent>(entity);  // Empty list.

    system_->recalculate_passives(entity);

    const auto* mods = registry_.try_get<AttributeModifiers>(entity);
    ASSERT_NE(mods, nullptr);
    ExpectAllZero(*mods);
}

// ============================================================================
// 4. SinglePassive_OneSword - ONESWORD at levels 1,2,3
//    hit_rate_bonus = clamped_level * 1
// ============================================================================

TEST_F(PassiveSkillSystemTest, OneswordLevel1HitRate) {
    auto entity = CreateEntityWithSkill(mir2::common::SkillId::ONESWORD, 1);
    system_->recalculate_passives(entity);

    const auto mods = system_->get_passive_bonuses(entity);
    EXPECT_EQ(mods.hit_rate_bonus, 1);  // 1 * 1
    EXPECT_EQ(mods.attack_bonus, 0);
    EXPECT_EQ(mods.defense_bonus, 0);
    EXPECT_EQ(mods.magic_defense_bonus, 0);
}

TEST_F(PassiveSkillSystemTest, OneswordLevel2HitRate) {
    auto entity = CreateEntityWithSkill(mir2::common::SkillId::ONESWORD, 2);
    system_->recalculate_passives(entity);

    const auto mods = system_->get_passive_bonuses(entity);
    EXPECT_EQ(mods.hit_rate_bonus, 2);  // 2 * 1
}

TEST_F(PassiveSkillSystemTest, OneswordLevel3HitRate) {
    auto entity = CreateEntityWithSkill(mir2::common::SkillId::ONESWORD, 3);
    system_->recalculate_passives(entity);

    const auto mods = system_->get_passive_bonuses(entity);
    EXPECT_EQ(mods.hit_rate_bonus, 3);  // 3 * 1
}

// ============================================================================
// 5. SinglePassive_BodyGuard - BODYGUARD at levels 1,2,3
//    defense_bonus = clamped_level * 5
// ============================================================================

TEST_F(PassiveSkillSystemTest, BodyguardLevel1Defense) {
    auto entity = CreateEntityWithSkill(mir2::common::SkillId::BODYGUARD, 1);
    system_->recalculate_passives(entity);

    const auto mods = system_->get_passive_bonuses(entity);
    EXPECT_EQ(mods.defense_bonus, 5);  // 1 * 5
    EXPECT_EQ(mods.attack_bonus, 0);
}

TEST_F(PassiveSkillSystemTest, BodyguardLevel2Defense) {
    auto entity = CreateEntityWithSkill(mir2::common::SkillId::BODYGUARD, 2);
    system_->recalculate_passives(entity);

    const auto mods = system_->get_passive_bonuses(entity);
    EXPECT_EQ(mods.defense_bonus, 10);  // 2 * 5
}

TEST_F(PassiveSkillSystemTest, BodyguardLevel3Defense) {
    auto entity = CreateEntityWithSkill(mir2::common::SkillId::BODYGUARD, 3);
    system_->recalculate_passives(entity);

    const auto mods = system_->get_passive_bonuses(entity);
    EXPECT_EQ(mods.defense_bonus, 15);  // 3 * 5
}

// ============================================================================
// 6. SinglePassive_BloodLust - BLOODLUST at levels 1,2,3
//    attack_bonus = clamped_level * 3
// ============================================================================

TEST_F(PassiveSkillSystemTest, BloodlustLevel1Attack) {
    auto entity = CreateEntityWithSkill(mir2::common::SkillId::BLOODLUST, 1);
    system_->recalculate_passives(entity);

    const auto mods = system_->get_passive_bonuses(entity);
    EXPECT_EQ(mods.attack_bonus, 3);  // 1 * 3
    EXPECT_EQ(mods.defense_bonus, 0);
}

TEST_F(PassiveSkillSystemTest, BloodlustLevel2Attack) {
    auto entity = CreateEntityWithSkill(mir2::common::SkillId::BLOODLUST, 2);
    system_->recalculate_passives(entity);

    const auto mods = system_->get_passive_bonuses(entity);
    EXPECT_EQ(mods.attack_bonus, 6);  // 2 * 3
}

TEST_F(PassiveSkillSystemTest, BloodlustLevel3Attack) {
    auto entity = CreateEntityWithSkill(mir2::common::SkillId::BLOODLUST, 3);
    system_->recalculate_passives(entity);

    const auto mods = system_->get_passive_bonuses(entity);
    EXPECT_EQ(mods.attack_bonus, 9);  // 3 * 3
}

// ============================================================================
// 7. SinglePassive_WujiZhenQi - WUJIZHENQI gives attack+defense bonuses
//    attack_bonus = clamped_level * 2, defense_bonus = clamped_level * 2
// ============================================================================

TEST_F(PassiveSkillSystemTest, WujizhenqiLevel1AttackAndDefense) {
    auto entity = CreateEntityWithSkill(mir2::common::SkillId::WUJIZHENQI, 1);
    system_->recalculate_passives(entity);

    const auto mods = system_->get_passive_bonuses(entity);
    EXPECT_EQ(mods.attack_bonus, 2);   // 1 * 2
    EXPECT_EQ(mods.defense_bonus, 2);  // 1 * 2
    EXPECT_EQ(mods.hit_rate_bonus, 0);
    EXPECT_EQ(mods.magic_defense_bonus, 0);
}

TEST_F(PassiveSkillSystemTest, WujizhenqiLevel2AttackAndDefense) {
    auto entity = CreateEntityWithSkill(mir2::common::SkillId::WUJIZHENQI, 2);
    system_->recalculate_passives(entity);

    const auto mods = system_->get_passive_bonuses(entity);
    EXPECT_EQ(mods.attack_bonus, 4);   // 2 * 2
    EXPECT_EQ(mods.defense_bonus, 4);  // 2 * 2
}

TEST_F(PassiveSkillSystemTest, WujizhenqiLevel3AttackAndDefense) {
    auto entity = CreateEntityWithSkill(mir2::common::SkillId::WUJIZHENQI, 3);
    system_->recalculate_passives(entity);

    const auto mods = system_->get_passive_bonuses(entity);
    EXPECT_EQ(mods.attack_bonus, 6);   // 3 * 2
    EXPECT_EQ(mods.defense_bonus, 6);  // 3 * 2
}

// ============================================================================
// 8. SinglePassive_TaoistWuji - TAOIST_WUJI gives same bonuses as WUJIZHENQI
// ============================================================================

TEST_F(PassiveSkillSystemTest, TaoistWujiLevel1AttackAndDefense) {
    auto entity = CreateEntityWithSkill(mir2::common::SkillId::TAOIST_WUJI, 1);
    system_->recalculate_passives(entity);

    const auto mods = system_->get_passive_bonuses(entity);
    EXPECT_EQ(mods.attack_bonus, 2);   // 1 * 2
    EXPECT_EQ(mods.defense_bonus, 2);  // 1 * 2
}

TEST_F(PassiveSkillSystemTest, TaoistWujiLevel2AttackAndDefense) {
    auto entity = CreateEntityWithSkill(mir2::common::SkillId::TAOIST_WUJI, 2);
    system_->recalculate_passives(entity);

    const auto mods = system_->get_passive_bonuses(entity);
    EXPECT_EQ(mods.attack_bonus, 4);   // 2 * 2
    EXPECT_EQ(mods.defense_bonus, 4);  // 2 * 2
}

TEST_F(PassiveSkillSystemTest, TaoistWujiLevel3AttackAndDefense) {
    auto entity = CreateEntityWithSkill(mir2::common::SkillId::TAOIST_WUJI, 3);
    system_->recalculate_passives(entity);

    const auto mods = system_->get_passive_bonuses(entity);
    EXPECT_EQ(mods.attack_bonus, 6);   // 3 * 2
    EXPECT_EQ(mods.defense_bonus, 6);  // 3 * 2
}

// ============================================================================
// 9. SinglePassive_GhostShield - GHOSTSHIELD gives magic_defense_bonus
//    magic_defense_bonus = clamped_level * 3
// ============================================================================

TEST_F(PassiveSkillSystemTest, GhostshieldLevel1MagicDefense) {
    auto entity = CreateEntityWithSkill(mir2::common::SkillId::GHOSTSHIELD, 1);
    system_->recalculate_passives(entity);

    const auto mods = system_->get_passive_bonuses(entity);
    EXPECT_EQ(mods.magic_defense_bonus, 3);  // 1 * 3
    EXPECT_EQ(mods.defense_bonus, 0);
    EXPECT_EQ(mods.attack_bonus, 0);
}

TEST_F(PassiveSkillSystemTest, GhostshieldLevel2MagicDefense) {
    auto entity = CreateEntityWithSkill(mir2::common::SkillId::GHOSTSHIELD, 2);
    system_->recalculate_passives(entity);

    const auto mods = system_->get_passive_bonuses(entity);
    EXPECT_EQ(mods.magic_defense_bonus, 6);  // 2 * 3
}

TEST_F(PassiveSkillSystemTest, GhostshieldLevel3MagicDefense) {
    auto entity = CreateEntityWithSkill(mir2::common::SkillId::GHOSTSHIELD, 3);
    system_->recalculate_passives(entity);

    const auto mods = system_->get_passive_bonuses(entity);
    EXPECT_EQ(mods.magic_defense_bonus, 9);  // 3 * 3
}

// ============================================================================
// 10. SinglePassive_DejiWonHo - DEJIWONHO gives defense_bonus (4 per level)
// ============================================================================

TEST_F(PassiveSkillSystemTest, DejiwonhoLevel1Defense) {
    auto entity = CreateEntityWithSkill(mir2::common::SkillId::DEJIWONHO, 1);
    system_->recalculate_passives(entity);

    const auto mods = system_->get_passive_bonuses(entity);
    EXPECT_EQ(mods.defense_bonus, 4);  // 1 * 4
    EXPECT_EQ(mods.attack_bonus, 0);
}

TEST_F(PassiveSkillSystemTest, DejiwonhoLevel2Defense) {
    auto entity = CreateEntityWithSkill(mir2::common::SkillId::DEJIWONHO, 2);
    system_->recalculate_passives(entity);

    const auto mods = system_->get_passive_bonuses(entity);
    EXPECT_EQ(mods.defense_bonus, 8);  // 2 * 4
}

TEST_F(PassiveSkillSystemTest, DejiwonhoLevel3Defense) {
    auto entity = CreateEntityWithSkill(mir2::common::SkillId::DEJIWONHO, 3);
    system_->recalculate_passives(entity);

    const auto mods = system_->get_passive_bonuses(entity);
    EXPECT_EQ(mods.defense_bonus, 12);  // 3 * 4
}

// ============================================================================
// 11. MultiplePassives - combine 2-3 passives and verify accumulation
// ============================================================================

TEST_F(PassiveSkillSystemTest, MultiplePassiveSkillsStack) {
    auto entity = CreateEntityWithSkills({
        {mir2::common::SkillId::BLOODLUST, 2},   // attack +6
        {mir2::common::SkillId::BODYGUARD, 1},    // defense +5
        {mir2::common::SkillId::ONESWORD, 3},     // hit_rate +3
    });

    system_->recalculate_passives(entity);

    const auto mods = system_->get_passive_bonuses(entity);
    EXPECT_EQ(mods.attack_bonus, 6);     // BLOODLUST level 2: 2*3
    EXPECT_EQ(mods.defense_bonus, 5);    // BODYGUARD level 1: 1*5
    EXPECT_EQ(mods.hit_rate_bonus, 3);   // ONESWORD level 3: 3*1
    EXPECT_EQ(mods.magic_defense_bonus, 0);
}

/**
 * @brief Defense bonuses from different skills stack: BODYGUARD + WUJIZHENQI.
 */
TEST_F(PassiveSkillSystemTest, DefenseBonusesFromDifferentSkillsStack) {
    auto entity = CreateEntityWithSkills({
        {mir2::common::SkillId::BODYGUARD, 2},    // defense +10
        {mir2::common::SkillId::WUJIZHENQI, 3},   // attack +6, defense +6
    });

    system_->recalculate_passives(entity);

    const auto mods = system_->get_passive_bonuses(entity);
    EXPECT_EQ(mods.defense_bonus, 16);  // 10 + 6
    EXPECT_EQ(mods.attack_bonus, 6);    // WUJIZHENQI only
}

/**
 * @brief Defense bonuses from BODYGUARD + DEJIWONHO stack.
 */
TEST_F(PassiveSkillSystemTest, BodyguardAndDejiwonhoDefenseStack) {
    auto entity = CreateEntityWithSkills({
        {mir2::common::SkillId::BODYGUARD, 3},    // defense +15
        {mir2::common::SkillId::DEJIWONHO, 3},    // defense +12
    });

    system_->recalculate_passives(entity);

    const auto mods = system_->get_passive_bonuses(entity);
    EXPECT_EQ(mods.defense_bonus, 27);  // 15 + 12
}

// ============================================================================
// 12. LevelClampedAt3 - level=4 or level=255 should be treated as 3
// ============================================================================

TEST_F(PassiveSkillSystemTest, OneswordLevelClampedAt3WithLevel4) {
    auto entity = CreateEntityWithSkill(mir2::common::SkillId::ONESWORD, 4);
    system_->recalculate_passives(entity);

    const auto mods = system_->get_passive_bonuses(entity);
    EXPECT_EQ(mods.hit_rate_bonus, 3);  // clamped: min(3,4) * 1
}

TEST_F(PassiveSkillSystemTest, OneswordLevelClampedAt3WithLevel10) {
    auto entity = CreateEntityWithSkill(mir2::common::SkillId::ONESWORD, 10);
    system_->recalculate_passives(entity);

    const auto mods = system_->get_passive_bonuses(entity);
    EXPECT_EQ(mods.hit_rate_bonus, 3);  // clamped: min(3,10) * 1
}

TEST_F(PassiveSkillSystemTest, BodyguardLevelClampedAt3WithLevel255) {
    auto entity = CreateEntityWithSkill(mir2::common::SkillId::BODYGUARD, 255);
    system_->recalculate_passives(entity);

    const auto mods = system_->get_passive_bonuses(entity);
    EXPECT_EQ(mods.defense_bonus, 15);  // clamped: min(3,255) * 5
}

TEST_F(PassiveSkillSystemTest, BloodlustLevelClampedAt3WithLevel100) {
    auto entity = CreateEntityWithSkill(mir2::common::SkillId::BLOODLUST, 100);
    system_->recalculate_passives(entity);

    const auto mods = system_->get_passive_bonuses(entity);
    EXPECT_EQ(mods.attack_bonus, 9);  // clamped: min(3,100) * 3
}

TEST_F(PassiveSkillSystemTest, WujizhenqiLevelClampedAt3WithLevel200) {
    auto entity = CreateEntityWithSkill(mir2::common::SkillId::WUJIZHENQI, 200);
    system_->recalculate_passives(entity);

    const auto mods = system_->get_passive_bonuses(entity);
    EXPECT_EQ(mods.attack_bonus, 6);   // clamped: min(3,200) * 2
    EXPECT_EQ(mods.defense_bonus, 6);  // clamped: min(3,200) * 2
}

TEST_F(PassiveSkillSystemTest, GhostshieldLevelClampedAt3WithLevel50) {
    auto entity = CreateEntityWithSkill(mir2::common::SkillId::GHOSTSHIELD, 50);
    system_->recalculate_passives(entity);

    const auto mods = system_->get_passive_bonuses(entity);
    EXPECT_EQ(mods.magic_defense_bonus, 9);  // clamped: min(3,50) * 3
}

TEST_F(PassiveSkillSystemTest, DejiwonhoLevelClampedAt3WithLevel255) {
    auto entity = CreateEntityWithSkill(mir2::common::SkillId::DEJIWONHO, 255);
    system_->recalculate_passives(entity);

    const auto mods = system_->get_passive_bonuses(entity);
    EXPECT_EQ(mods.defense_bonus, 12);  // clamped: min(3,255) * 4
}

// ============================================================================
// 13. NonPassiveSkillIgnored - add a non-passive skill, verify no bonuses
// ============================================================================

TEST_F(PassiveSkillSystemTest, NonPassiveSkillIsSkipped) {
    RegisterNonPassiveSkill(mir2::common::SkillId::FIREBALL, "FIREBALL");

    auto entity = CreateEntityWithSkill(mir2::common::SkillId::FIREBALL, 3);
    system_->recalculate_passives(entity);

    const auto mods = system_->get_passive_bonuses(entity);
    ExpectAllZero(mods);
}

// ============================================================================
// 14. MixedPassiveAndActive - combine passive+active skills
// ============================================================================

TEST_F(PassiveSkillSystemTest, MixPassiveAndNonPassiveSkills) {
    RegisterNonPassiveSkill(mir2::common::SkillId::FIREBALL, "FIREBALL");
    RegisterNonPassiveSkill(mir2::common::SkillId::LIGHTENING, "LIGHTENING");

    auto entity = CreateEntityWithSkills({
        {mir2::common::SkillId::FIREBALL, 3},       // non-passive: ignored
        {mir2::common::SkillId::BLOODLUST, 1},      // passive: attack +3
        {mir2::common::SkillId::LIGHTENING, 3},      // non-passive: ignored
        {mir2::common::SkillId::GHOSTSHIELD, 2},     // passive: magic_defense +6
    });

    system_->recalculate_passives(entity);

    const auto mods = system_->get_passive_bonuses(entity);
    EXPECT_EQ(mods.attack_bonus, 3);          // BLOODLUST level 1: 1*3
    EXPECT_EQ(mods.magic_defense_bonus, 6);   // GHOSTSHIELD level 2: 2*3
    EXPECT_EQ(mods.defense_bonus, 0);
    EXPECT_EQ(mods.hit_rate_bonus, 0);
    EXPECT_EQ(mods.magic_attack_bonus, 0);
}

// ============================================================================
// 15. GetPassiveBonuses_NoCachedData - returns zeroes
// ============================================================================

TEST_F(PassiveSkillSystemTest, GetPassiveBonusesNoCachedDataReturnsZero) {
    auto entity = CreateBareEntity();
    // Deliberately do NOT call recalculate_passives.

    const AttributeModifiers mods = system_->get_passive_bonuses(entity);
    ExpectAllZero(mods);
}

// ============================================================================
// 16. GetPassiveBonuses_AfterRecalculate - returns stored values
// ============================================================================

TEST_F(PassiveSkillSystemTest, GetPassiveBonusesAfterRecalculateReturnsStoredValues) {
    auto entity = CreateEntityWithSkill(mir2::common::SkillId::BLOODLUST, 2);
    system_->recalculate_passives(entity);

    const AttributeModifiers mods = system_->get_passive_bonuses(entity);
    EXPECT_EQ(mods.attack_bonus, 6);  // 2 * 3

    // Verify also via direct component access.
    const auto* stored = registry_.try_get<AttributeModifiers>(entity);
    ASSERT_NE(stored, nullptr);
    EXPECT_EQ(stored->attack_bonus, 6);
}

// ============================================================================
// 17. TriggerOnAttack_SameAsGetPassiveBonuses
// ============================================================================

TEST_F(PassiveSkillSystemTest, TriggerOnAttackDelegatesToGetPassiveBonuses) {
    auto entity = CreateEntityWithSkill(mir2::common::SkillId::BLOODLUST, 2);
    system_->recalculate_passives(entity);

    const AttributeModifiers from_get = system_->get_passive_bonuses(entity);
    const AttributeModifiers from_trigger = system_->trigger_on_attack(entity);

    EXPECT_EQ(from_get.attack_bonus, from_trigger.attack_bonus);
    EXPECT_EQ(from_get.defense_bonus, from_trigger.defense_bonus);
    EXPECT_EQ(from_get.magic_attack_bonus, from_trigger.magic_attack_bonus);
    EXPECT_EQ(from_get.magic_defense_bonus, from_trigger.magic_defense_bonus);
    EXPECT_EQ(from_get.hit_rate_bonus, from_trigger.hit_rate_bonus);
    EXPECT_EQ(from_get.dodge_bonus, from_trigger.dodge_bonus);
    EXPECT_EQ(from_get.speed_bonus, from_trigger.speed_bonus);
    EXPECT_FLOAT_EQ(from_get.critical_bonus, from_trigger.critical_bonus);
}

/**
 * @brief trigger_on_attack with multiple skills should match get_passive_bonuses.
 */
TEST_F(PassiveSkillSystemTest, TriggerOnAttackMatchesGetBonusesMultipleSkills) {
    auto entity = CreateEntityWithSkills({
        {mir2::common::SkillId::ONESWORD, 3},
        {mir2::common::SkillId::BODYGUARD, 2},
        {mir2::common::SkillId::WUJIZHENQI, 1},
    });
    system_->recalculate_passives(entity);

    const AttributeModifiers from_get = system_->get_passive_bonuses(entity);
    const AttributeModifiers from_trigger = system_->trigger_on_attack(entity);

    EXPECT_EQ(from_get.attack_bonus, from_trigger.attack_bonus);
    EXPECT_EQ(from_get.defense_bonus, from_trigger.defense_bonus);
    EXPECT_EQ(from_get.hit_rate_bonus, from_trigger.hit_rate_bonus);
}

// ============================================================================
// 18. RecalculateOverwritesPrevious - calling twice with different skills updates
// ============================================================================

TEST_F(PassiveSkillSystemTest, RecalculateOverwritesPreviousModifiers) {
    auto entity = CreateEntityWithSkill(mir2::common::SkillId::BLOODLUST, 1);

    system_->recalculate_passives(entity);
    EXPECT_EQ(system_->get_passive_bonuses(entity).attack_bonus, 3);  // 1*3

    // Change the skill level.
    auto& skill_list = registry_.get<SkillListComponent>(entity);
    LearnedSkill* ls = skill_list.get_skill(mir2::common::SkillId::BLOODLUST);
    ASSERT_NE(ls, nullptr);
    ls->level = 3;

    system_->recalculate_passives(entity);
    EXPECT_EQ(system_->get_passive_bonuses(entity).attack_bonus, 9);  // 3*3
}

/**
 * @brief After adding a new skill and recalculating, bonuses include both.
 */
TEST_F(PassiveSkillSystemTest, RecalculateAfterAddingSecondSkill) {
    auto entity = CreateEntityWithSkill(mir2::common::SkillId::BLOODLUST, 2);

    system_->recalculate_passives(entity);
    EXPECT_EQ(system_->get_passive_bonuses(entity).attack_bonus, 6);
    EXPECT_EQ(system_->get_passive_bonuses(entity).defense_bonus, 0);

    // Add a second passive skill.
    auto& skill_list = registry_.get<SkillListComponent>(entity);
    skill_list.add_skill(mir2::common::SkillId::BODYGUARD);
    skill_list.get_skill(mir2::common::SkillId::BODYGUARD)->level = 2;

    system_->recalculate_passives(entity);
    EXPECT_EQ(system_->get_passive_bonuses(entity).attack_bonus, 6);   // unchanged
    EXPECT_EQ(system_->get_passive_bonuses(entity).defense_bonus, 10); // 2*5
}

// ============================================================================
// 19. AllSevenPassivesCombined - all passive skills active at once
// ============================================================================

TEST_F(PassiveSkillSystemTest, AllSevenPassivesCombinedAtMaxLevel) {
    auto entity = CreateEntityWithSkills({
        {mir2::common::SkillId::ONESWORD, 3},     // hit_rate +3
        {mir2::common::SkillId::BODYGUARD, 3},    // defense +15
        {mir2::common::SkillId::BLOODLUST, 3},    // attack +9
        {mir2::common::SkillId::WUJIZHENQI, 3},   // attack +6, defense +6
        {mir2::common::SkillId::TAOIST_WUJI, 3},  // attack +6, defense +6
        {mir2::common::SkillId::GHOSTSHIELD, 3},  // magic_defense +9
        {mir2::common::SkillId::DEJIWONHO, 3},    // defense +12
    });

    system_->recalculate_passives(entity);

    const auto mods = system_->get_passive_bonuses(entity);
    // attack: BLOODLUST(9) + WUJIZHENQI(6) + TAOIST_WUJI(6) = 21
    EXPECT_EQ(mods.attack_bonus, 21);
    // defense: BODYGUARD(15) + WUJIZHENQI(6) + TAOIST_WUJI(6) + DEJIWONHO(12) = 39
    EXPECT_EQ(mods.defense_bonus, 39);
    // hit_rate: ONESWORD(3) = 3
    EXPECT_EQ(mods.hit_rate_bonus, 3);
    // magic_defense: GHOSTSHIELD(9) = 9
    EXPECT_EQ(mods.magic_defense_bonus, 9);
    // These should remain zero as no skill modifies them.
    EXPECT_EQ(mods.magic_attack_bonus, 0);
    EXPECT_EQ(mods.dodge_bonus, 0);
    EXPECT_EQ(mods.speed_bonus, 0);
    EXPECT_FLOAT_EQ(mods.critical_bonus, 0.0f);
}

/**
 * @brief All seven passives at level 1 to verify minimum stacking.
 */
TEST_F(PassiveSkillSystemTest, AllSevenPassivesCombinedAtLevel1) {
    auto entity = CreateEntityWithSkills({
        {mir2::common::SkillId::ONESWORD, 1},     // hit_rate +1
        {mir2::common::SkillId::BODYGUARD, 1},    // defense +5
        {mir2::common::SkillId::BLOODLUST, 1},    // attack +3
        {mir2::common::SkillId::WUJIZHENQI, 1},   // attack +2, defense +2
        {mir2::common::SkillId::TAOIST_WUJI, 1},  // attack +2, defense +2
        {mir2::common::SkillId::GHOSTSHIELD, 1},  // magic_defense +3
        {mir2::common::SkillId::DEJIWONHO, 1},    // defense +4
    });

    system_->recalculate_passives(entity);

    const auto mods = system_->get_passive_bonuses(entity);
    // attack: BLOODLUST(3) + WUJIZHENQI(2) + TAOIST_WUJI(2) = 7
    EXPECT_EQ(mods.attack_bonus, 7);
    // defense: BODYGUARD(5) + WUJIZHENQI(2) + TAOIST_WUJI(2) + DEJIWONHO(4) = 13
    EXPECT_EQ(mods.defense_bonus, 13);
    // hit_rate: ONESWORD(1) = 1
    EXPECT_EQ(mods.hit_rate_bonus, 1);
    // magic_defense: GHOSTSHIELD(3) = 3
    EXPECT_EQ(mods.magic_defense_bonus, 3);
}

// ============================================================================
// 20. ZeroLevelGivesZeroBonus - level=0 should produce zero bonuses
// ============================================================================

TEST_F(PassiveSkillSystemTest, OneswordLevel0ZeroBonus) {
    auto entity = CreateEntityWithSkill(mir2::common::SkillId::ONESWORD, 0);
    system_->recalculate_passives(entity);

    const auto mods = system_->get_passive_bonuses(entity);
    EXPECT_EQ(mods.hit_rate_bonus, 0);  // 0 * 1
}

TEST_F(PassiveSkillSystemTest, BodyguardLevel0ZeroBonus) {
    auto entity = CreateEntityWithSkill(mir2::common::SkillId::BODYGUARD, 0);
    system_->recalculate_passives(entity);

    const auto mods = system_->get_passive_bonuses(entity);
    EXPECT_EQ(mods.defense_bonus, 0);  // 0 * 5
}

TEST_F(PassiveSkillSystemTest, BloodlustLevel0ZeroBonus) {
    auto entity = CreateEntityWithSkill(mir2::common::SkillId::BLOODLUST, 0);
    system_->recalculate_passives(entity);

    const auto mods = system_->get_passive_bonuses(entity);
    EXPECT_EQ(mods.attack_bonus, 0);  // 0 * 3
}

TEST_F(PassiveSkillSystemTest, WujizhenqiLevel0ZeroBonus) {
    auto entity = CreateEntityWithSkill(mir2::common::SkillId::WUJIZHENQI, 0);
    system_->recalculate_passives(entity);

    const auto mods = system_->get_passive_bonuses(entity);
    EXPECT_EQ(mods.attack_bonus, 0);   // 0 * 2
    EXPECT_EQ(mods.defense_bonus, 0);  // 0 * 2
}

TEST_F(PassiveSkillSystemTest, GhostshieldLevel0ZeroBonus) {
    auto entity = CreateEntityWithSkill(mir2::common::SkillId::GHOSTSHIELD, 0);
    system_->recalculate_passives(entity);

    const auto mods = system_->get_passive_bonuses(entity);
    EXPECT_EQ(mods.magic_defense_bonus, 0);  // 0 * 3
}

TEST_F(PassiveSkillSystemTest, DejiwonhoLevel0ZeroBonus) {
    auto entity = CreateEntityWithSkill(mir2::common::SkillId::DEJIWONHO, 0);
    system_->recalculate_passives(entity);

    const auto mods = system_->get_passive_bonuses(entity);
    EXPECT_EQ(mods.defense_bonus, 0);  // 0 * 4
}

TEST_F(PassiveSkillSystemTest, TaoistWujiLevel0ZeroBonus) {
    auto entity = CreateEntityWithSkill(mir2::common::SkillId::TAOIST_WUJI, 0);
    system_->recalculate_passives(entity);

    const auto mods = system_->get_passive_bonuses(entity);
    EXPECT_EQ(mods.attack_bonus, 0);
    EXPECT_EQ(mods.defense_bonus, 0);
}

// ============================================================================
// 21. UnknownSkillIdIgnored - a skill with unknown id gives zero bonus
// ============================================================================

TEST_F(PassiveSkillSystemTest, UnrecognizedPassiveSkillIdContributesZero) {
    constexpr uint32_t kUnknownSkillId = 9999;
    RegisterPassiveSkill(kUnknownSkillId, "UNKNOWN_PASSIVE");

    auto entity = CreateEntityWithSkill(kUnknownSkillId, 3);
    system_->recalculate_passives(entity);

    const auto mods = system_->get_passive_bonuses(entity);
    ExpectAllZero(mods);
}

/**
 * @brief If a skill_id is in the SkillListComponent but not registered in
 *        SkillRegistry at all, it should be skipped (get_skill returns nullptr).
 */
TEST_F(PassiveSkillSystemTest, SkillNotInRegistryIsSkipped) {
    constexpr uint32_t kUnregistered = 8888;
    // Intentionally NOT registering this skill.

    auto entity = CreateEntityWithSkill(kUnregistered, 3);
    system_->recalculate_passives(entity);

    const auto mods = system_->get_passive_bonuses(entity);
    ExpectAllZero(mods);
}

// ============================================================================
// 22. EntityDestroyedBetweenCalls - entity destroyed after recalculate
// ============================================================================

TEST_F(PassiveSkillSystemTest, EntityDestroyedAfterRecalculateGetBonusesReturnsZero) {
    auto entity = CreateEntityWithSkill(mir2::common::SkillId::BLOODLUST, 3);
    system_->recalculate_passives(entity);

    // Confirm bonus was stored.
    EXPECT_EQ(system_->get_passive_bonuses(entity).attack_bonus, 9);

    // Destroy the entity.
    registry_.destroy(entity);

    // After destruction, querying should return zero without crashing.
    const auto mods = system_->get_passive_bonuses(entity);
    EXPECT_EQ(mods.attack_bonus, 0);
    EXPECT_EQ(mods.defense_bonus, 0);
}

TEST_F(PassiveSkillSystemTest, EntityDestroyedAfterRecalculateTriggerAttackReturnsZero) {
    auto entity = CreateEntityWithSkill(mir2::common::SkillId::BODYGUARD, 2);
    system_->recalculate_passives(entity);
    EXPECT_EQ(system_->trigger_on_attack(entity).defense_bonus, 10);

    registry_.destroy(entity);

    const auto mods = system_->trigger_on_attack(entity);
    EXPECT_EQ(mods.defense_bonus, 0);
}

// ============================================================================
// 23. InvalidEntityGetPassive - get_passive_bonuses on invalid entity returns zeroes
// ============================================================================

TEST_F(PassiveSkillSystemTest, GetPassiveBonusesInvalidEntityReturnsZero) {
    const AttributeModifiers mods = system_->get_passive_bonuses(entt::null);
    ExpectAllZero(mods);
}

TEST_F(PassiveSkillSystemTest, GetPassiveBonusesDestroyedEntityReturnsZero) {
    auto entity = CreateBareEntity();
    registry_.destroy(entity);

    const AttributeModifiers mods = system_->get_passive_bonuses(entity);
    EXPECT_EQ(mods.attack_bonus, 0);
    EXPECT_EQ(mods.defense_bonus, 0);
    EXPECT_EQ(mods.magic_defense_bonus, 0);
    EXPECT_EQ(mods.hit_rate_bonus, 0);
}

// ============================================================================
// 24. InvalidEntityTriggerAttack - trigger_on_attack on invalid entity returns zeroes
// ============================================================================

TEST_F(PassiveSkillSystemTest, TriggerOnAttackInvalidEntityReturnsZero) {
    const AttributeModifiers mods = system_->trigger_on_attack(entt::null);
    ExpectAllZero(mods);
}

TEST_F(PassiveSkillSystemTest, TriggerOnAttackDestroyedEntityReturnsZero) {
    auto entity = CreateBareEntity();
    registry_.destroy(entity);

    const AttributeModifiers mods = system_->trigger_on_attack(entity);
    EXPECT_EQ(mods.attack_bonus, 0);
    EXPECT_EQ(mods.defense_bonus, 0);
}

// ============================================================================
// 25. RecalculateWithRemovedSkills - remove skills and recalculate to verify zeroes
// ============================================================================

TEST_F(PassiveSkillSystemTest, RecalculateAfterRemovingAllSkillsYieldsZero) {
    auto entity = CreateEntityWithSkills({
        {mir2::common::SkillId::BLOODLUST, 3},    // attack +9
        {mir2::common::SkillId::BODYGUARD, 2},    // defense +10
    });

    system_->recalculate_passives(entity);
    EXPECT_EQ(system_->get_passive_bonuses(entity).attack_bonus, 9);
    EXPECT_EQ(system_->get_passive_bonuses(entity).defense_bonus, 10);

    // Remove all skills.
    auto& skill_list = registry_.get<SkillListComponent>(entity);
    skill_list.remove_skill(mir2::common::SkillId::BLOODLUST);
    skill_list.remove_skill(mir2::common::SkillId::BODYGUARD);

    system_->recalculate_passives(entity);

    const auto mods = system_->get_passive_bonuses(entity);
    ExpectAllZero(mods);
}

TEST_F(PassiveSkillSystemTest, RecalculateAfterRemovingOneSkillUpdatesCorrectly) {
    auto entity = CreateEntityWithSkills({
        {mir2::common::SkillId::BLOODLUST, 2},    // attack +6
        {mir2::common::SkillId::ONESWORD, 3},     // hit_rate +3
    });

    system_->recalculate_passives(entity);
    EXPECT_EQ(system_->get_passive_bonuses(entity).attack_bonus, 6);
    EXPECT_EQ(system_->get_passive_bonuses(entity).hit_rate_bonus, 3);

    // Remove only BLOODLUST.
    auto& skill_list = registry_.get<SkillListComponent>(entity);
    skill_list.remove_skill(mir2::common::SkillId::BLOODLUST);

    system_->recalculate_passives(entity);

    const auto mods = system_->get_passive_bonuses(entity);
    EXPECT_EQ(mods.attack_bonus, 0);     // BLOODLUST removed
    EXPECT_EQ(mods.hit_rate_bonus, 3);   // ONESWORD still there
}

// ============================================================================
// Additional edge case tests
// ============================================================================

/**
 * @brief Recalculating on an entity that already has a manually placed
 *        AttributeModifiers component should overwrite it completely.
 */
TEST_F(PassiveSkillSystemTest, RecalculateOverwritesManuallyPlacedComponent) {
    auto entity = CreateBareEntity();

    // Manually place a non-zero AttributeModifiers.
    AttributeModifiers manual{};
    manual.attack_bonus = 999;
    manual.defense_bonus = 888;
    registry_.emplace<AttributeModifiers>(entity, manual);

    // Recalculate with no skills -> should overwrite to all zeros.
    system_->recalculate_passives(entity);

    const auto* mods = registry_.try_get<AttributeModifiers>(entity);
    ASSERT_NE(mods, nullptr);
    EXPECT_EQ(mods->attack_bonus, 0);
    EXPECT_EQ(mods->defense_bonus, 0);
}

/**
 * @brief Two different entities should have independent passive bonuses.
 */
TEST_F(PassiveSkillSystemTest, MultipleEntitiesIndependentBonuses) {
    auto warrior = CreateEntityWithSkill(mir2::common::SkillId::BLOODLUST, 3);
    auto taoist = CreateEntityWithSkill(mir2::common::SkillId::GHOSTSHIELD, 2);

    system_->recalculate_passives(warrior);
    system_->recalculate_passives(taoist);

    const auto warrior_mods = system_->get_passive_bonuses(warrior);
    const auto taoist_mods = system_->get_passive_bonuses(taoist);

    EXPECT_EQ(warrior_mods.attack_bonus, 9);         // 3*3
    EXPECT_EQ(warrior_mods.magic_defense_bonus, 0);

    EXPECT_EQ(taoist_mods.attack_bonus, 0);
    EXPECT_EQ(taoist_mods.magic_defense_bonus, 6);   // 2*3
}

/**
 * @brief Recalculating one entity does not affect another entity's cached bonuses.
 */
TEST_F(PassiveSkillSystemTest, RecalculateOneEntityDoesNotAffectAnother) {
    auto entity_a = CreateEntityWithSkill(mir2::common::SkillId::BLOODLUST, 2);
    auto entity_b = CreateEntityWithSkill(mir2::common::SkillId::BODYGUARD, 3);

    system_->recalculate_passives(entity_a);
    system_->recalculate_passives(entity_b);

    EXPECT_EQ(system_->get_passive_bonuses(entity_a).attack_bonus, 6);
    EXPECT_EQ(system_->get_passive_bonuses(entity_b).defense_bonus, 15);

    // Change entity_a's skill and recalculate only entity_a.
    auto& skill_list_a = registry_.get<SkillListComponent>(entity_a);
    skill_list_a.get_skill(mir2::common::SkillId::BLOODLUST)->level = 1;
    system_->recalculate_passives(entity_a);

    // entity_a updated, entity_b unchanged.
    EXPECT_EQ(system_->get_passive_bonuses(entity_a).attack_bonus, 3);
    EXPECT_EQ(system_->get_passive_bonuses(entity_b).defense_bonus, 15);
}

/**
 * @brief WUJIZHENQI and TAOIST_WUJI produce identical results at the same level.
 */
TEST_F(PassiveSkillSystemTest, WujizhenqiAndTaoistWujiProduceIdenticalBonuses) {
    for (uint8_t level = 0; level <= 3; ++level) {
        auto entity_warrior = CreateEntityWithSkill(
            mir2::common::SkillId::WUJIZHENQI, level);
        auto entity_taoist = CreateEntityWithSkill(
            mir2::common::SkillId::TAOIST_WUJI, level);

        system_->recalculate_passives(entity_warrior);
        system_->recalculate_passives(entity_taoist);

        const auto warrior_mods = system_->get_passive_bonuses(entity_warrior);
        const auto taoist_mods = system_->get_passive_bonuses(entity_taoist);

        EXPECT_EQ(warrior_mods.attack_bonus, taoist_mods.attack_bonus)
            << "Mismatch at level " << static_cast<int>(level);
        EXPECT_EQ(warrior_mods.defense_bonus, taoist_mods.defense_bonus)
            << "Mismatch at level " << static_cast<int>(level);
    }
}

/**
 * @brief Sparse skill list (skills in non-contiguous slots with gaps) should
 *        correctly skip empty optional slots.
 */
TEST_F(PassiveSkillSystemTest, SparseSkillListSkipsEmptySlots) {
    auto entity = registry_.create();
    auto& skill_list = registry_.emplace<SkillListComponent>(entity);

    // Add and then remove a skill to create a gap in the array.
    skill_list.add_skill(mir2::common::SkillId::ONESWORD);
    skill_list.add_skill(mir2::common::SkillId::BLOODLUST);
    skill_list.add_skill(mir2::common::SkillId::BODYGUARD);

    // Remove the middle one to create a sparse array.
    skill_list.remove_skill(mir2::common::SkillId::BLOODLUST);

    // Set levels.
    skill_list.get_skill(mir2::common::SkillId::ONESWORD)->level = 2;
    skill_list.get_skill(mir2::common::SkillId::BODYGUARD)->level = 1;

    system_->recalculate_passives(entity);

    const auto mods = system_->get_passive_bonuses(entity);
    EXPECT_EQ(mods.hit_rate_bonus, 2);   // ONESWORD level 2: 2*1
    EXPECT_EQ(mods.defense_bonus, 5);    // BODYGUARD level 1: 1*5
    EXPECT_EQ(mods.attack_bonus, 0);     // BLOODLUST removed
}

/**
 * @brief Fill all 20 skill slots with a mixture and verify correct accumulation.
 */
TEST_F(PassiveSkillSystemTest, FullSkillListWithMixedTypes) {
    RegisterNonPassiveSkill(mir2::common::SkillId::FIREBALL, "FIREBALL");
    RegisterNonPassiveSkill(mir2::common::SkillId::LIGHTENING, "LIGHTENING");
    RegisterNonPassiveSkill(mir2::common::SkillId::HEALING, "HEALING");

    auto entity = registry_.create();
    auto& skill_list = registry_.emplace<SkillListComponent>(entity);

    // Add 7 passive skills.
    skill_list.add_skill(mir2::common::SkillId::ONESWORD);
    skill_list.get_skill(mir2::common::SkillId::ONESWORD)->level = 1;

    skill_list.add_skill(mir2::common::SkillId::BODYGUARD);
    skill_list.get_skill(mir2::common::SkillId::BODYGUARD)->level = 1;

    skill_list.add_skill(mir2::common::SkillId::BLOODLUST);
    skill_list.get_skill(mir2::common::SkillId::BLOODLUST)->level = 1;

    skill_list.add_skill(mir2::common::SkillId::WUJIZHENQI);
    skill_list.get_skill(mir2::common::SkillId::WUJIZHENQI)->level = 1;

    skill_list.add_skill(mir2::common::SkillId::TAOIST_WUJI);
    skill_list.get_skill(mir2::common::SkillId::TAOIST_WUJI)->level = 1;

    skill_list.add_skill(mir2::common::SkillId::GHOSTSHIELD);
    skill_list.get_skill(mir2::common::SkillId::GHOSTSHIELD)->level = 1;

    skill_list.add_skill(mir2::common::SkillId::DEJIWONHO);
    skill_list.get_skill(mir2::common::SkillId::DEJIWONHO)->level = 1;

    // Add 3 non-passive skills.
    skill_list.add_skill(mir2::common::SkillId::FIREBALL);
    skill_list.get_skill(mir2::common::SkillId::FIREBALL)->level = 3;

    skill_list.add_skill(mir2::common::SkillId::LIGHTENING);
    skill_list.get_skill(mir2::common::SkillId::LIGHTENING)->level = 3;

    skill_list.add_skill(mir2::common::SkillId::HEALING);
    skill_list.get_skill(mir2::common::SkillId::HEALING)->level = 3;

    // Fill remaining 10 slots with unregistered skills (should be skipped).
    for (uint32_t i = 0; i < 10; ++i) {
        skill_list.add_skill(7000 + i);
    }

    EXPECT_EQ(skill_list.count, 20);

    system_->recalculate_passives(entity);

    const auto mods = system_->get_passive_bonuses(entity);
    // Only the 7 passive skills at level 1 should contribute.
    EXPECT_EQ(mods.attack_bonus, 7);           // BLOODLUST(3) + WUJI(2) + TAOIST(2)
    EXPECT_EQ(mods.defense_bonus, 13);         // BODYGUARD(5) + WUJI(2) + TAOIST(2) + DEJI(4)
    EXPECT_EQ(mods.hit_rate_bonus, 1);         // ONESWORD(1)
    EXPECT_EQ(mods.magic_defense_bonus, 3);    // GHOSTSHIELD(3)
}

}  // namespace
}  // namespace mir2::ecs::test
