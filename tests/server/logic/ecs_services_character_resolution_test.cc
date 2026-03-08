#include <gtest/gtest.h>

#include <array>
#include <cstdint>

#include "combat_generated.h"
#include "ecs/components/character_components.h"
#include "ecs/components/monster_component.h"
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

TEST(EcsServicesCharacterResolutionTest, AttackMonsterTargetResolvesInAttackerWorldRegistry) {
  auto& registry_manager = ecs::RegistryManager::Instance();
  auto& character_manager = registry_manager.GetCharacterManager();
  character_manager.BindToCurrentThread();

  constexpr uint32_t kMapId = 910001;
  constexpr uint32_t kAttackerId = 920001;

  auto* world = registry_manager.CreateWorld(kMapId);
  ASSERT_NE(world, nullptr);

  const entt::entity attacker = character_manager.GetOrCreate(kAttackerId, kMapId);
  ASSERT_TRUE(attacker != entt::null);
  auto& attacker_state = world->Registry().get<ecs::CharacterStateComponent>(attacker);
  attacker_state.map_id = kMapId;
  attacker_state.position = {10, 10};
  auto& attacker_attrs = world->Registry().get<ecs::CharacterAttributesComponent>(attacker);
  attacker_attrs.hp = 100;
  attacker_attrs.max_hp = 100;
  attacker_attrs.attack = 30;

  const entt::entity monster = world->Registry().create();
  auto& monster_identity = world->Registry().emplace<ecs::MonsterIdentityComponent>(monster);
  monster_identity.monster_template_id = 7001;
  monster_identity.spawn_point_id = 0;
  auto& monster_state = world->Registry().emplace<ecs::CharacterStateComponent>(monster);
  monster_state.map_id = kMapId;
  monster_state.position = {10, 10};
  auto& monster_attrs = world->Registry().emplace<ecs::CharacterAttributesComponent>(monster);
  monster_attrs.hp = 60;
  monster_attrs.max_hp = 60;
  monster_attrs.defense = 0;

  EcsCombatService service(registry_manager);
  const CombatResult result = service.Attack(
      kAttackerId,
      static_cast<uint64_t>(entt::to_integral(monster)),
      mir2::proto::EntityType::MONSTER);

  EXPECT_NE(result.code, mir2::common::ErrorCode::kTargetNotFound);
}

TEST(EcsServicesCharacterResolutionTest, UseSkillMonsterTargetResolvesInCasterWorldRegistry) {
  auto& registry_manager = ecs::RegistryManager::Instance();
  auto& character_manager = registry_manager.GetCharacterManager();
  character_manager.BindToCurrentThread();

  constexpr uint32_t kMapId = 910002;
  constexpr uint32_t kCasterId = 920002;

  auto* world = registry_manager.CreateWorld(kMapId);
  ASSERT_NE(world, nullptr);

  const entt::entity caster = character_manager.GetOrCreate(kCasterId, kMapId);
  ASSERT_TRUE(caster != entt::null);
  auto& caster_state = world->Registry().get<ecs::CharacterStateComponent>(caster);
  caster_state.map_id = kMapId;
  caster_state.position = {15, 15};
  auto& caster_attrs = world->Registry().get<ecs::CharacterAttributesComponent>(caster);
  caster_attrs.hp = 100;
  caster_attrs.max_hp = 100;
  caster_attrs.mp = 100;
  caster_attrs.max_mp = 100;
  caster_attrs.attack = 30;

  const entt::entity monster = world->Registry().create();
  auto& monster_identity = world->Registry().emplace<ecs::MonsterIdentityComponent>(monster);
  monster_identity.monster_template_id = 7002;
  monster_identity.spawn_point_id = 0;
  auto& monster_state = world->Registry().emplace<ecs::CharacterStateComponent>(monster);
  monster_state.map_id = kMapId;
  monster_state.position = {15, 15};
  auto& monster_attrs = world->Registry().emplace<ecs::CharacterAttributesComponent>(monster);
  monster_attrs.hp = 80;
  monster_attrs.max_hp = 80;
  monster_attrs.defense = 0;

  EcsCombatService service(registry_manager);
  const CombatResult result = service.UseSkill(
      kCasterId,
      static_cast<uint64_t>(entt::to_integral(monster)),
      /*skill_id=*/1001);

  EXPECT_NE(result.code, mir2::common::ErrorCode::kTargetNotFound);
}

TEST(EcsServicesCharacterResolutionTest, UseSkillPrefersPlayerTargetOverMonsterWithSameNumericId) {
  auto& registry_manager = ecs::RegistryManager::Instance();
  auto& character_manager = registry_manager.GetCharacterManager();
  character_manager.BindToCurrentThread();

  constexpr uint32_t kMapId = 910003;
  constexpr uint32_t kCasterId = 920003;
  constexpr uint32_t kTargetPlayerId = 1;

  auto* world = registry_manager.CreateWorld(kMapId);
  ASSERT_NE(world, nullptr);

  const entt::entity caster = character_manager.GetOrCreate(kCasterId, kMapId);
  ASSERT_TRUE(caster != entt::null);
  auto& caster_state = world->Registry().get<ecs::CharacterStateComponent>(caster);
  caster_state.map_id = kMapId;
  caster_state.position = {20, 20};
  auto& caster_attrs = world->Registry().get<ecs::CharacterAttributesComponent>(caster);
  caster_attrs.hp = 100;
  caster_attrs.max_hp = 100;
  caster_attrs.mp = 100;
  caster_attrs.max_mp = 100;

  const entt::entity monster = world->Registry().create(entt::entity{1});
  auto& monster_identity = world->Registry().emplace<ecs::MonsterIdentityComponent>(monster);
  monster_identity.monster_template_id = 7003;
  auto& monster_state = world->Registry().emplace<ecs::CharacterStateComponent>(monster);
  monster_state.map_id = kMapId;
  monster_state.position = {20, 20};
  auto& monster_attrs = world->Registry().emplace<ecs::CharacterAttributesComponent>(monster);
  monster_attrs.hp = 80;
  monster_attrs.max_hp = 80;

  const entt::entity target_player = character_manager.GetOrCreate(kTargetPlayerId, kMapId);
  ASSERT_TRUE(target_player != entt::null);
  ASSERT_NE(target_player, monster);
  auto& target_state = world->Registry().get<ecs::CharacterStateComponent>(target_player);
  target_state.map_id = kMapId;
  target_state.position = {20, 20};
  auto& target_attrs = world->Registry().get<ecs::CharacterAttributesComponent>(target_player);
  target_attrs.hp = 90;
  target_attrs.max_hp = 90;

  EcsCombatService service(registry_manager);
  const CombatResult result = service.UseSkill(kCasterId, kTargetPlayerId, /*skill_id=*/1001);

  EXPECT_NE(result.code, mir2::common::ErrorCode::kTargetNotFound);
  EXPECT_LT(target_attrs.hp, 90);
  EXPECT_EQ(monster_attrs.hp, 80);
}

}  // namespace mir2::logic::test
