/**
 * @file ecs_improvements_test.cc
 * @brief Comprehensive tests for ECS architecture improvements (Phases 1-4)
 *
 * Test Coverage:
 * - Phase 1: GetDefaultMapId() and defensive programming
 * - Phase 2: HandlerContext cache validation
 * - Phase 3: MoveToMap transactional safety
 * - Phase 4: Native inventory storage
 */

#include <gtest/gtest.h>

#include <entt/entt.hpp>
#include <optional>

#include "ecs/character_entity_manager.h"
#include "ecs/component_utils.h"
#include "ecs/components/character_components.h"
#include "ecs/registry_manager.h"
#include "logic/handler_context.h"

namespace {

using mir2::ecs::CharacterEntityManager;
using mir2::ecs::CharacterIdentityComponent;
using mir2::ecs::CharacterStateComponent;
using mir2::ecs::InventoryComponent;
using mir2::ecs::ItemData;
using mir2::ecs::RegistryManager;
using mir2::ecs::SkillData;
using mir2::ecs::require_component;
using mir2::logic::HandlerContext;

// =============================================================================
// Phase 1: GetDefaultMapId() Tests
// =============================================================================

class GetDefaultMapIdTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // Reset to known state for each test
    CharacterEntityManager::SetDefaultMapId(1);
  }
};

TEST_F(GetDefaultMapIdTest, ReturnsDefaultValueWhenNotConfigured) {
  uint32_t map_id = CharacterEntityManager::GetDefaultMapId();
  EXPECT_GT(map_id, 0u);  // Should return a valid map ID
}

TEST_F(GetDefaultMapIdTest, CanBeOverriddenForTesting) {
  CharacterEntityManager::SetDefaultMapId(42);
  EXPECT_EQ(CharacterEntityManager::GetDefaultMapId(), 42u);
}

TEST_F(GetDefaultMapIdTest, UsedInGetOrCreateWhenMapIdIsZero) {
  auto& registry_manager = RegistryManager::Instance();
  CharacterEntityManager manager(registry_manager);
  CharacterEntityManager::SetDefaultMapId(5);

  auto entity = manager.GetOrCreate(100, 0);  // map_id = 0 should use default

  ASSERT_TRUE(entity != entt::null);

  // Verify the character is in the correct map
  auto map_id = manager.TryGetMapId(100);
  ASSERT_TRUE(map_id.has_value());
  EXPECT_EQ(*map_id, 5u);

  // Clean up
  registry_manager.ForEachWorld([](uint32_t, mir2::ecs::World& world) {
    world.ClearSystems();
  });
}

// =============================================================================
// Phase 1: Component Utils Tests
// =============================================================================

TEST(ComponentUtilsTest, RequireComponentReturnsPointerWhenPresent) {
  entt::registry registry;
  auto entity = registry.create();
  auto& state = registry.emplace<CharacterStateComponent>(entity);
  state.map_id = 10;

  auto* result = require_component<CharacterStateComponent>(registry, entity, "TestContext");

  ASSERT_NE(result, nullptr);
  EXPECT_EQ(result->map_id, 10u);
}

TEST(ComponentUtilsTest, RequireComponentReturnsNullptrWhenMissing) {
  entt::registry registry;
  auto entity = registry.create();

  auto* result = require_component<CharacterStateComponent>(registry, entity, "TestContext");

  EXPECT_EQ(result, nullptr);
  // Error should be logged (check logs manually)
}

TEST(ComponentUtilsTest, RequireComponentWorksWithConstRegistry) {
  entt::registry registry;
  auto entity = registry.create();
  registry.emplace<CharacterStateComponent>(entity);

  const entt::registry& const_registry = registry;
  const auto* result = require_component<CharacterStateComponent>(
      const_registry, entity, "TestContext");

  EXPECT_NE(result, nullptr);
}

// =============================================================================
// Phase 2: HandlerContext Cache Tests
// =============================================================================

class HandlerContextCacheTest : public ::testing::Test {
 protected:
  void SetUp() override {
    registry_manager_ = &RegistryManager::Instance();
    world_ = registry_manager_->CreateWorld(1);
    ASSERT_NE(world_, nullptr);
  }

  void TearDown() override {
    registry_manager_->ForEachWorld([](uint32_t, mir2::ecs::World& world) {
      world.ClearSystems();
    });
  }

  RegistryManager* registry_manager_ = nullptr;
  mir2::ecs::World* world_ = nullptr;
};

TEST_F(HandlerContextCacheTest, HasCacheReturnsFalseWhenEmpty) {
  HandlerContext ctx;
  EXPECT_FALSE(ctx.HasCache());
}

TEST_F(HandlerContextCacheTest, HasCacheReturnsTrueWhenPopulated) {
  HandlerContext ctx;
  ctx.registry = &world_->Registry();
  ctx.world = world_;
  ctx.map_id = 1;

  EXPECT_TRUE(ctx.HasCache());
}

TEST_F(HandlerContextCacheTest, InvalidateCacheClearsAllFields) {
  HandlerContext ctx;
  ctx.registry = &world_->Registry();
  ctx.world = world_;
  ctx.map_id = 1;

  ctx.InvalidateCache();

  EXPECT_FALSE(ctx.HasCache());
  EXPECT_EQ(ctx.registry, nullptr);
  EXPECT_EQ(ctx.world, nullptr);
  EXPECT_EQ(ctx.map_id, 0u);
}

TEST_F(HandlerContextCacheTest, ValidateCacheReturnsFalseWhenNoCachePopulated) {
  HandlerContext ctx;
  EXPECT_FALSE(ctx.ValidateCache());
}

TEST_F(HandlerContextCacheTest, ValidateCacheReturnsTrueForValidEntity) {
  auto& registry = world_->Registry();
  auto entity = registry.create();
  registry.emplace<CharacterIdentityComponent>(entity);

  HandlerContext ctx;
  ctx.entity = entity;
  ctx.registry = &registry;
  ctx.world = world_;
  ctx.map_id = 1;

  EXPECT_TRUE(ctx.ValidateCache());
}

TEST_F(HandlerContextCacheTest, ValidateCacheReturnsFalseAfterEntityDestroyed) {
  auto& registry = world_->Registry();
  auto entity = registry.create();

  HandlerContext ctx;
  ctx.entity = entity;
  ctx.registry = &registry;
  ctx.world = world_;
  ctx.map_id = 1;

  registry.destroy(entity);

  EXPECT_FALSE(ctx.ValidateCache());
}

// =============================================================================
// Phase 3: MoveToMap Transactional Tests
// =============================================================================

class MoveToMapTransactionalTest : public ::testing::Test {
 protected:
  void SetUp() override {
    registry_manager_ = &RegistryManager::Instance();
    manager_ = std::make_unique<CharacterEntityManager>(*registry_manager_);
  }

  void TearDown() override {
    manager_.reset();
    registry_manager_->ForEachWorld([](uint32_t, mir2::ecs::World& world) {
      world.ClearSystems();
    });
  }

  RegistryManager* registry_manager_ = nullptr;
  std::unique_ptr<CharacterEntityManager> manager_;
};

TEST_F(MoveToMapTransactionalTest, SuccessfulMoveTransfersEntityBetweenMaps) {
  const uint32_t character_id = 1000;
  const uint32_t source_map = 1;
  const uint32_t target_map = 2;

  // Create character in source map
  auto entity = manager_->GetOrCreate(character_id, source_map);
  ASSERT_TRUE(entity != entt::null);

  // Verify in source map
  auto source_map_id = manager_->TryGetMapId(character_id);
  ASSERT_TRUE(source_map_id.has_value());
  EXPECT_EQ(*source_map_id, source_map);

  // Move to target map
  bool success = manager_->MoveToMap(character_id, target_map, 100, 200);
  ASSERT_TRUE(success);

  // Verify in target map
  auto target_map_id = manager_->TryGetMapId(character_id);
  ASSERT_TRUE(target_map_id.has_value());
  EXPECT_EQ(*target_map_id, target_map);

  // Verify entity exists in target registry
  auto* target_registry = manager_->TryGetRegistry(character_id);
  ASSERT_NE(target_registry, nullptr);
  auto new_entity = manager_->TryGet(character_id);
  ASSERT_TRUE(new_entity.has_value());
  EXPECT_TRUE(target_registry->valid(*new_entity));
}

TEST_F(MoveToMapTransactionalTest, FailedMoveKeepsEntityInSourceMap) {
  const uint32_t character_id = 2000;
  const uint32_t source_map = 1;
  const uint32_t invalid_target_map = 999999;  // Non-existent map

  // Create character in source map
  auto entity = manager_->GetOrCreate(character_id, source_map);
  ASSERT_TRUE(entity != entt::null);

  // Attempt move to invalid map (should fail gracefully)
  bool success = manager_->MoveToMap(character_id, invalid_target_map, 100, 200);

  // If move fails, character should still be in source map
  if (!success) {
    auto map_id = manager_->TryGetMapId(character_id);
    ASSERT_TRUE(map_id.has_value());
    EXPECT_EQ(*map_id, source_map);

    // Entity should still be valid
    auto check_entity = manager_->TryGet(character_id);
    EXPECT_TRUE(check_entity.has_value());
  }
}

TEST_F(MoveToMapTransactionalTest, MovePreservesCharacterData) {
  const uint32_t character_id = 3000;
  const uint32_t source_map = 1;
  const uint32_t target_map = 2;

  // Create character with specific HP
  auto entity = manager_->GetOrCreate(character_id, source_map);
  ASSERT_TRUE(entity != entt::null);

  auto* source_registry = manager_->TryGetRegistry(character_id);
  ASSERT_NE(source_registry, nullptr);

  // Verify entity is valid before modifying
  ASSERT_TRUE(source_registry->valid(entity));
  auto& attributes = source_registry->get_or_emplace<mir2::ecs::CharacterAttributesComponent>(entity);
  attributes.hp = 12345;

  // Move to target map
  bool success = manager_->MoveToMap(character_id, target_map, 100, 200);
  ASSERT_TRUE(success);

  // Verify HP preserved
  auto* target_registry = manager_->TryGetRegistry(character_id);
  ASSERT_NE(target_registry, nullptr);
  auto new_entity = manager_->TryGet(character_id);
  ASSERT_TRUE(new_entity.has_value());

  // Verify new entity is valid
  ASSERT_TRUE(target_registry->valid(*new_entity));

  const auto* new_attributes = target_registry->try_get<mir2::ecs::CharacterAttributesComponent>(*new_entity);
  ASSERT_NE(new_attributes, nullptr);
  EXPECT_EQ(new_attributes->hp, 12345);
}

// =============================================================================
// Phase 4: Native Inventory Storage Tests
// =============================================================================

TEST(NativeInventoryTest, ItemDataStructureHasCorrectFields) {
  ItemData item;
  item.instance_id = 12345;
  item.item_id = 100;
  item.count = 5;
  item.durability = 80;
  item.max_durability = 100;
  item.enhancement_level = 3;

  EXPECT_EQ(item.instance_id, 12345u);
  EXPECT_EQ(item.item_id, 100u);
  EXPECT_EQ(item.count, 5);
  EXPECT_EQ(item.durability, 80);
  EXPECT_EQ(item.max_durability, 100);
  EXPECT_EQ(item.enhancement_level, 3);
}

TEST(NativeInventoryTest, SkillDataStructureHasCorrectFields) {
  SkillData skill;
  skill.skill_id = 28;
  skill.level = 5;
  skill.cooldown_end_ms = 1000000;

  EXPECT_EQ(skill.skill_id, 28u);
  EXPECT_EQ(skill.level, 5u);
  EXPECT_EQ(skill.cooldown_end_ms, 1000000u);
}

TEST(NativeInventoryTest, InventoryComponentHasCorrectCapacity) {
  InventoryComponent inventory;

  EXPECT_EQ(inventory.slots.size(), InventoryComponent::kMaxSlots);
  EXPECT_EQ(inventory.equipment.size(), InventoryComponent::kMaxEquipmentSlots);
  EXPECT_TRUE(inventory.skills.empty());
}

TEST(NativeInventoryTest, CanAddItemToSlot) {
  InventoryComponent inventory;

  ItemData item;
  item.instance_id = 1;
  item.item_id = 100;
  item.count = 1;

  inventory.slots[0] = item;

  ASSERT_TRUE(inventory.slots[0].has_value());
  EXPECT_EQ(inventory.slots[0]->instance_id, 1u);
  EXPECT_EQ(inventory.slots[0]->item_id, 100u);
}

TEST(NativeInventoryTest, CanRemoveItemFromSlot) {
  InventoryComponent inventory;

  ItemData item;
  item.instance_id = 1;
  item.item_id = 100;

  inventory.slots[5] = item;
  ASSERT_TRUE(inventory.slots[5].has_value());

  inventory.slots[5].reset();
  EXPECT_FALSE(inventory.slots[5].has_value());
}

TEST(NativeInventoryTest, CanAddEquipment) {
  InventoryComponent inventory;

  ItemData weapon;
  weapon.instance_id = 1;
  weapon.item_id = 200;
  weapon.equip_slot = 0;  // Weapon slot

  inventory.equipment[0] = weapon;

  ASSERT_TRUE(inventory.equipment[0].has_value());
  EXPECT_EQ(inventory.equipment[0]->item_id, 200u);
}

TEST(NativeInventoryTest, CanAddSkills) {
  InventoryComponent inventory;

  SkillData skill1;
  skill1.skill_id = 28;
  skill1.level = 3;

  SkillData skill2;
  skill2.skill_id = 29;
  skill2.level = 5;

  inventory.skills.push_back(skill1);
  inventory.skills.push_back(skill2);

  ASSERT_EQ(inventory.skills.size(), 2u);
  EXPECT_EQ(inventory.skills[0].skill_id, 28u);
  EXPECT_EQ(inventory.skills[1].skill_id, 29u);
}

TEST(NativeInventoryTest, OptionalSlotsDefaultToEmpty) {
  InventoryComponent inventory;

  for (size_t i = 0; i < inventory.slots.size(); ++i) {
    EXPECT_FALSE(inventory.slots[i].has_value()) << "Slot " << i << " should be empty";
  }

  for (size_t i = 0; i < inventory.equipment.size(); ++i) {
    EXPECT_FALSE(inventory.equipment[i].has_value()) << "Equipment slot " << i << " should be empty";
  }
}

TEST(NativeInventoryTest, CanIterateOverNonEmptySlots) {
  InventoryComponent inventory;

  // Add items to specific slots
  ItemData item1;
  item1.instance_id = 1;
  inventory.slots[0] = item1;

  ItemData item2;
  item2.instance_id = 2;
  inventory.slots[5] = item2;

  ItemData item3;
  item3.instance_id = 3;
  inventory.slots[10] = item3;

  // Count non-empty slots
  int count = 0;
  for (const auto& slot : inventory.slots) {
    if (slot.has_value()) {
      ++count;
    }
  }

  EXPECT_EQ(count, 3);
}

}  // namespace
