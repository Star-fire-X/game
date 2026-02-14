#include <gtest/gtest.h>

#include <array>
#include <cstdint>

#include "combat_generated.h"
#include "ecs/registry_manager.h"
#include "logic/services/ecs_combat_service.h"
#include "logic/services/ecs_inventory_service.h"

namespace mir2::logic::test {
namespace {

uint32_t FindUnusedCharacterId(ecs::CharacterEntityManager& character_manager) {
  constexpr std::array<uint32_t, 8> kCandidates = {
      4294967294u, 4294967293u, 4294967292u, 4294967291u,
      4294967290u, 4294967289u, 4294967288u, 4294967287u};

  for (const auto candidate : kCandidates) {
    if (!character_manager.TryGet(candidate).has_value() &&
        character_manager.TryGetRegistry(candidate) == nullptr) {
      return candidate;
    }
  }
  return kCandidates.back();
}

}  // namespace

TEST(EcsServicesCharacterResolutionTest, AttackMissingAttackerDoesNotCreateCharacter) {
  auto& registry_manager = ecs::RegistryManager::Instance();
  auto& character_manager = registry_manager.GetCharacterManager();
  character_manager.BindToCurrentThread();

  const uint32_t attacker_id = FindUnusedCharacterId(character_manager);
  ASSERT_FALSE(character_manager.TryGet(attacker_id).has_value());

  const size_t before_size = character_manager.GetIndexSize();
  EcsCombatService service(registry_manager);
  const CombatResult result =
      service.Attack(attacker_id, 1, mir2::proto::EntityType::PLAYER);

  EXPECT_EQ(result.code, mir2::common::ErrorCode::kInvalidAction);
  EXPECT_EQ(character_manager.GetIndexSize(), before_size);
  EXPECT_FALSE(character_manager.TryGet(attacker_id).has_value());
}

TEST(EcsServicesCharacterResolutionTest, UseSkillMissingCasterDoesNotCreateCharacter) {
  auto& registry_manager = ecs::RegistryManager::Instance();
  auto& character_manager = registry_manager.GetCharacterManager();
  character_manager.BindToCurrentThread();

  const uint32_t caster_id = FindUnusedCharacterId(character_manager);
  ASSERT_FALSE(character_manager.TryGet(caster_id).has_value());

  const size_t before_size = character_manager.GetIndexSize();
  EcsCombatService service(registry_manager);
  const CombatResult result = service.UseSkill(caster_id, 1, 1001);

  EXPECT_EQ(result.code, mir2::common::ErrorCode::kInvalidAction);
  EXPECT_EQ(character_manager.GetIndexSize(), before_size);
  EXPECT_FALSE(character_manager.TryGet(caster_id).has_value());
}

TEST(EcsServicesCharacterResolutionTest, PickupMissingCharacterDoesNotCreateCharacter) {
  auto& registry_manager = ecs::RegistryManager::Instance();
  auto& character_manager = registry_manager.GetCharacterManager();
  character_manager.BindToCurrentThread();

  const uint32_t character_id = FindUnusedCharacterId(character_manager);
  ASSERT_FALSE(character_manager.TryGet(character_id).has_value());

  const size_t before_size = character_manager.GetIndexSize();
  EcsInventoryService service(registry_manager);
  const ItemPickupResult result = service.PickupItem(character_id, 1001);

  EXPECT_EQ(result.code, mir2::common::ErrorCode::kInvalidAction);
  EXPECT_EQ(character_manager.GetIndexSize(), before_size);
  EXPECT_FALSE(character_manager.TryGet(character_id).has_value());
}

TEST(EcsServicesCharacterResolutionTest, UseItemMissingCharacterDoesNotCreateCharacter) {
  auto& registry_manager = ecs::RegistryManager::Instance();
  auto& character_manager = registry_manager.GetCharacterManager();
  character_manager.BindToCurrentThread();

  const uint32_t character_id = FindUnusedCharacterId(character_manager);
  ASSERT_FALSE(character_manager.TryGet(character_id).has_value());

  const size_t before_size = character_manager.GetIndexSize();
  EcsInventoryService service(registry_manager);
  const ItemUseResult result = service.UseItem(character_id, 0, 1001);

  EXPECT_EQ(result.code, mir2::common::ErrorCode::kInvalidAction);
  EXPECT_EQ(character_manager.GetIndexSize(), before_size);
  EXPECT_FALSE(character_manager.TryGet(character_id).has_value());
}

TEST(EcsServicesCharacterResolutionTest, DropItemMissingCharacterDoesNotCreateCharacter) {
  auto& registry_manager = ecs::RegistryManager::Instance();
  auto& character_manager = registry_manager.GetCharacterManager();
  character_manager.BindToCurrentThread();

  const uint32_t character_id = FindUnusedCharacterId(character_manager);
  ASSERT_FALSE(character_manager.TryGet(character_id).has_value());

  const size_t before_size = character_manager.GetIndexSize();
  EcsInventoryService service(registry_manager);
  const ItemDropResult result = service.DropItem(character_id, 0, 1001, 1);

  EXPECT_EQ(result.code, mir2::common::ErrorCode::kInvalidAction);
  EXPECT_EQ(character_manager.GetIndexSize(), before_size);
  EXPECT_FALSE(character_manager.TryGet(character_id).has_value());
}

}  // namespace mir2::logic::test
