#include <gtest/gtest.h>

#include <entt/entt.hpp>

#include "common/item_constants.h"
#include "common/types/constants.h"
#include "core/utils.h"
#include "ecs/components/character_components.h"
#include "ecs/components/equipment_component.h"
#include "ecs/components/ground_item_component.h"
#include "ecs/components/item_component.h"
#include "ecs/components/party_component.h"
#include "ecs/components/skill_component.h"
#include "ecs/dirty_tracker.h"
#include "ecs/event_bus.h"
#include "ecs/events/inventory_events.h"
#include "ecs/systems/inventory_system.h"

namespace {

using mir2::ecs::DirtyComponent;
using mir2::ecs::EventBus;
using mir2::ecs::CharacterStateComponent;
using mir2::ecs::GroundItemComponent;
using mir2::ecs::InventoryOwnerComponent;
using mir2::ecs::ItemComponent;
using mir2::ecs::PartyMemberComponent;
using mir2::ecs::SkillComponent;
using mir2::ecs::dirty_tracker::is_dirty;
using mir2::ecs::events::ItemAddedEvent;
using mir2::ecs::events::ItemDroppedEvent;
using mir2::ecs::events::ItemEquippedEvent;
using mir2::ecs::events::ItemUnequippedEvent;
using mir2::ecs::events::ItemUsedEvent;
using mir2::ecs::events::SkillLearnedEvent;
using mir2::ecs::events::SkillUpgradedEvent;

entt::entity CreateCharacter(entt::registry& registry) {
    return registry.create();
}

entt::entity CreateItem(entt::registry& registry,
                        entt::entity owner,
                        int slot_index,
                        uint32_t item_id,
                        int count,
                        int equip_slot) {
    auto item = registry.create();
    auto& item_component = registry.emplace<ItemComponent>(item);
    item_component.item_id = item_id;
    item_component.count = count;
    item_component.equip_slot = equip_slot;

    auto& owner_component = registry.emplace<InventoryOwnerComponent>(item);
    owner_component.owner = owner;
    owner_component.slot_index = slot_index;
    return item;
}

void FillInventory(entt::registry& registry, entt::entity owner) {
    for (int slot = 0; slot < mir2::common::constants::MAX_INVENTORY_SIZE; ++slot) {
        CreateItem(registry, owner, slot, 1000u + static_cast<uint32_t>(slot), 1, -1);
    }
}

int CountItems(entt::registry& registry, entt::entity owner) {
    int count = 0;
    auto view = registry.view<InventoryOwnerComponent, ItemComponent>();
    for (auto entity : view) {
        const auto& item_owner = view.get<InventoryOwnerComponent>(entity);
        if (item_owner.owner == owner) {
            ++count;
        }
    }
    return count;
}

}  // namespace

TEST(InventorySystemTest, AddItemAddsToBagAndPublishesEvent) {
    entt::registry registry;
    const auto character = CreateCharacter(registry);
    EventBus event_bus(registry);

    ItemAddedEvent captured{};
    int added_count = 0;
    event_bus.Subscribe<ItemAddedEvent>([&](const ItemAddedEvent& event) {
        captured = event;
        ++added_count;
    });

    auto result = mir2::ecs::InventorySystem::AddItem(
        registry, character, 200u, 3, &event_bus);

    ASSERT_TRUE(result.has_value());
    auto item = *result;
    const auto* item_component = registry.try_get<ItemComponent>(item);
    const auto* owner = registry.try_get<InventoryOwnerComponent>(item);
    ASSERT_NE(item_component, nullptr);
    ASSERT_NE(owner, nullptr);

    EXPECT_EQ(item_component->item_id, 200u);
    EXPECT_EQ(item_component->count, 3);
    EXPECT_EQ(owner->owner, character);
    EXPECT_EQ(owner->slot_index, 0);

    auto* dirty = registry.try_get<DirtyComponent>(character);
    ASSERT_NE(dirty, nullptr);
    EXPECT_TRUE(dirty->items_dirty);
    EXPECT_TRUE(is_dirty(registry, character));

    EXPECT_EQ(added_count, 1);
    EXPECT_EQ(captured.item_id, 200u);
    EXPECT_EQ(captured.slot_index, 0);
}

TEST(InventorySystemTest, AddItemFailsWhenInventoryFull) {
    entt::registry registry;
    const auto character = CreateCharacter(registry);

    FillInventory(registry, character);
    const int before_count = CountItems(registry, character);

    auto result = mir2::ecs::InventorySystem::AddItem(
        registry, character, 201u, 1, nullptr);

    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(CountItems(registry, character), before_count);
    EXPECT_FALSE(is_dirty(registry, character));
}

TEST(InventorySystemTest, AddItemIgnoresCompatibilitySnapshotOccupancy) {
    entt::registry registry;
    const auto character = CreateCharacter(registry);

    auto& snapshot = registry.emplace<mir2::ecs::InventorySnapshotComponent>(character);
    for (std::size_t i = 0; i < snapshot.slots.size(); ++i) {
        mir2::ecs::InventorySnapshotItemData item;
        item.instance_id = static_cast<uint64_t>(i + 1);
        item.item_id = 9000u + static_cast<uint32_t>(i);
        item.count = 1;
        snapshot.slots[i] = item;
    }

    auto result = mir2::ecs::InventorySystem::AddItem(
        registry, character, 202u, 1, nullptr);

    ASSERT_TRUE(result.has_value());
    const auto item_entity = *result;
    const auto* owner = registry.try_get<InventoryOwnerComponent>(item_entity);
    ASSERT_NE(owner, nullptr);
    EXPECT_EQ(owner->slot_index, 0);
}

TEST(InventorySystemTest, EquipItemIgnoresCompatibilitySnapshotEquipmentOccupancy) {
    entt::registry registry;
    const auto character = CreateCharacter(registry);

    auto& snapshot = registry.emplace<mir2::ecs::InventorySnapshotComponent>(character);
    mir2::ecs::InventorySnapshotItemData equipped_snapshot_item;
    equipped_snapshot_item.instance_id = 9999;
    equipped_snapshot_item.item_id = 5001;
    equipped_snapshot_item.count = 1;
    snapshot.equipment[0] = equipped_snapshot_item;

    const auto item = CreateItem(registry, character, 0, 300u, 1, 0);
    bool equipped = mir2::ecs::InventorySystem::EquipItem(registry, character, item, nullptr);

    ASSERT_TRUE(equipped);
    const auto* equipment = registry.try_get<mir2::ecs::EquipmentSlotComponent>(character);
    ASSERT_NE(equipment, nullptr);
    EXPECT_EQ(equipment->slots[0], item);

    const auto* owner = registry.try_get<InventoryOwnerComponent>(item);
    ASSERT_NE(owner, nullptr);
    EXPECT_EQ(owner->slot_index, -1);
}

TEST(InventorySystemTest, LearnSkillIgnoresCompatibilitySnapshotSkills) {
    entt::registry registry;
    const auto character = CreateCharacter(registry);

    auto& snapshot = registry.emplace<mir2::ecs::InventorySnapshotComponent>(character);
    mir2::ecs::InventorySnapshotSkillData snapshot_skill;
    snapshot_skill.skill_id = 7001u;
    snapshot_skill.level = 9;
    snapshot.skills.push_back(snapshot_skill);

    auto learned = mir2::ecs::InventorySystem::LearnSkill(registry, character, 7001u, 1, nullptr);
    ASSERT_TRUE(learned.has_value());

    const auto skill_entity = *learned;
    const auto* skill = registry.try_get<SkillComponent>(skill_entity);
    ASSERT_NE(skill, nullptr);
    EXPECT_EQ(skill->skill_id, 7001u);
    EXPECT_EQ(skill->level, 1);
}

TEST(InventorySystemTest, EquipItemMovesToEquipment) {
    entt::registry registry;
    const auto character = CreateCharacter(registry);
    EventBus event_bus(registry);

    int equipped_count = 0;
    event_bus.Subscribe<ItemEquippedEvent>([&](const ItemEquippedEvent&) {
        ++equipped_count;
    });

    auto item = CreateItem(
        registry, character, 0, 300u, 1,
        static_cast<int>(mir2::common::EquipSlot::WEAPON));

    bool ok = mir2::ecs::InventorySystem::EquipItem(
        registry, character, item, &event_bus);

    EXPECT_TRUE(ok);
    const auto& equipment = registry.get<mir2::ecs::EquipmentSlotComponent>(character);
    EXPECT_EQ(equipment.slots[static_cast<std::size_t>(mir2::common::EquipSlot::WEAPON)], item);

    const auto& owner = registry.get<InventoryOwnerComponent>(item);
    EXPECT_EQ(owner.slot_index, -1);

    auto* dirty = registry.try_get<DirtyComponent>(character);
    ASSERT_NE(dirty, nullptr);
    EXPECT_TRUE(dirty->items_dirty);
    EXPECT_TRUE(dirty->equipment_dirty);
    EXPECT_EQ(equipped_count, 1);
}

TEST(InventorySystemTest, EquipItemSwapsWithEquippedItemEvenWhenBagFull) {
    entt::registry registry;
    const auto character = CreateCharacter(registry);
    EventBus event_bus(registry);

    int unequip_count = 0;
    event_bus.Subscribe<ItemUnequippedEvent>([&](const ItemUnequippedEvent&) {
        ++unequip_count;
    });

    auto& equipment = registry.emplace<mir2::ecs::EquipmentSlotComponent>(character);
    auto equipped_item = CreateItem(
        registry, character, -1, 400u, 1,
        static_cast<int>(mir2::common::EquipSlot::WEAPON));
    equipment.slots[static_cast<std::size_t>(mir2::common::EquipSlot::WEAPON)] = equipped_item;

    for (int slot = 1; slot < mir2::common::constants::MAX_INVENTORY_SIZE; ++slot) {
        CreateItem(registry, character, slot, 500u + static_cast<uint32_t>(slot), 1, -1);
    }

    auto new_item = CreateItem(
        registry, character, 0, 401u, 1,
        static_cast<int>(mir2::common::EquipSlot::WEAPON));

    bool ok = mir2::ecs::InventorySystem::EquipItem(
        registry, character, new_item, &event_bus);

    EXPECT_TRUE(ok);
    EXPECT_EQ(equipment.slots[static_cast<std::size_t>(mir2::common::EquipSlot::WEAPON)], new_item);
    const auto& new_owner = registry.get<InventoryOwnerComponent>(new_item);
    EXPECT_EQ(new_owner.slot_index, -1);

    const auto& old_owner = registry.get<InventoryOwnerComponent>(equipped_item);
    EXPECT_EQ(old_owner.slot_index, 0);
    EXPECT_EQ(unequip_count, 1);
}

TEST(InventorySystemTest, UnequipFailsWhenBagFull) {
    entt::registry registry;
    const auto character = CreateCharacter(registry);

    auto& equipment = registry.emplace<mir2::ecs::EquipmentSlotComponent>(character);
    auto equipped_item = CreateItem(
        registry, character, -1, 600u, 1,
        static_cast<int>(mir2::common::EquipSlot::WEAPON));
    equipment.slots[static_cast<std::size_t>(mir2::common::EquipSlot::WEAPON)] = equipped_item;

    FillInventory(registry, character);

    bool ok = mir2::ecs::InventorySystem::UnequipItem(
        registry, character, static_cast<int>(mir2::common::EquipSlot::WEAPON), nullptr);

    EXPECT_FALSE(ok);
    EXPECT_EQ(equipment.slots[static_cast<std::size_t>(mir2::common::EquipSlot::WEAPON)], equipped_item);
    const auto& owner = registry.get<InventoryOwnerComponent>(equipped_item);
    EXPECT_EQ(owner.slot_index, -1);
    EXPECT_FALSE(is_dirty(registry, character));
}

TEST(InventorySystemTest, UseItemConsumesStackAndDestroysWhenEmpty) {
    entt::registry registry;
    const auto character = CreateCharacter(registry);
    EventBus event_bus(registry);

    ItemUsedEvent captured{};
    int used_count = 0;
    event_bus.Subscribe<ItemUsedEvent>([&](const ItemUsedEvent& event) {
        captured = event;
        ++used_count;
    });

    auto item = CreateItem(registry, character, 0, 700u, 2, -1);

    EXPECT_TRUE(mir2::ecs::InventorySystem::UseItem(
        registry, character, item, 1, &event_bus));
    EXPECT_TRUE(registry.valid(item));
    EXPECT_EQ(registry.get<ItemComponent>(item).count, 1);
    EXPECT_EQ(captured.remaining_count, 1);

    EXPECT_TRUE(mir2::ecs::InventorySystem::UseItem(
        registry, character, item, 1, &event_bus));
    EXPECT_FALSE(registry.valid(item));
    EXPECT_EQ(used_count, 2);
}

TEST(InventorySystemTest, DropItemClearsOwnership) {
    entt::registry registry;
    const auto character = CreateCharacter(registry);
    registry.emplace<CharacterStateComponent>(character);
    EventBus event_bus(registry);

    int drop_count = 0;
    event_bus.Subscribe<ItemDroppedEvent>([&](const ItemDroppedEvent&) {
        ++drop_count;
    });

    auto item = CreateItem(registry, character, 0, 800u, 1, -1);

    EXPECT_TRUE(mir2::ecs::InventorySystem::DropItem(
        registry, character, item, &event_bus));
    EXPECT_TRUE(registry.valid(item));

    const auto& owner = registry.get<InventoryOwnerComponent>(item);
    EXPECT_EQ(owner.owner, static_cast<entt::entity>(entt::null));
    EXPECT_EQ(owner.slot_index, -1);
    EXPECT_EQ(drop_count, 1);
}

TEST(InventorySystemTest, LearnAndUpgradeSkill) {
    entt::registry registry;
    const auto character = CreateCharacter(registry);
    EventBus event_bus(registry);

    int learned_count = 0;
    int upgraded_count = 0;
    event_bus.Subscribe<SkillLearnedEvent>([&](const SkillLearnedEvent&) {
        ++learned_count;
    });
    event_bus.Subscribe<SkillUpgradedEvent>([&](const SkillUpgradedEvent&) {
        ++upgraded_count;
    });

    auto skill = mir2::ecs::InventorySystem::LearnSkill(
        registry, character, 900u, 1, &event_bus);

    ASSERT_TRUE(skill.has_value());
    const auto& skill_component = registry.get<SkillComponent>(*skill);
    EXPECT_EQ(skill_component.skill_id, 900u);
    EXPECT_EQ(skill_component.level, 1);

    EXPECT_TRUE(mir2::ecs::InventorySystem::UpgradeSkill(
        registry, character, 900u, 2, &event_bus));
    EXPECT_EQ(registry.get<SkillComponent>(*skill).level, 3);

    auto* dirty = registry.try_get<DirtyComponent>(character);
    ASSERT_NE(dirty, nullptr);
    EXPECT_TRUE(dirty->skills_dirty);
    EXPECT_EQ(learned_count, 1);
    EXPECT_EQ(upgraded_count, 1);
}

TEST(InventorySystemTest, DropItemSpecialMeatDecaysDurability) {
    entt::registry registry;
    const auto character = CreateCharacter(registry);
    registry.emplace<CharacterStateComponent>(character);

    auto item = CreateItem(registry, character, 0, 1001u, 1, -1);
    auto& item_component = registry.get<ItemComponent>(item);
    item_component.std_mode = mir2::common::item_std_mode::kSpecialMeat;
    item_component.durability = 5000;
    item_component.max_durability = 5000;

    EXPECT_TRUE(mir2::ecs::InventorySystem::DropItem(
        registry, character, item, nullptr));
    EXPECT_EQ(item_component.durability, 3000);
}

TEST(InventorySystemTest, DropItemDiceRollsLooksInRange) {
    entt::registry registry;
    const auto character = CreateCharacter(registry);
    registry.emplace<CharacterStateComponent>(character);

    auto item = CreateItem(registry, character, 0, 1002u, 1, -1);
    auto& item_component = registry.get<ItemComponent>(item);
    item_component.std_mode = mir2::common::item_std_mode::kDice;
    item_component.looks = 0;

    EXPECT_TRUE(mir2::ecs::InventorySystem::DropItem(
        registry, character, item, nullptr));
    EXPECT_GE(item_component.looks, 1);
    EXPECT_LE(item_component.looks, 6);
}

TEST(InventorySystemTest, DropItemFailsDuringActivity) {
    entt::registry registry;
    const auto character = CreateCharacter(registry);
    auto& state = registry.emplace<CharacterStateComponent>(character);
    state.is_in_activity = true;

    auto item = CreateItem(registry, character, 0, 1003u, 1, -1);

    EXPECT_FALSE(mir2::ecs::InventorySystem::DropItem(
        registry, character, item, nullptr));
    const auto& owner = registry.get<InventoryOwnerComponent>(item);
    EXPECT_EQ(owner.owner, character);
    EXPECT_EQ(owner.slot_index, 0);
}

TEST(InventorySystemTest, DropItemFailsDuringTradeCooldown) {
    entt::registry registry;
    const auto character = CreateCharacter(registry);
    auto& state = registry.emplace<CharacterStateComponent>(character);
    state.last_trade_close_time_ms = mir2::core::GetCurrentTimestampMs();

    auto item = CreateItem(registry, character, 0, 1004u, 1, -1);

    EXPECT_FALSE(mir2::ecs::InventorySystem::DropItem(
        registry, character, item, nullptr));
    const auto& owner = registry.get<InventoryOwnerComponent>(item);
    EXPECT_EQ(owner.owner, character);
    EXPECT_EQ(owner.slot_index, 0);
}

TEST(InventorySystemTest, PickupItemHonorsOwner) {
    entt::registry registry;
    const auto character = CreateCharacter(registry);
    registry.emplace<CharacterStateComponent>(character);

    auto ground_item = registry.create();
    auto& item_component = registry.emplace<ItemComponent>(ground_item);
    item_component.item_id = 2000u;
    item_component.count = 1;
    auto& owner = registry.emplace<InventoryOwnerComponent>(ground_item);
    owner.owner = entt::null;
    owner.slot_index = -1;

    auto& ground_state = registry.emplace<GroundItemComponent>(ground_item);
    ground_state.owner = character;
    ground_state.drop_time_ms = mir2::core::GetCurrentTimestampMs();

    EXPECT_TRUE(mir2::ecs::InventorySystem::PickupItem(
        registry, character, ground_item, nullptr));
    EXPECT_EQ(owner.owner, character);
    EXPECT_EQ(owner.slot_index, 0);
    EXPECT_FALSE(registry.all_of<GroundItemComponent>(ground_item));
}

TEST(InventorySystemTest, PickupItemAllowsPartySharedOwnership) {
    entt::registry registry;
    const auto owner_character = CreateCharacter(registry);
    const auto picker = CreateCharacter(registry);
    registry.emplace<PartyMemberComponent>(owner_character, 10u);
    registry.emplace<PartyMemberComponent>(picker, 10u);

    auto ground_item = registry.create();
    auto& item_component = registry.emplace<ItemComponent>(ground_item);
    item_component.item_id = 2001u;
    item_component.count = 1;
    auto& owner = registry.emplace<InventoryOwnerComponent>(ground_item);
    owner.owner = entt::null;
    owner.slot_index = -1;

    auto& ground_state = registry.emplace<GroundItemComponent>(ground_item);
    ground_state.owner = owner_character;
    ground_state.drop_time_ms = mir2::core::GetCurrentTimestampMs();

    EXPECT_TRUE(mir2::ecs::InventorySystem::PickupItem(
        registry, picker, ground_item, nullptr));
    EXPECT_EQ(owner.owner, picker);
}

TEST(InventorySystemTest, PickupItemRejectsNonOwnerWithoutParty) {
    entt::registry registry;
    const auto owner_character = CreateCharacter(registry);
    const auto picker = CreateCharacter(registry);

    auto ground_item = registry.create();
    auto& item_component = registry.emplace<ItemComponent>(ground_item);
    item_component.item_id = 2003u;
    item_component.count = 1;
    auto& owner = registry.emplace<InventoryOwnerComponent>(ground_item);
    owner.owner = entt::null;
    owner.slot_index = -1;

    auto& ground_state = registry.emplace<GroundItemComponent>(ground_item);
    ground_state.owner = owner_character;
    ground_state.drop_time_ms = mir2::core::GetCurrentTimestampMs();

    EXPECT_FALSE(mir2::ecs::InventorySystem::PickupItem(
        registry, picker, ground_item, nullptr));
    EXPECT_EQ(owner.owner, static_cast<entt::entity>(entt::null));
    EXPECT_EQ(owner.slot_index, -1);
}

TEST(InventorySystemTest, PickupItemAllowsAfterOwnershipExpiry) {
    entt::registry registry;
    const auto owner_character = CreateCharacter(registry);
    const auto picker = CreateCharacter(registry);

    auto ground_item = registry.create();
    auto& item_component = registry.emplace<ItemComponent>(ground_item);
    item_component.item_id = 2002u;
    item_component.count = 1;
    auto& owner = registry.emplace<InventoryOwnerComponent>(ground_item);
    owner.owner = entt::null;
    owner.slot_index = -1;

    auto& ground_state = registry.emplace<GroundItemComponent>(ground_item);
    ground_state.owner = owner_character;
    ground_state.ownership_expire_ms = mir2::core::GetCurrentTimestampMs() - 1;

    EXPECT_TRUE(mir2::ecs::InventorySystem::PickupItem(
        registry, picker, ground_item, nullptr));
    EXPECT_EQ(owner.owner, picker);
}
