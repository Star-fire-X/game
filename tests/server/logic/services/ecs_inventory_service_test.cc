#include <gtest/gtest.h>

#include <entt/entt.hpp>

#include <atomic>
#include <cstdint>
#include <memory>

#include "common/types/constants.h"
#include "ecs/components/character_components.h"
#include "ecs/components/ground_item_component.h"
#include "ecs/components/item_component.h"
#include "ecs/registry_manager.h"
#include "logic/services/ecs_inventory_service.h"

namespace mir2::logic::test {
namespace {

class EcsInventoryServiceTest : public ::testing::Test {
 protected:
  void SetUp() override {
    static std::atomic<uint32_t> map_seq{10000};
    static std::atomic<uint32_t> char_seq{800000};

    map_id_ = map_seq.fetch_add(1, std::memory_order_relaxed);
    character_id_ = char_seq.fetch_add(1, std::memory_order_relaxed);

    registry_manager_ = &ecs::RegistryManager::Instance();
    character_manager_ = &registry_manager_->GetCharacterManager();
    character_manager_->BindToCurrentThread();

    world_ = registry_manager_->CreateWorld(map_id_);
    ASSERT_NE(world_, nullptr);
    registry_ = &world_->Registry();

    character_ = character_manager_->GetOrCreate(character_id_, map_id_);
    ASSERT_TRUE(character_ != entt::null);
    ASSERT_TRUE(registry_->valid(character_));

    auto& identity = registry_->get_or_emplace<ecs::CharacterIdentityComponent>(character_);
    identity.id = character_id_;

    auto& state = registry_->get_or_emplace<ecs::CharacterStateComponent>(character_);
    state.map_id = map_id_;
    state.is_online = true;
    state.is_in_activity = false;
    state.last_trade_close_time_ms = 0;

    service_ = std::make_unique<EcsInventoryService>(*registry_manager_);
  }

  entt::entity CreateInventoryItem(uint32_t item_id,
                                   uint64_t instance_id,
                                   int count,
                                   int slot_index) {
    const entt::entity item = registry_->create();

    auto& item_component = registry_->emplace<ecs::ItemComponent>(item);
    item_component.item_id = item_id;
    item_component.instance_id = instance_id;
    item_component.count = count;

    auto& owner = registry_->emplace<ecs::InventoryOwnerComponent>(item);
    owner.owner = character_;
    owner.slot_index = slot_index;

    return item;
  }

  entt::entity CreateGroundItem(uint32_t item_id,
                                uint64_t instance_id,
                                int count,
                                entt::entity pickup_owner = entt::null,
                                uint32_t map_id = 0) {
    if (map_id == 0) {
      map_id = map_id_;
    }

    const entt::entity item = registry_->create();

    auto& item_component = registry_->emplace<ecs::ItemComponent>(item);
    item_component.item_id = item_id;
    item_component.instance_id = instance_id;
    item_component.count = count;

    auto& owner = registry_->emplace<ecs::InventoryOwnerComponent>(item);
    owner.owner = pickup_owner;
    owner.slot_index = -1;

    auto& ground = registry_->emplace<ecs::GroundItemComponent>(item);
    ground.owner = pickup_owner;
    ground.map_id = map_id;
    ground.x = 10;
    ground.y = 20;

    return item;
  }

  void FillInventory() {
    for (int slot = 0; slot < common::constants::MAX_INVENTORY_SIZE; ++slot) {
      CreateInventoryItem(1000u + static_cast<uint32_t>(slot),
                          900000u + static_cast<uint64_t>(slot),
                          1,
                          slot);
    }
  }

  ecs::RegistryManager* registry_manager_ = nullptr;
  ecs::CharacterEntityManager* character_manager_ = nullptr;
  ecs::World* world_ = nullptr;
  entt::registry* registry_ = nullptr;
  std::unique_ptr<EcsInventoryService> service_;

  uint32_t map_id_ = 0;
  uint32_t character_id_ = 0;
  entt::entity character_ = entt::null;
};

TEST_F(EcsInventoryServiceTest, PickupRejectsInvalidInputs) {
  const ItemPickupResult bad_character = service_->PickupItem(0, 1);
  EXPECT_EQ(bad_character.code, common::ErrorCode::kInvalidAction);

  const ItemPickupResult bad_item = service_->PickupItem(character_id_, 0);
  EXPECT_EQ(bad_item.code, common::ErrorCode::kInvalidAction);
}

TEST_F(EcsInventoryServiceTest, PickupReturnsTargetNotFoundWhenGroundItemMissing) {
  const ItemPickupResult result = service_->PickupItem(character_id_, 123456);
  EXPECT_EQ(result.code, common::ErrorCode::kTargetNotFound);
}

TEST_F(EcsInventoryServiceTest, PickupReturnsInvalidActionWhenInventoryIsFull) {
  FillInventory();
  const entt::entity ground_item = CreateGroundItem(/*item_id=*/7001,
                                                    /*instance_id=*/555001,
                                                    /*count=*/1);

  const ItemPickupResult result = service_->PickupItem(character_id_, 555001);
  EXPECT_EQ(result.code, common::ErrorCode::kInvalidAction);

  ASSERT_TRUE(registry_->valid(ground_item));
  const auto* owner = registry_->try_get<ecs::InventoryOwnerComponent>(ground_item);
  ASSERT_NE(owner, nullptr);
  EXPECT_EQ(owner->slot_index, -1);
}

TEST_F(EcsInventoryServiceTest, PickupReturnsInvalidActionWhenOwnershipBlocksPickup) {
  const entt::entity owner = registry_->create();
  registry_->emplace<ecs::CharacterStateComponent>(owner);
  CreateGroundItem(/*item_id=*/7002,
                   /*instance_id=*/555002,
                   /*count=*/1,
                   /*pickup_owner=*/owner);

  const ItemPickupResult result = service_->PickupItem(character_id_, 555002);
  EXPECT_EQ(result.code, common::ErrorCode::kInvalidAction);
}

TEST_F(EcsInventoryServiceTest, PickupSucceedsAndMovesGroundItemToInventory) {
  const entt::entity ground_item = CreateGroundItem(/*item_id=*/7003,
                                                    /*instance_id=*/555003,
                                                    /*count=*/2);

  const ItemPickupResult result = service_->PickupItem(character_id_, 555003);
  EXPECT_EQ(result.code, common::ErrorCode::kOk);
  EXPECT_EQ(result.item_id, 555003u);

  ASSERT_TRUE(registry_->valid(ground_item));
  const auto* owner = registry_->try_get<ecs::InventoryOwnerComponent>(ground_item);
  ASSERT_NE(owner, nullptr);
  EXPECT_EQ(owner->owner, character_);
  EXPECT_GE(owner->slot_index, 0);
  EXPECT_FALSE(registry_->all_of<ecs::GroundItemComponent>(ground_item));
}

TEST_F(EcsInventoryServiceTest, UseItemCoversValidationAndSuccessPath) {
  const ItemUseResult invalid_slot = service_->UseItem(character_id_, 999, 7004);
  EXPECT_EQ(invalid_slot.code, common::ErrorCode::kInvalidAction);

  const ItemUseResult missing_item = service_->UseItem(character_id_, 0, 7004);
  EXPECT_EQ(missing_item.code, common::ErrorCode::kTargetNotFound);

  const entt::entity item = CreateInventoryItem(/*item_id=*/7004,
                                                /*instance_id=*/555004,
                                                /*count=*/1,
                                                /*slot_index=*/0);

  const ItemUseResult mismatch = service_->UseItem(character_id_, 0, 9999);
  EXPECT_EQ(mismatch.code, common::ErrorCode::kTargetNotFound);

  const ItemUseResult success = service_->UseItem(character_id_, 0, 7004);
  EXPECT_EQ(success.code, common::ErrorCode::kOk);
  EXPECT_EQ(success.remaining, 0u);
  EXPECT_FALSE(registry_->valid(item));
}

TEST_F(EcsInventoryServiceTest, DropItemCoversPartialAndSuccessPath) {
  const entt::entity item = CreateInventoryItem(/*item_id=*/7005,
                                                /*instance_id=*/555005,
                                                /*count=*/3,
                                                /*slot_index=*/1);

  const ItemDropResult partial = service_->DropItem(character_id_, 1, 7005, 1);
  EXPECT_EQ(partial.code, common::ErrorCode::kInvalidAction);

  const ItemDropResult success = service_->DropItem(character_id_, 1, 7005, 3);
  EXPECT_EQ(success.code, common::ErrorCode::kOk);

  ASSERT_TRUE(registry_->valid(item));
  const auto* owner = registry_->try_get<ecs::InventoryOwnerComponent>(item);
  ASSERT_NE(owner, nullptr);
  EXPECT_TRUE(owner->owner == entt::null);
  EXPECT_EQ(owner->slot_index, -1);
}

TEST_F(EcsInventoryServiceTest, DropItemReturnsInvalidActionWhenInventorySystemRejects) {
  CreateInventoryItem(/*item_id=*/7006,
                      /*instance_id=*/555006,
                      /*count=*/1,
                      /*slot_index=*/2);

  auto* state = registry_->try_get<ecs::CharacterStateComponent>(character_);
  ASSERT_NE(state, nullptr);
  state->is_in_activity = true;

  const ItemDropResult result = service_->DropItem(character_id_, 2, 7006, 1);
  EXPECT_EQ(result.code, common::ErrorCode::kInvalidAction);
}

}  // namespace
}  // namespace mir2::logic::test
