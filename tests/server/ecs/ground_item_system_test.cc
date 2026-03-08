#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <entt/entt.hpp>

#include "common/item_constants.h"
#include "core/utils.h"
#include "ecs/components/character_components.h"
#include "ecs/components/ground_item_component.h"
#include "ecs/components/item_component.h"
#include "ecs/systems/ground_item_system.h"
#include "ecs/systems/inventory_system.h"

namespace {

using mir2::ecs::CharacterStateComponent;
using mir2::ecs::GroundItemComponent;
using mir2::ecs::InventoryOwnerComponent;
using mir2::ecs::ItemComponent;

entt::entity CreateCharacter(entt::registry& registry) {
    auto entity = registry.create();
    registry.emplace<CharacterStateComponent>(entity);
    return entity;
}

entt::entity CreateGroundItem(entt::registry& registry, uint32_t item_id) {
    auto entity = registry.create();
    auto& item = registry.emplace<ItemComponent>(entity);
    item.item_id = item_id;
    item.count = 1;

    auto& owner = registry.emplace<InventoryOwnerComponent>(entity);
    owner.owner = entt::null;
    owner.slot_index = -1;

    return entity;
}

}  // namespace

TEST(GroundItemSystemTest, UpdateRemovesExpiredItems) {
    entt::registry registry;
    mir2::ecs::GroundItemSystem system;

    auto item = CreateGroundItem(registry, 3001u);
    auto& ground = registry.emplace<GroundItemComponent>(item);
    const int64_t now = mir2::core::GetCurrentTimestampMs();
    ground.drop_time_ms = now - mir2::common::pickup::kItemExpireDurationMs - 1;

    system.Update(registry, 1.0f);

    EXPECT_FALSE(registry.valid(item));
}

TEST(GroundItemSystemTest, OwnershipExpiredAllowsPickup) {
    entt::registry registry;
    auto owner = CreateCharacter(registry);
    auto picker = CreateCharacter(registry);

    auto item = CreateGroundItem(registry, 3002u);
    auto& ground = registry.emplace<GroundItemComponent>(item);
    ground.owner = owner;
    ground.ownership_expire_ms = mir2::core::GetCurrentTimestampMs() - 1;

    EXPECT_TRUE(mir2::ecs::InventorySystem::PickupItem(registry, picker, item, nullptr));
    const auto& owner_component = registry.get<InventoryOwnerComponent>(item);
    EXPECT_EQ(owner_component.owner, picker);
}
