#include <gtest/gtest.h>

#include <cstdint>

#include <entt/entt.hpp>

#include "common/types.h"
#include "ecs/components/character_components.h"
#include "ecs/components/equipment_component.h"
#include "ecs/components/pk_component.h"
#include "ecs/dirty_tracker.h"
#include "ecs/systems/pk_system.h"

namespace {

using mir2::ecs::CharacterIdentityComponent;
using mir2::ecs::DirtyComponent;
using mir2::ecs::EquipmentSlotComponent;
using mir2::ecs::PKComponent;
using mir2::ecs::PKSystem;

entt::entity CreatePlayer(entt::registry& registry) {
    auto entity = registry.create();
    registry.emplace<CharacterIdentityComponent>(entity);
    return entity;
}

entt::entity CreateItem(entt::registry& registry) {
    return registry.create();
}

int CountEquippedItems(const entt::registry& registry, const EquipmentSlotComponent& equipment) {
    int count = 0;
    for (const auto item : equipment.slots) {
        if (item != entt::null && registry.valid(item)) {
            ++count;
        }
    }
    return count;
}

}  // namespace

TEST(PKSystemTest, RecordPkAttackStoresAttacker) {
    entt::registry registry;
    PKSystem system(registry);

    const auto attacker = CreatePlayer(registry);
    const auto victim = CreatePlayer(registry);

    system.update(12345);
    system.record_pk_attack(attacker, victim);

    const auto* pk = registry.try_get<PKComponent>(victim);
    ASSERT_NE(pk, nullptr);
    ASSERT_EQ(pk->pk_hiter_list.size(), 1u);
    EXPECT_EQ(pk->pk_hiter_list[0].attacker, attacker);
    EXPECT_EQ(pk->pk_hiter_list[0].hit_time_ms, 12345);
}

TEST(PKSystemTest, HandlePkKillAddsPkPointsAndMarksDirty) {
    entt::registry registry;
    PKSystem system(registry);

    const auto killer = CreatePlayer(registry);
    const auto victim = CreatePlayer(registry);

    system.update(2000);
    system.handle_pk_kill(killer, victim);

    const auto& pk = registry.get<PKComponent>(killer);
    EXPECT_EQ(pk.pk_points, 10);
    EXPECT_EQ(pk.last_pk_time_ms, 2000);
    EXPECT_FALSE(pk.is_red_name);

    const auto* dirty = registry.try_get<DirtyComponent>(killer);
    ASSERT_NE(dirty, nullptr);
    EXPECT_TRUE(dirty->attributes_dirty);
}

TEST(PKSystemTest, IsRedNameReflectsPkPoints) {
    entt::registry registry;
    PKSystem system(registry);

    const auto entity = CreatePlayer(registry);
    auto& pk = registry.emplace<PKComponent>(entity);

    pk.pk_points = 101;
    pk.update_red_name_status();
    EXPECT_TRUE(system.is_red_name(entity));

    pk.pk_points = 100;
    pk.update_red_name_status();
    EXPECT_FALSE(system.is_red_name(entity));
}

TEST(PKSystemTest, HandleRedNameDeathDropsEquippedItems) {
    entt::registry registry;
    PKSystem system(registry);

    const auto victim = CreatePlayer(registry);
    auto& pk = registry.emplace<PKComponent>(victim);
    pk.pk_points = 120;
    pk.update_red_name_status();

    auto& equipment = registry.emplace<EquipmentSlotComponent>(victim);
    equipment.slots[static_cast<std::size_t>(mir2::common::EquipSlot::WEAPON)] = CreateItem(registry);
    equipment.slots[static_cast<std::size_t>(mir2::common::EquipSlot::ARMOR)] = CreateItem(registry);
    equipment.slots[static_cast<std::size_t>(mir2::common::EquipSlot::HELMET)] = CreateItem(registry);

    const int before_count = CountEquippedItems(registry, equipment);

    const mir2::common::Position death_pos{10, 10};
    system.handle_red_name_death(victim, death_pos);

    const int after_count = CountEquippedItems(registry, equipment);
    const int dropped = before_count - after_count;
    EXPECT_EQ(dropped, 2);
    EXPECT_EQ(after_count, before_count - dropped);

    const auto* dirty = registry.try_get<DirtyComponent>(victim);
    ASSERT_NE(dirty, nullptr);
    EXPECT_TRUE(dirty->items_dirty);
    EXPECT_TRUE(dirty->equipment_dirty);
}

TEST(PKSystemTest, UpdateDecaysPkPointsOverTime) {
    entt::registry registry;
    PKSystem system(registry);

    const auto entity = CreatePlayer(registry);
    auto& pk = registry.emplace<PKComponent>(entity);
    pk.pk_points = 5;
    pk.last_pk_time_ms = 1000;

    const int64_t now = 1000 + 2 * 3600000;
    system.update(now);

    const auto& updated_pk = registry.get<PKComponent>(entity);
    EXPECT_EQ(updated_pk.pk_points, 3);
    EXPECT_EQ(updated_pk.last_pk_time_ms, now);

    const auto* dirty = registry.try_get<DirtyComponent>(entity);
    ASSERT_NE(dirty, nullptr);
    EXPECT_TRUE(dirty->attributes_dirty);
}
