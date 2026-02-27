/**
 * @file storage_system_test.cc
 * @brief Unit tests for StorageSystem deposit/withdraw/query operations.
 */

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <vector>

#include <entt/entt.hpp>

#include "common/types/constants.h"
#include "ecs/components/character_components.h"
#include "ecs/components/item_component.h"
#include "ecs/components/storage_component.h"
#include "ecs/dirty_tracker.h"
#include "ecs/event_bus.h"
#include "ecs/events/storage_events.h"
#include "ecs/systems/storage_system.h"

namespace {

using mir2::ecs::DirtyComponent;
using mir2::ecs::EventBus;
using mir2::ecs::InventoryOwnerComponent;
using mir2::ecs::ItemComponent;
using mir2::ecs::StorageComponent;
using mir2::ecs::StorageSystem;
using mir2::ecs::kMaxStorageSlots;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

entt::entity CreateCharacter(entt::registry& registry) {
    return registry.create();
}

entt::entity CreateItemInInventory(entt::registry& registry,
                                   entt::entity owner,
                                   uint32_t item_id,
                                   int slot) {
    auto item = registry.create();
    auto& ic = registry.emplace<ItemComponent>(item);
    ic.item_id = item_id;
    ic.count = 1;
    auto& oc = registry.emplace<InventoryOwnerComponent>(item);
    oc.owner = owner;
    oc.slot_index = slot;
    return item;
}

void FillStorageSlots(entt::registry& registry,
                      entt::entity character,
                      int count) {
    auto& storage = registry.get_or_emplace<StorageComponent>(character);
    for (int i = 0; i < count && i < kMaxStorageSlots; ++i) {
        auto filler = registry.create();
        registry.emplace<ItemComponent>(filler).item_id =
            static_cast<uint32_t>(9000 + i);
        storage.slots[static_cast<std::size_t>(i)] = filler;
    }
}

void FillInventorySlots(entt::registry& registry, entt::entity character) {
    constexpr int kMaxInvSlots = mir2::common::constants::MAX_INVENTORY_SIZE;
    for (int i = 0; i < kMaxInvSlots; ++i) {
        CreateItemInInventory(registry, character, static_cast<uint32_t>(8000 + i), i);
    }
}

// ---------------------------------------------------------------------------
// DepositItem tests
// ---------------------------------------------------------------------------

TEST(StorageSystemTest, DepositItemSuccess) {
    entt::registry registry;
    auto character = CreateCharacter(registry);
    auto item = CreateItemInInventory(registry, character, 1001, 0);

    EXPECT_TRUE(StorageSystem::DepositItem(registry, character, item));

    auto* storage = registry.try_get<StorageComponent>(character);
    ASSERT_NE(storage, nullptr);
    EXPECT_EQ(storage->slots[0], item);
    EXPECT_FALSE(registry.all_of<InventoryOwnerComponent>(item));
}

TEST(StorageSystemTest, DepositItemInvalidCharacter) {
    entt::registry registry;
    auto character = CreateCharacter(registry);
    auto item = CreateItemInInventory(registry, character, 1001, 0);
    registry.destroy(character);

    EXPECT_FALSE(StorageSystem::DepositItem(registry, character, item));
}

TEST(StorageSystemTest, DepositItemInvalidItem) {
    entt::registry registry;
    auto character = CreateCharacter(registry);
    auto item = registry.create();
    registry.destroy(item);

    EXPECT_FALSE(StorageSystem::DepositItem(registry, character, item));
}

TEST(StorageSystemTest, DepositItemNotOwned) {
    entt::registry registry;
    auto character = CreateCharacter(registry);
    auto other = CreateCharacter(registry);
    auto item = CreateItemInInventory(registry, other, 1001, 0);

    EXPECT_FALSE(StorageSystem::DepositItem(registry, character, item));
}

TEST(StorageSystemTest, DepositItemNoItemComponent) {
    entt::registry registry;
    auto character = CreateCharacter(registry);

    auto item = registry.create();
    auto& oc = registry.emplace<InventoryOwnerComponent>(item);
    oc.owner = character;
    oc.slot_index = 0;
    // No ItemComponent attached.

    EXPECT_FALSE(StorageSystem::DepositItem(registry, character, item));
}

TEST(StorageSystemTest, DepositItemNoOwnerComponent) {
    entt::registry registry;
    auto character = CreateCharacter(registry);

    auto item = registry.create();
    registry.emplace<ItemComponent>(item).item_id = 1001;
    // No InventoryOwnerComponent attached.

    EXPECT_FALSE(StorageSystem::DepositItem(registry, character, item));
}

TEST(StorageSystemTest, DepositItemInvalidSlotIndex) {
    entt::registry registry;
    auto character = CreateCharacter(registry);
    // slot_index = -1 is invalid for deposit (not a valid inventory slot).
    auto item = CreateItemInInventory(registry, character, 1001, -1);

    EXPECT_FALSE(StorageSystem::DepositItem(registry, character, item));
}

TEST(StorageSystemTest, DepositItemStorageFull) {
    entt::registry registry;
    auto character = CreateCharacter(registry);
    FillStorageSlots(registry, character, kMaxStorageSlots);
    auto item = CreateItemInInventory(registry, character, 1001, 0);

    EXPECT_FALSE(StorageSystem::DepositItem(registry, character, item));
    // Item must remain in inventory.
    EXPECT_TRUE(registry.all_of<InventoryOwnerComponent>(item));
}

TEST(StorageSystemTest, DepositItemRemovesOwnerComponent) {
    entt::registry registry;
    auto character = CreateCharacter(registry);
    auto item = CreateItemInInventory(registry, character, 1001, 3);

    ASSERT_TRUE(registry.all_of<InventoryOwnerComponent>(item));
    ASSERT_TRUE(StorageSystem::DepositItem(registry, character, item));

    EXPECT_FALSE(registry.all_of<InventoryOwnerComponent>(item));
}

TEST(StorageSystemTest, DepositItemMarksItemsDirty) {
    entt::registry registry;
    auto character = CreateCharacter(registry);
    auto item = CreateItemInInventory(registry, character, 1001, 0);

    StorageSystem::DepositItem(registry, character, item);

    auto* dirty = registry.try_get<DirtyComponent>(character);
    ASSERT_NE(dirty, nullptr);
    EXPECT_TRUE(dirty->items_dirty);
}

TEST(StorageSystemTest, DepositItemPublishesEvent) {
    entt::registry registry;
    EventBus event_bus(registry);

    bool event_received = false;
    mir2::ecs::events::ItemDepositedEvent captured{};
    event_bus.Subscribe<mir2::ecs::events::ItemDepositedEvent>(
        [&](mir2::ecs::events::ItemDepositedEvent& event) {
            event_received = true;
            captured = event;
        });

    auto character = CreateCharacter(registry);
    auto item = CreateItemInInventory(registry, character, 1001, 5);

    ASSERT_TRUE(StorageSystem::DepositItem(registry, character, item, &event_bus));

    EXPECT_TRUE(event_received);
    EXPECT_EQ(captured.character, character);
    EXPECT_EQ(captured.item, item);
    EXPECT_EQ(captured.item_id, 1001u);
    EXPECT_EQ(captured.count, 1);
    EXPECT_EQ(captured.inventory_slot_index, 5);
    EXPECT_GE(captured.storage_slot_index, 0);
    EXPECT_LT(captured.storage_slot_index, kMaxStorageSlots);
}

TEST(StorageSystemTest, DepositItemNoEventWithoutBus) {
    entt::registry registry;
    auto character = CreateCharacter(registry);
    auto item = CreateItemInInventory(registry, character, 1001, 0);

    // Passing nullptr should succeed without crash.
    EXPECT_TRUE(StorageSystem::DepositItem(registry, character, item, nullptr));
}

// ---------------------------------------------------------------------------
// WithdrawItem tests
// ---------------------------------------------------------------------------

TEST(StorageSystemTest, WithdrawItemSuccess) {
    entt::registry registry;
    auto character = CreateCharacter(registry);
    auto item = CreateItemInInventory(registry, character, 2001, 0);

    ASSERT_TRUE(StorageSystem::DepositItem(registry, character, item));

    // Find which slot the item ended up in.
    auto& storage = registry.get<StorageComponent>(character);
    int slot = -1;
    for (int i = 0; i < kMaxStorageSlots; ++i) {
        if (storage.slots[static_cast<std::size_t>(i)] == item) {
            slot = i;
            break;
        }
    }
    ASSERT_GE(slot, 0);

    EXPECT_TRUE(StorageSystem::WithdrawItem(registry, character, slot));
    EXPECT_TRUE(storage.slots[static_cast<std::size_t>(slot)] == entt::null);

    ASSERT_TRUE(registry.all_of<InventoryOwnerComponent>(item));
    const auto& oc = registry.get<InventoryOwnerComponent>(item);
    EXPECT_EQ(oc.owner, character);
    EXPECT_GE(oc.slot_index, 0);
}

TEST(StorageSystemTest, WithdrawItemInvalidCharacter) {
    entt::registry registry;
    auto character = CreateCharacter(registry);
    registry.destroy(character);

    EXPECT_FALSE(StorageSystem::WithdrawItem(registry, character, 0));
}

TEST(StorageSystemTest, WithdrawItemNegativeSlot) {
    entt::registry registry;
    auto character = CreateCharacter(registry);
    registry.get_or_emplace<StorageComponent>(character);

    EXPECT_FALSE(StorageSystem::WithdrawItem(registry, character, -1));
}

TEST(StorageSystemTest, WithdrawItemSlotOutOfRange) {
    entt::registry registry;
    auto character = CreateCharacter(registry);
    registry.get_or_emplace<StorageComponent>(character);

    EXPECT_FALSE(StorageSystem::WithdrawItem(registry, character, kMaxStorageSlots));
    EXPECT_FALSE(StorageSystem::WithdrawItem(registry, character, kMaxStorageSlots + 100));
}

TEST(StorageSystemTest, WithdrawItemEmptySlot) {
    entt::registry registry;
    auto character = CreateCharacter(registry);
    registry.emplace<StorageComponent>(character);

    EXPECT_FALSE(StorageSystem::WithdrawItem(registry, character, 0));
}

TEST(StorageSystemTest, WithdrawItemNoStorageComponent) {
    entt::registry registry;
    auto character = CreateCharacter(registry);

    EXPECT_FALSE(StorageSystem::WithdrawItem(registry, character, 0));
}

TEST(StorageSystemTest, WithdrawItemSetsOwnerComponent) {
    entt::registry registry;
    auto character = CreateCharacter(registry);
    auto item = CreateItemInInventory(registry, character, 2001, 0);

    ASSERT_TRUE(StorageSystem::DepositItem(registry, character, item));
    // After deposit the InventoryOwnerComponent is removed.
    ASSERT_FALSE(registry.all_of<InventoryOwnerComponent>(item));

    ASSERT_TRUE(StorageSystem::WithdrawItem(registry, character, 0));

    ASSERT_TRUE(registry.all_of<InventoryOwnerComponent>(item));
    const auto& oc = registry.get<InventoryOwnerComponent>(item);
    EXPECT_EQ(oc.owner, character);
    EXPECT_GE(oc.slot_index, 0);
    EXPECT_LT(oc.slot_index, mir2::common::constants::MAX_INVENTORY_SIZE);
}

TEST(StorageSystemTest, WithdrawItemInventoryFull) {
    entt::registry registry;
    auto character = CreateCharacter(registry);

    // Place one item in storage slot 0 directly.
    auto& storage = registry.emplace<StorageComponent>(character);
    auto stored_item = registry.create();
    registry.emplace<ItemComponent>(stored_item).item_id = 2001;
    storage.slots[0] = stored_item;

    // Fill all inventory slots.
    FillInventorySlots(registry, character);

    EXPECT_FALSE(StorageSystem::WithdrawItem(registry, character, 0));
    // Item must remain in storage.
    EXPECT_EQ(storage.slots[0], stored_item);
}

TEST(StorageSystemTest, WithdrawItemMarksItemsDirty) {
    entt::registry registry;
    auto character = CreateCharacter(registry);
    auto item = CreateItemInInventory(registry, character, 2001, 0);
    ASSERT_TRUE(StorageSystem::DepositItem(registry, character, item));

    // Clear dirty state from the deposit.
    mir2::ecs::dirty_tracker::clear_dirty(registry, character);

    ASSERT_TRUE(StorageSystem::WithdrawItem(registry, character, 0));

    auto* dirty = registry.try_get<DirtyComponent>(character);
    ASSERT_NE(dirty, nullptr);
    EXPECT_TRUE(dirty->items_dirty);
}

TEST(StorageSystemTest, WithdrawItemPublishesEvent) {
    entt::registry registry;
    EventBus event_bus(registry);

    bool event_received = false;
    mir2::ecs::events::ItemWithdrawnEvent captured{};
    event_bus.Subscribe<mir2::ecs::events::ItemWithdrawnEvent>(
        [&](mir2::ecs::events::ItemWithdrawnEvent& event) {
            event_received = true;
            captured = event;
        });

    auto character = CreateCharacter(registry);
    auto item = CreateItemInInventory(registry, character, 2001, 0);
    ASSERT_TRUE(StorageSystem::DepositItem(registry, character, item));

    ASSERT_TRUE(StorageSystem::WithdrawItem(registry, character, 0, &event_bus));

    EXPECT_TRUE(event_received);
    EXPECT_EQ(captured.character, character);
    EXPECT_EQ(captured.item, item);
    EXPECT_EQ(captured.item_id, 2001u);
    EXPECT_EQ(captured.count, 1);
    EXPECT_EQ(captured.storage_slot_index, 0);
    EXPECT_GE(captured.inventory_slot_index, 0);
}

TEST(StorageSystemTest, WithdrawItemDestroyedEntityInSlot) {
    entt::registry registry;
    auto character = CreateCharacter(registry);

    auto& storage = registry.emplace<StorageComponent>(character);
    auto item = registry.create();
    registry.emplace<ItemComponent>(item).item_id = 2001;
    storage.slots[0] = item;

    // Destroy the item before withdrawing.
    registry.destroy(item);

    EXPECT_FALSE(StorageSystem::WithdrawItem(registry, character, 0));
    // Slot should be cleaned up to null.
    EXPECT_TRUE(storage.slots[0] == entt::null);
}

TEST(StorageSystemTest, WithdrawItemOwnershipConflict) {
    entt::registry registry;
    auto character = CreateCharacter(registry);
    auto other = CreateCharacter(registry);

    // Manually place an item in storage that has an InventoryOwnerComponent
    // pointing to a different character.
    auto& storage = registry.emplace<StorageComponent>(character);
    auto item = registry.create();
    registry.emplace<ItemComponent>(item).item_id = 2001;
    auto& oc = registry.emplace<InventoryOwnerComponent>(item);
    oc.owner = other;
    oc.slot_index = 0;
    storage.slots[0] = item;

    EXPECT_FALSE(StorageSystem::WithdrawItem(registry, character, 0));
}

// ---------------------------------------------------------------------------
// GetStorageItems tests
// ---------------------------------------------------------------------------

TEST(StorageSystemTest, GetStorageItemsEmpty) {
    entt::registry registry;
    auto character = CreateCharacter(registry);
    // No StorageComponent at all.

    auto items = StorageSystem::GetStorageItems(registry, character);
    EXPECT_TRUE(items.empty());
}

TEST(StorageSystemTest, GetStorageItemsEmptyStorage) {
    entt::registry registry;
    auto character = CreateCharacter(registry);
    registry.emplace<StorageComponent>(character);

    auto items = StorageSystem::GetStorageItems(registry, character);
    EXPECT_TRUE(items.empty());
}

TEST(StorageSystemTest, GetStorageItemsReturnsValidItems) {
    entt::registry registry;
    auto character = CreateCharacter(registry);

    auto& storage = registry.emplace<StorageComponent>(character);
    auto item_a = registry.create();
    registry.emplace<ItemComponent>(item_a).item_id = 3001;
    auto item_b = registry.create();
    registry.emplace<ItemComponent>(item_b).item_id = 3002;
    storage.slots[0] = item_a;
    storage.slots[5] = item_b;

    auto items = StorageSystem::GetStorageItems(registry, character);
    ASSERT_EQ(items.size(), 2u);
    EXPECT_EQ(items[0], item_a);
    EXPECT_EQ(items[1], item_b);
}

TEST(StorageSystemTest, GetStorageItemsSkipsInvalidEntities) {
    entt::registry registry;
    auto character = CreateCharacter(registry);

    auto& storage = registry.emplace<StorageComponent>(character);
    auto valid_item = registry.create();
    registry.emplace<ItemComponent>(valid_item).item_id = 3001;
    auto doomed_item = registry.create();
    registry.emplace<ItemComponent>(doomed_item).item_id = 3002;

    storage.slots[0] = valid_item;
    storage.slots[1] = doomed_item;
    registry.destroy(doomed_item);

    auto items = StorageSystem::GetStorageItems(registry, character);
    ASSERT_EQ(items.size(), 1u);
    EXPECT_EQ(items[0], valid_item);
    // Destroyed entity's slot should be cleaned up.
    EXPECT_TRUE(storage.slots[1] == entt::null);
}

TEST(StorageSystemTest, GetStorageItemsInvalidCharacter) {
    entt::registry registry;
    auto character = CreateCharacter(registry);
    registry.destroy(character);

    auto items = StorageSystem::GetStorageItems(registry, character);
    EXPECT_TRUE(items.empty());
}

// ---------------------------------------------------------------------------
// Integration / roundtrip test
// ---------------------------------------------------------------------------

TEST(StorageSystemTest, DepositThenWithdraw) {
    entt::registry registry;
    EventBus event_bus(registry);

    auto character = CreateCharacter(registry);
    auto item = CreateItemInInventory(registry, character, 5001, 2);

    // Deposit into storage.
    ASSERT_TRUE(StorageSystem::DepositItem(registry, character, item, &event_bus));
    EXPECT_FALSE(registry.all_of<InventoryOwnerComponent>(item));

    auto stored = StorageSystem::GetStorageItems(registry, character);
    ASSERT_EQ(stored.size(), 1u);
    EXPECT_EQ(stored[0], item);

    // Find the slot.
    auto& storage = registry.get<StorageComponent>(character);
    int slot = -1;
    for (int i = 0; i < kMaxStorageSlots; ++i) {
        if (storage.slots[static_cast<std::size_t>(i)] == item) {
            slot = i;
            break;
        }
    }
    ASSERT_GE(slot, 0);

    // Withdraw back to inventory.
    ASSERT_TRUE(StorageSystem::WithdrawItem(registry, character, slot, &event_bus));
    EXPECT_TRUE(storage.slots[static_cast<std::size_t>(slot)] == entt::null);

    ASSERT_TRUE(registry.all_of<InventoryOwnerComponent>(item));
    const auto& oc = registry.get<InventoryOwnerComponent>(item);
    EXPECT_EQ(oc.owner, character);
    EXPECT_GE(oc.slot_index, 0);
    EXPECT_LT(oc.slot_index, mir2::common::constants::MAX_INVENTORY_SIZE);

    // ItemComponent must be unchanged.
    const auto& ic = registry.get<ItemComponent>(item);
    EXPECT_EQ(ic.item_id, 5001u);
    EXPECT_EQ(ic.count, 1);

    // Storage should now be empty.
    auto remaining = StorageSystem::GetStorageItems(registry, character);
    EXPECT_TRUE(remaining.empty());
}

}  // namespace
