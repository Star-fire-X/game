/**
 * @file equipment_bonus_system_test.cc
 * @brief Comprehensive tests for EquipmentBonusSystem - equipment bonus aggregation
 *
 * Test Coverage:
 * - No equipment component -> zero bonus
 * - Empty equipment slots -> zero bonus
 * - Single item equipped -> correct bonus
 * - Multiple items -> bonuses sum correctly
 * - Elemental damage routing: fire(1), ice(2), lightning(3), poison(4)
 * - Invalid entity -> zero/null
 * - recalculate_bonus stores component, get_cached_bonus retrieves it
 * - Null item entity in slot -> skipped
 * - Invalid (destroyed) item entity in slot -> skipped
 * - Item entity without ItemComponent -> skipped
 * - Special effects: lifesteal, reflect
 *
 * Priority: P0 Critical (Score: 43)
 * Risk: Critical - Incorrect equipment bonuses directly affect character stats
 */

#include <gtest/gtest.h>

#include "ecs/systems/equipment_bonus_system.h"
#include "ecs/components/equipment_component.h"
#include "ecs/components/item_component.h"

#include <entt/entt.hpp>

namespace mir2::ecs::test {
namespace {

/**
 * @brief Test fixture for EquipmentBonusSystem
 *
 * Provides helpers to create characters with equipment slots and item entities
 * with configurable bonus values.
 */
class EquipmentBonusSystemTest : public ::testing::Test {
 protected:
  void SetUp() override {
    system_ = std::make_unique<EquipmentBonusSystem>(registry_);
  }

  /**
   * @brief Create a character entity with an empty EquipmentSlotComponent
   */
  entt::entity CreateCharacter() {
    auto entity = registry_.create();
    registry_.emplace<EquipmentSlotComponent>(entity);
    return entity;
  }

  /**
   * @brief Create a bare entity (no EquipmentSlotComponent)
   */
  entt::entity CreateBareEntity() {
    return registry_.create();
  }

  /**
   * @brief Create an item entity with specified bonus values
   */
  entt::entity CreateItem(int attack = 0, int defense = 0,
                          int magic_attack = 0, int magic_defense = 0,
                          int hp = 0, int mp = 0,
                          int hit_rate = 0, int dodge = 0,
                          int speed = 0, int luck = 0) {
    auto entity = registry_.create();
    auto& item = registry_.emplace<ItemComponent>(entity);
    item.attack_bonus = attack;
    item.defense_bonus = defense;
    item.magic_attack_bonus = magic_attack;
    item.magic_defense_bonus = magic_defense;
    item.hp_bonus = hp;
    item.mp_bonus = mp;
    item.hit_rate_bonus = hit_rate;
    item.dodge_bonus = dodge;
    item.speed_bonus = speed;
    item.luck = luck;
    return entity;
  }

  /**
   * @brief Create an item entity with elemental damage
   */
  entt::entity CreateElementalItem(int elemental_type, int elemental_damage) {
    auto entity = registry_.create();
    auto& item = registry_.emplace<ItemComponent>(entity);
    item.elemental_type = elemental_type;
    item.elemental_damage = elemental_damage;
    return entity;
  }

  /**
   * @brief Create an item entity with special effects (lifesteal, reflect)
   */
  entt::entity CreateSpecialItem(int lifesteal = 0, int reflect = 0) {
    auto entity = registry_.create();
    auto& item = registry_.emplace<ItemComponent>(entity);
    item.lifesteal_percent = lifesteal;
    item.reflect_percent = reflect;
    return entity;
  }

  /**
   * @brief Equip an item entity into a specific slot on a character
   */
  void EquipItem(entt::entity character, std::size_t slot,
                 entt::entity item_entity) {
    auto& equipment = registry_.get<EquipmentSlotComponent>(character);
    equipment.slots[slot] = item_entity;
  }

  /**
   * @brief Helper to verify all bonus fields are zero
   */
  void ExpectZeroBonus(const EquipmentBonus& bonus) {
    EXPECT_EQ(bonus.attack_bonus, 0);
    EXPECT_EQ(bonus.defense_bonus, 0);
    EXPECT_EQ(bonus.magic_attack_bonus, 0);
    EXPECT_EQ(bonus.magic_defense_bonus, 0);
    EXPECT_EQ(bonus.hp_bonus, 0);
    EXPECT_EQ(bonus.mp_bonus, 0);
    EXPECT_EQ(bonus.hit_rate_bonus, 0);
    EXPECT_EQ(bonus.dodge_bonus, 0);
    EXPECT_EQ(bonus.speed_bonus, 0);
    EXPECT_EQ(bonus.luck_bonus, 0);
    EXPECT_EQ(bonus.total_weight, 0);
    EXPECT_EQ(bonus.hand_weight, 0);
    EXPECT_EQ(bonus.lifesteal_percent, 0);
    EXPECT_EQ(bonus.reflect_percent, 0);
    EXPECT_EQ(bonus.fire_damage, 0);
    EXPECT_EQ(bonus.ice_damage, 0);
    EXPECT_EQ(bonus.lightning_damage, 0);
    EXPECT_EQ(bonus.poison_damage, 0);
  }

  entt::registry registry_;
  std::unique_ptr<EquipmentBonusSystem> system_;
};

// ============================================================================
// Test 1: No equipment component -> zero bonus
// ============================================================================

/**
 * @brief An entity without EquipmentSlotComponent should produce all-zero bonus
 */
TEST_F(EquipmentBonusSystemTest, NoEquipmentComponentReturnsZeroBonus) {
  auto entity = CreateBareEntity();

  EquipmentBonus bonus = system_->calculate_total_bonus(entity);

  ExpectZeroBonus(bonus);
}

// ============================================================================
// Test 2: Empty equipment slots -> zero bonus
// ============================================================================

/**
 * @brief A character with EquipmentSlotComponent but no items equipped
 * should produce all-zero bonus (all slots are entt::null)
 */
TEST_F(EquipmentBonusSystemTest, EmptyEquipmentSlotsReturnZeroBonus) {
  auto character = CreateCharacter();

  EquipmentBonus bonus = system_->calculate_total_bonus(character);

  ExpectZeroBonus(bonus);
}

// ============================================================================
// Test 3: Single item equipped -> correct bonus
// ============================================================================

/**
 * @brief Equipping a single item should produce bonus matching that item
 */
TEST_F(EquipmentBonusSystemTest, SingleItemEquippedReturnsCorrectBonus) {
  auto character = CreateCharacter();
  auto weapon = CreateItem(
      /*attack=*/15, /*defense=*/3,
      /*magic_attack=*/5, /*magic_defense=*/2,
      /*hp=*/50, /*mp=*/20,
      /*hit_rate=*/4, /*dodge=*/1,
      /*speed=*/2, /*luck=*/7);

  EquipItem(character, 0, weapon);  // Slot 0 = WEAPON

  EquipmentBonus bonus = system_->calculate_total_bonus(character);

  EXPECT_EQ(bonus.attack_bonus, 15);
  EXPECT_EQ(bonus.defense_bonus, 3);
  EXPECT_EQ(bonus.magic_attack_bonus, 5);
  EXPECT_EQ(bonus.magic_defense_bonus, 2);
  EXPECT_EQ(bonus.hp_bonus, 50);
  EXPECT_EQ(bonus.mp_bonus, 20);
  EXPECT_EQ(bonus.hit_rate_bonus, 4);
  EXPECT_EQ(bonus.dodge_bonus, 1);
  EXPECT_EQ(bonus.speed_bonus, 2);
  EXPECT_EQ(bonus.luck_bonus, 7);
}

// ============================================================================
// Test 4: Multiple items -> bonuses sum correctly
// ============================================================================

/**
 * @brief Equipping multiple items should produce the sum of all their bonuses
 */
TEST_F(EquipmentBonusSystemTest, MultipleItemsBonusesSumCorrectly) {
  auto character = CreateCharacter();

  // Weapon: +15 attack, +5 magic attack
  auto weapon = CreateItem(/*attack=*/15, /*defense=*/0,
                           /*magic_attack=*/5);
  // Armor: +20 defense, +100 hp
  auto armor = CreateItem(/*attack=*/0, /*defense=*/20,
                          /*magic_attack=*/0, /*magic_defense=*/0,
                          /*hp=*/100);
  // Helmet: +8 defense, +3 magic defense
  auto helmet = CreateItem(/*attack=*/0, /*defense=*/8,
                           /*magic_attack=*/0, /*magic_defense=*/3);
  // Ring: +3 hit rate, +2 luck
  auto ring = CreateItem(/*attack=*/0, /*defense=*/0,
                         /*magic_attack=*/0, /*magic_defense=*/0,
                         /*hp=*/0, /*mp=*/0,
                         /*hit_rate=*/3, /*dodge=*/0,
                         /*speed=*/0, /*luck=*/2);

  EquipItem(character, 0, weapon);   // WEAPON
  EquipItem(character, 1, armor);    // ARMOR
  EquipItem(character, 2, helmet);   // HELMET
  EquipItem(character, 4, ring);     // RING_LEFT

  EquipmentBonus bonus = system_->calculate_total_bonus(character);

  EXPECT_EQ(bonus.attack_bonus, 15);
  EXPECT_EQ(bonus.defense_bonus, 28);      // 20 + 8
  EXPECT_EQ(bonus.magic_attack_bonus, 5);
  EXPECT_EQ(bonus.magic_defense_bonus, 3);
  EXPECT_EQ(bonus.hp_bonus, 100);
  EXPECT_EQ(bonus.hit_rate_bonus, 3);
  EXPECT_EQ(bonus.luck_bonus, 2);
}

/**
 * @brief All 13 slots filled should sum correctly
 */
TEST_F(EquipmentBonusSystemTest, AllSlotsFilled) {
  auto character = CreateCharacter();

  for (std::size_t i = 0; i < EquipmentSlotComponent::kSlotCount; ++i) {
    auto item = CreateItem(/*attack=*/1, /*defense=*/1);
    EquipItem(character, i, item);
  }

  EquipmentBonus bonus = system_->calculate_total_bonus(character);

  EXPECT_EQ(bonus.attack_bonus, static_cast<int>(EquipmentSlotComponent::kSlotCount));
  EXPECT_EQ(bonus.defense_bonus, static_cast<int>(EquipmentSlotComponent::kSlotCount));
}

// ============================================================================
// Test 5: Elemental damage routing
// ============================================================================

/**
 * @brief Fire elemental type (1) routes to fire_damage
 */
TEST_F(EquipmentBonusSystemTest, FireElementalDamageRouting) {
  auto character = CreateCharacter();
  auto weapon = CreateElementalItem(/*type=*/1, /*damage=*/25);
  EquipItem(character, 0, weapon);

  EquipmentBonus bonus = system_->calculate_total_bonus(character);

  EXPECT_EQ(bonus.fire_damage, 25);
  EXPECT_EQ(bonus.ice_damage, 0);
  EXPECT_EQ(bonus.lightning_damage, 0);
  EXPECT_EQ(bonus.poison_damage, 0);
}

/**
 * @brief Ice elemental type (2) routes to ice_damage
 */
TEST_F(EquipmentBonusSystemTest, IceElementalDamageRouting) {
  auto character = CreateCharacter();
  auto weapon = CreateElementalItem(/*type=*/2, /*damage=*/30);
  EquipItem(character, 0, weapon);

  EquipmentBonus bonus = system_->calculate_total_bonus(character);

  EXPECT_EQ(bonus.fire_damage, 0);
  EXPECT_EQ(bonus.ice_damage, 30);
  EXPECT_EQ(bonus.lightning_damage, 0);
  EXPECT_EQ(bonus.poison_damage, 0);
}

/**
 * @brief Lightning elemental type (3) routes to lightning_damage
 */
TEST_F(EquipmentBonusSystemTest, LightningElementalDamageRouting) {
  auto character = CreateCharacter();
  auto weapon = CreateElementalItem(/*type=*/3, /*damage=*/18);
  EquipItem(character, 0, weapon);

  EquipmentBonus bonus = system_->calculate_total_bonus(character);

  EXPECT_EQ(bonus.fire_damage, 0);
  EXPECT_EQ(bonus.ice_damage, 0);
  EXPECT_EQ(bonus.lightning_damage, 18);
  EXPECT_EQ(bonus.poison_damage, 0);
}

/**
 * @brief Poison elemental type (4) routes to poison_damage
 */
TEST_F(EquipmentBonusSystemTest, PoisonElementalDamageRouting) {
  auto character = CreateCharacter();
  auto weapon = CreateElementalItem(/*type=*/4, /*damage=*/12);
  EquipItem(character, 0, weapon);

  EquipmentBonus bonus = system_->calculate_total_bonus(character);

  EXPECT_EQ(bonus.fire_damage, 0);
  EXPECT_EQ(bonus.ice_damage, 0);
  EXPECT_EQ(bonus.lightning_damage, 0);
  EXPECT_EQ(bonus.poison_damage, 12);
}

/**
 * @brief Unknown elemental type (0 or out of range) adds no elemental damage
 */
TEST_F(EquipmentBonusSystemTest, UnknownElementalTypeIgnored) {
  auto character = CreateCharacter();

  auto item_type_0 = CreateElementalItem(/*type=*/0, /*damage=*/50);
  auto item_type_99 = CreateElementalItem(/*type=*/99, /*damage=*/50);

  EquipItem(character, 0, item_type_0);
  EquipItem(character, 1, item_type_99);

  EquipmentBonus bonus = system_->calculate_total_bonus(character);

  EXPECT_EQ(bonus.fire_damage, 0);
  EXPECT_EQ(bonus.ice_damage, 0);
  EXPECT_EQ(bonus.lightning_damage, 0);
  EXPECT_EQ(bonus.poison_damage, 0);
}

/**
 * @brief Multiple items with different elemental types should accumulate
 * into their respective damage fields independently
 */
TEST_F(EquipmentBonusSystemTest, MultipleElementalTypesSumIndependently) {
  auto character = CreateCharacter();

  auto fire_weapon = CreateElementalItem(/*type=*/1, /*damage=*/10);
  auto ice_ring = CreateElementalItem(/*type=*/2, /*damage=*/8);
  auto fire_necklace = CreateElementalItem(/*type=*/1, /*damage=*/5);
  auto lightning_amulet = CreateElementalItem(/*type=*/3, /*damage=*/12);

  EquipItem(character, 0, fire_weapon);    // WEAPON
  EquipItem(character, 4, ice_ring);       // RING_LEFT
  EquipItem(character, 6, fire_necklace);  // NECKLACE
  EquipItem(character, 10, lightning_amulet); // AMULET

  EquipmentBonus bonus = system_->calculate_total_bonus(character);

  EXPECT_EQ(bonus.fire_damage, 15);       // 10 + 5
  EXPECT_EQ(bonus.ice_damage, 8);
  EXPECT_EQ(bonus.lightning_damage, 12);
  EXPECT_EQ(bonus.poison_damage, 0);
}

// ============================================================================
// Test 6: Invalid entity -> zero/null
// ============================================================================

/**
 * @brief calculate_total_bonus on an invalid entity should return zero bonus
 */
TEST_F(EquipmentBonusSystemTest, InvalidEntityReturnsZeroBonus) {
  // Create and immediately destroy an entity to get an invalid handle
  auto entity = registry_.create();
  registry_.destroy(entity);

  EquipmentBonus bonus = system_->calculate_total_bonus(entity);

  ExpectZeroBonus(bonus);
}

/**
 * @brief get_cached_bonus on an invalid entity should return nullptr
 */
TEST_F(EquipmentBonusSystemTest, InvalidEntityCachedBonusReturnsNull) {
  auto entity = registry_.create();
  registry_.destroy(entity);

  const EquipmentBonus* cached = system_->get_cached_bonus(entity);

  EXPECT_EQ(cached, nullptr);
}

/**
 * @brief recalculate_bonus on an invalid entity should not crash
 */
TEST_F(EquipmentBonusSystemTest, RecalculateBonusOnInvalidEntityDoesNotCrash) {
  auto entity = registry_.create();
  registry_.destroy(entity);

  // Should not throw or crash
  EXPECT_NO_FATAL_FAILURE(system_->recalculate_bonus(entity));
}

// ============================================================================
// Test 7: recalculate_bonus stores, get_cached_bonus retrieves
// ============================================================================

/**
 * @brief Before recalculate_bonus is called, get_cached_bonus returns nullptr
 */
TEST_F(EquipmentBonusSystemTest, CachedBonusNullBeforeRecalculate) {
  auto character = CreateCharacter();
  auto weapon = CreateItem(/*attack=*/10);
  EquipItem(character, 0, weapon);

  const EquipmentBonus* cached = system_->get_cached_bonus(character);

  EXPECT_EQ(cached, nullptr);
}

/**
 * @brief recalculate_bonus stores the bonus as a component, get_cached_bonus
 * retrieves the same values
 */
TEST_F(EquipmentBonusSystemTest, RecalculateStoresAndGetCachedRetrieves) {
  auto character = CreateCharacter();
  auto weapon = CreateItem(/*attack=*/10, /*defense=*/5,
                           /*magic_attack=*/3, /*magic_defense=*/2,
                           /*hp=*/100, /*mp=*/50,
                           /*hit_rate=*/4, /*dodge=*/3,
                           /*speed=*/1, /*luck=*/6);

  EquipItem(character, 0, weapon);

  system_->recalculate_bonus(character);

  const EquipmentBonus* cached = system_->get_cached_bonus(character);
  ASSERT_NE(cached, nullptr);

  EXPECT_EQ(cached->attack_bonus, 10);
  EXPECT_EQ(cached->defense_bonus, 5);
  EXPECT_EQ(cached->magic_attack_bonus, 3);
  EXPECT_EQ(cached->magic_defense_bonus, 2);
  EXPECT_EQ(cached->hp_bonus, 100);
  EXPECT_EQ(cached->mp_bonus, 50);
  EXPECT_EQ(cached->hit_rate_bonus, 4);
  EXPECT_EQ(cached->dodge_bonus, 3);
  EXPECT_EQ(cached->speed_bonus, 1);
  EXPECT_EQ(cached->luck_bonus, 6);
}

/**
 * @brief recalculate_bonus updates the cached component when equipment changes
 */
TEST_F(EquipmentBonusSystemTest, RecalculateUpdatesOnEquipmentChange) {
  auto character = CreateCharacter();
  auto weapon = CreateItem(/*attack=*/10);
  EquipItem(character, 0, weapon);

  system_->recalculate_bonus(character);

  const EquipmentBonus* cached = system_->get_cached_bonus(character);
  ASSERT_NE(cached, nullptr);
  EXPECT_EQ(cached->attack_bonus, 10);

  // Replace weapon with a stronger one
  auto better_weapon = CreateItem(/*attack=*/25);
  EquipItem(character, 0, better_weapon);

  system_->recalculate_bonus(character);

  cached = system_->get_cached_bonus(character);
  ASSERT_NE(cached, nullptr);
  EXPECT_EQ(cached->attack_bonus, 25);
}

/**
 * @brief calculate_total_bonus and cached bonus produce same results
 */
TEST_F(EquipmentBonusSystemTest, CalculateAndCachedAreConsistent) {
  auto character = CreateCharacter();
  auto weapon = CreateItem(/*attack=*/12, /*defense=*/4);
  auto armor = CreateItem(/*attack=*/0, /*defense=*/18,
                          /*magic_attack=*/0, /*magic_defense=*/7,
                          /*hp=*/200);
  EquipItem(character, 0, weapon);
  EquipItem(character, 1, armor);

  EquipmentBonus calculated = system_->calculate_total_bonus(character);
  system_->recalculate_bonus(character);

  const EquipmentBonus* cached = system_->get_cached_bonus(character);
  ASSERT_NE(cached, nullptr);

  EXPECT_EQ(cached->attack_bonus, calculated.attack_bonus);
  EXPECT_EQ(cached->defense_bonus, calculated.defense_bonus);
  EXPECT_EQ(cached->magic_attack_bonus, calculated.magic_attack_bonus);
  EXPECT_EQ(cached->magic_defense_bonus, calculated.magic_defense_bonus);
  EXPECT_EQ(cached->hp_bonus, calculated.hp_bonus);
  EXPECT_EQ(cached->mp_bonus, calculated.mp_bonus);
  EXPECT_EQ(cached->hit_rate_bonus, calculated.hit_rate_bonus);
  EXPECT_EQ(cached->dodge_bonus, calculated.dodge_bonus);
  EXPECT_EQ(cached->speed_bonus, calculated.speed_bonus);
  EXPECT_EQ(cached->luck_bonus, calculated.luck_bonus);
  EXPECT_EQ(cached->fire_damage, calculated.fire_damage);
  EXPECT_EQ(cached->ice_damage, calculated.ice_damage);
  EXPECT_EQ(cached->lightning_damage, calculated.lightning_damage);
  EXPECT_EQ(cached->poison_damage, calculated.poison_damage);
  EXPECT_EQ(cached->lifesteal_percent, calculated.lifesteal_percent);
  EXPECT_EQ(cached->reflect_percent, calculated.reflect_percent);
}

// ============================================================================
// Test 8: Null item entity in slot -> skipped
// ============================================================================

/**
 * @brief Slots containing entt::null should be silently skipped
 */
TEST_F(EquipmentBonusSystemTest, NullItemEntityInSlotIsSkipped) {
  auto character = CreateCharacter();

  // Equip only in slot 0, rest remain entt::null
  auto weapon = CreateItem(/*attack=*/10);
  EquipItem(character, 0, weapon);

  EquipmentBonus bonus = system_->calculate_total_bonus(character);

  // Should only count the weapon, not crash on null slots
  EXPECT_EQ(bonus.attack_bonus, 10);
}

/**
 * @brief Explicitly setting a slot to entt::null should be handled gracefully
 */
TEST_F(EquipmentBonusSystemTest, ExplicitNullSlotIsSkipped) {
  auto character = CreateCharacter();
  auto weapon = CreateItem(/*attack=*/10);
  EquipItem(character, 0, weapon);

  // Explicitly set another slot to null
  auto& equipment = registry_.get<EquipmentSlotComponent>(character);
  equipment.slots[1] = entt::null;

  EquipmentBonus bonus = system_->calculate_total_bonus(character);

  EXPECT_EQ(bonus.attack_bonus, 10);
}

// ============================================================================
// Test 9: Invalid (destroyed) item entity in slot -> skipped
// ============================================================================

/**
 * @brief An item entity that has been destroyed should be skipped
 */
TEST_F(EquipmentBonusSystemTest, DestroyedItemEntityInSlotIsSkipped) {
  auto character = CreateCharacter();

  auto weapon = CreateItem(/*attack=*/10);
  auto armor = CreateItem(/*attack=*/0, /*defense=*/20);

  EquipItem(character, 0, weapon);
  EquipItem(character, 1, armor);

  // Destroy the weapon entity, simulating item removal without clearing the slot
  registry_.destroy(weapon);

  EquipmentBonus bonus = system_->calculate_total_bonus(character);

  // Only armor should contribute
  EXPECT_EQ(bonus.attack_bonus, 0);
  EXPECT_EQ(bonus.defense_bonus, 20);
}

/**
 * @brief Multiple destroyed item entities should all be skipped
 */
TEST_F(EquipmentBonusSystemTest, MultipleDestroyedItemsAllSkipped) {
  auto character = CreateCharacter();

  auto weapon = CreateItem(/*attack=*/10);
  auto armor = CreateItem(/*attack=*/0, /*defense=*/20);
  auto helmet = CreateItem(/*attack=*/0, /*defense=*/5);

  EquipItem(character, 0, weapon);
  EquipItem(character, 1, armor);
  EquipItem(character, 2, helmet);

  registry_.destroy(weapon);
  registry_.destroy(armor);

  EquipmentBonus bonus = system_->calculate_total_bonus(character);

  EXPECT_EQ(bonus.attack_bonus, 0);
  EXPECT_EQ(bonus.defense_bonus, 5);
}

// ============================================================================
// Test 10: Item entity without ItemComponent -> skipped
// ============================================================================

/**
 * @brief A valid entity in a slot that lacks ItemComponent should be skipped
 */
TEST_F(EquipmentBonusSystemTest, ItemWithoutItemComponentIsSkipped) {
  auto character = CreateCharacter();

  // Create a bare entity (no ItemComponent) and put it in a slot
  auto bare_entity = registry_.create();
  EquipItem(character, 0, bare_entity);

  // Also equip a valid item in another slot
  auto armor = CreateItem(/*attack=*/0, /*defense=*/15);
  EquipItem(character, 1, armor);

  EquipmentBonus bonus = system_->calculate_total_bonus(character);

  // Only the armor should count
  EXPECT_EQ(bonus.attack_bonus, 0);
  EXPECT_EQ(bonus.defense_bonus, 15);
}

/**
 * @brief Mix of null, invalid, and componentless entities with valid items
 */
TEST_F(EquipmentBonusSystemTest, MixedSlotConditions) {
  auto character = CreateCharacter();

  // Slot 0: valid item with bonuses
  auto weapon = CreateItem(/*attack=*/20);
  EquipItem(character, 0, weapon);

  // Slot 1: entt::null (default)

  // Slot 2: entity without ItemComponent
  auto bare = registry_.create();
  EquipItem(character, 2, bare);

  // Slot 3: destroyed entity
  auto destroyed = CreateItem(/*attack=*/99);
  EquipItem(character, 3, destroyed);
  registry_.destroy(destroyed);

  // Slot 4: valid item with bonuses
  auto ring = CreateItem(/*attack=*/5);
  EquipItem(character, 4, ring);

  EquipmentBonus bonus = system_->calculate_total_bonus(character);

  EXPECT_EQ(bonus.attack_bonus, 25);  // 20 + 5
}

// ============================================================================
// Test 11: Special effects - lifesteal and reflect
// ============================================================================

/**
 * @brief Single item with lifesteal
 */
TEST_F(EquipmentBonusSystemTest, LifestealFromSingleItem) {
  auto character = CreateCharacter();
  auto weapon = CreateSpecialItem(/*lifesteal=*/5, /*reflect=*/0);
  EquipItem(character, 0, weapon);

  EquipmentBonus bonus = system_->calculate_total_bonus(character);

  EXPECT_EQ(bonus.lifesteal_percent, 5);
  EXPECT_EQ(bonus.reflect_percent, 0);
}

/**
 * @brief Single item with reflect
 */
TEST_F(EquipmentBonusSystemTest, ReflectFromSingleItem) {
  auto character = CreateCharacter();
  auto armor = CreateSpecialItem(/*lifesteal=*/0, /*reflect=*/10);
  EquipItem(character, 1, armor);

  EquipmentBonus bonus = system_->calculate_total_bonus(character);

  EXPECT_EQ(bonus.lifesteal_percent, 0);
  EXPECT_EQ(bonus.reflect_percent, 10);
}

/**
 * @brief Multiple items with lifesteal and reflect should sum
 */
TEST_F(EquipmentBonusSystemTest, LifestealAndReflectSumAcrossItems) {
  auto character = CreateCharacter();

  auto weapon = CreateSpecialItem(/*lifesteal=*/3, /*reflect=*/0);
  auto ring = CreateSpecialItem(/*lifesteal=*/2, /*reflect=*/5);
  auto necklace = CreateSpecialItem(/*lifesteal=*/0, /*reflect=*/8);

  EquipItem(character, 0, weapon);    // WEAPON
  EquipItem(character, 4, ring);      // RING_LEFT
  EquipItem(character, 6, necklace);  // NECKLACE

  EquipmentBonus bonus = system_->calculate_total_bonus(character);

  EXPECT_EQ(bonus.lifesteal_percent, 5);   // 3 + 2
  EXPECT_EQ(bonus.reflect_percent, 13);    // 5 + 8
}

/**
 * @brief Item with both lifesteal and reflect
 */
TEST_F(EquipmentBonusSystemTest, ItemWithBothLifestealAndReflect) {
  auto character = CreateCharacter();
  auto weapon = CreateSpecialItem(/*lifesteal=*/7, /*reflect=*/4);
  EquipItem(character, 0, weapon);

  EquipmentBonus bonus = system_->calculate_total_bonus(character);

  EXPECT_EQ(bonus.lifesteal_percent, 7);
  EXPECT_EQ(bonus.reflect_percent, 4);
}

// ============================================================================
// Additional edge case tests
// ============================================================================

/**
 * @brief Verify total_weight and hand_weight remain zero
 * (current implementation does not populate these fields)
 */
TEST_F(EquipmentBonusSystemTest, WeightFieldsDefaultToZero) {
  auto character = CreateCharacter();
  auto weapon = CreateItem(/*attack=*/10, /*defense=*/5);
  auto armor = CreateItem(/*attack=*/0, /*defense=*/20);
  EquipItem(character, 0, weapon);
  EquipItem(character, 1, armor);

  EquipmentBonus bonus = system_->calculate_total_bonus(character);

  EXPECT_EQ(bonus.total_weight, 0);
  EXPECT_EQ(bonus.hand_weight, 0);
}

/**
 * @brief Item with all bonus fields set should accumulate everything
 */
TEST_F(EquipmentBonusSystemTest, ItemWithAllFieldsSet) {
  auto character = CreateCharacter();

  auto entity = registry_.create();
  auto& item = registry_.emplace<ItemComponent>(entity);
  item.attack_bonus = 10;
  item.defense_bonus = 8;
  item.magic_attack_bonus = 6;
  item.magic_defense_bonus = 4;
  item.hp_bonus = 200;
  item.mp_bonus = 100;
  item.hit_rate_bonus = 5;
  item.dodge_bonus = 3;
  item.speed_bonus = 2;
  item.luck = 9;
  item.lifesteal_percent = 7;
  item.reflect_percent = 4;
  item.elemental_type = 1;  // Fire
  item.elemental_damage = 15;

  EquipItem(character, 0, entity);

  EquipmentBonus bonus = system_->calculate_total_bonus(character);

  EXPECT_EQ(bonus.attack_bonus, 10);
  EXPECT_EQ(bonus.defense_bonus, 8);
  EXPECT_EQ(bonus.magic_attack_bonus, 6);
  EXPECT_EQ(bonus.magic_defense_bonus, 4);
  EXPECT_EQ(bonus.hp_bonus, 200);
  EXPECT_EQ(bonus.mp_bonus, 100);
  EXPECT_EQ(bonus.hit_rate_bonus, 5);
  EXPECT_EQ(bonus.dodge_bonus, 3);
  EXPECT_EQ(bonus.speed_bonus, 2);
  EXPECT_EQ(bonus.luck_bonus, 9);
  EXPECT_EQ(bonus.lifesteal_percent, 7);
  EXPECT_EQ(bonus.reflect_percent, 4);
  EXPECT_EQ(bonus.fire_damage, 15);
  EXPECT_EQ(bonus.ice_damage, 0);
  EXPECT_EQ(bonus.lightning_damage, 0);
  EXPECT_EQ(bonus.poison_damage, 0);
}

/**
 * @brief Default-constructed EquipmentBonus has all fields zero
 */
TEST_F(EquipmentBonusSystemTest, DefaultConstructedBonusIsZero) {
  EquipmentBonus bonus{};
  ExpectZeroBonus(bonus);
}

/**
 * @brief Negative bonus values on items are accumulated correctly
 * (cursed items with negative luck, etc.)
 */
TEST_F(EquipmentBonusSystemTest, NegativeBonusValuesAccumulate) {
  auto character = CreateCharacter();

  // Cursed weapon with negative luck
  auto cursed = CreateItem(/*attack=*/20, /*defense=*/0,
                           /*magic_attack=*/0, /*magic_defense=*/0,
                           /*hp=*/0, /*mp=*/0,
                           /*hit_rate=*/0, /*dodge=*/0,
                           /*speed=*/0, /*luck=*/-3);

  // Normal ring with positive luck
  auto ring = CreateItem(/*attack=*/0, /*defense=*/0,
                         /*magic_attack=*/0, /*magic_defense=*/0,
                         /*hp=*/0, /*mp=*/0,
                         /*hit_rate=*/0, /*dodge=*/0,
                         /*speed=*/0, /*luck=*/5);

  EquipItem(character, 0, cursed);
  EquipItem(character, 4, ring);

  EquipmentBonus bonus = system_->calculate_total_bonus(character);

  EXPECT_EQ(bonus.attack_bonus, 20);
  EXPECT_EQ(bonus.luck_bonus, 2);  // -3 + 5
}

/**
 * @brief Same item entity referenced in multiple slots is counted once per
 * slot reference (the system iterates all slots independently)
 */
TEST_F(EquipmentBonusSystemTest, SameItemInMultipleSlotsCountedMultipleTimes) {
  auto character = CreateCharacter();
  auto item = CreateItem(/*attack=*/10);

  // Place the same item entity in two slots (unusual but tests the iteration)
  EquipItem(character, 0, item);
  EquipItem(character, 1, item);

  EquipmentBonus bonus = system_->calculate_total_bonus(character);

  // The system iterates all slots independently, so it counts twice
  EXPECT_EQ(bonus.attack_bonus, 20);
}

}  // namespace
}  // namespace mir2::ecs::test
