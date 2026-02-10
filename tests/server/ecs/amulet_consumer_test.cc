#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <entt/entt.hpp>

#include "common/item_constants.h"
#include "ecs/components/equipment_component.h"
#include "ecs/components/item_component.h"
#include "ecs/systems/amulet_consumer.h"

namespace {

using mir2::ecs::EquipmentSlotComponent;
using mir2::ecs::ItemComponent;

entt::entity CreateCharacter(entt::registry& registry) {
    auto entity = registry.create();
    auto& equipment = registry.emplace<EquipmentSlotComponent>(entity);
    equipment.slots.fill(entt::null);
    return entity;
}

entt::entity CreateTalisman(entt::registry& registry, int durability) {
    auto entity = registry.create();
    auto& item = registry.emplace<ItemComponent>(entity);
    item.std_mode = mir2::common::item_std_mode::kAmulet;
    item.shape = mir2::common::amulet_shape::kTalisman;
    item.durability = durability;
    item.max_durability = std::max(durability, 0);
    return entity;
}

}  // namespace

TEST(AmuletConsumerTest, CheckAmuletFindsAmuletSlot) {
    entt::registry registry;
    const auto character = CreateCharacter(registry);
    auto& equipment = registry.get<EquipmentSlotComponent>(character);

    auto talisman = CreateTalisman(registry, 150);
    equipment.slots[static_cast<std::size_t>(mir2::common::EquipSlot::AMULET)] = talisman;

    EXPECT_TRUE(mir2::ecs::CheckAmulet(registry, character));
}

TEST(AmuletConsumerTest, CheckAmuletFindsBraceletLeftSlot) {
    entt::registry registry;
    const auto character = CreateCharacter(registry);
    auto& equipment = registry.get<EquipmentSlotComponent>(character);

    auto talisman = CreateTalisman(registry, 150);
    equipment.slots[static_cast<std::size_t>(mir2::common::EquipSlot::BRACELET_LEFT)] = talisman;

    EXPECT_TRUE(mir2::ecs::CheckAmulet(registry, character));
}

TEST(AmuletConsumerTest, ConsumeAmuletReducesDurabilityWhenEnough) {
    entt::registry registry;
    const auto character = CreateCharacter(registry);
    auto& equipment = registry.get<EquipmentSlotComponent>(character);

    auto talisman = CreateTalisman(registry, 250);
    equipment.slots[static_cast<std::size_t>(mir2::common::EquipSlot::AMULET)] = talisman;

    EXPECT_TRUE(mir2::ecs::ConsumeAmulet(registry, character));
    const auto& item = registry.get<ItemComponent>(talisman);
    EXPECT_EQ(item.durability, 150);
    EXPECT_TRUE(registry.valid(talisman));
}

TEST(AmuletConsumerTest, ConsumeAmuletRemovesWhenBelowConsume) {
    entt::registry registry;
    const auto character = CreateCharacter(registry);
    auto& equipment = registry.get<EquipmentSlotComponent>(character);

    auto talisman = CreateTalisman(registry, 50);
    equipment.slots[static_cast<std::size_t>(mir2::common::EquipSlot::AMULET)] = talisman;

    EXPECT_FALSE(mir2::ecs::ConsumeAmulet(registry, character));
    EXPECT_FALSE(registry.valid(talisman));
    EXPECT_EQ(equipment.slots[static_cast<std::size_t>(mir2::common::EquipSlot::AMULET)],
              static_cast<entt::entity>(entt::null));
}

TEST(AmuletConsumerTest, ConsumeAmuletRemovesWhenDropsBelowThreshold) {
    entt::registry registry;
    const auto character = CreateCharacter(registry);
    auto& equipment = registry.get<EquipmentSlotComponent>(character);

    auto talisman = CreateTalisman(registry, 100);
    equipment.slots[static_cast<std::size_t>(mir2::common::EquipSlot::AMULET)] = talisman;

    EXPECT_TRUE(mir2::ecs::ConsumeAmulet(registry, character));
    EXPECT_FALSE(registry.valid(talisman));
    EXPECT_EQ(equipment.slots[static_cast<std::size_t>(mir2::common::EquipSlot::AMULET)],
              static_cast<entt::entity>(entt::null));
}
