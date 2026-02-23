#include "ecs/systems/amulet_consumer.h"

#include "common/item_constants.h"
#include "ecs/components/equipment_component.h"
#include "ecs/components/item_component.h"
#include "ecs/dirty_tracker.h"

#include <algorithm>
#include <array>

namespace mir2::ecs {

namespace {

constexpr std::array<mir2::common::EquipSlot, 2> kTalismanSlots = {
    mir2::common::EquipSlot::AMULET,
    mir2::common::EquipSlot::BRACELET_LEFT
};

template <typename Registry, typename ItemPtr>
entt::entity FindTalismanEntity(Registry& registry,
                                entt::entity character,
                                mir2::common::EquipSlot* out_slot,
                                ItemPtr* out_item) {
    if (!registry.valid(character)) {
        return entt::null;
    }

    const auto* equipment = registry.template try_get<EquipmentSlotComponent>(character);
    if (!equipment) {
        return entt::null;
    }

    for (auto slot : kTalismanSlots) {
        const std::size_t index = static_cast<std::size_t>(slot);
        if (index >= equipment->slots.size()) {
            continue;
        }

        const entt::entity item_entity = equipment->slots[index];
        if (item_entity == entt::null || !registry.valid(item_entity)) {
            continue;
        }

        auto* item = registry.template try_get<ItemComponent>(item_entity);
        if (!item) {
            continue;
        }

        if (item->std_mode != mir2::common::item_std_mode::kAmulet ||
            item->shape != mir2::common::amulet_shape::kTalisman) {
            continue;
        }

        if (out_slot) {
            *out_slot = slot;
        }
        if (out_item) {
            *out_item = item;
        }
        return item_entity;
    }

    return entt::null;
}

void RemoveTalisman(entt::registry& registry,
                    entt::entity character,
                    mir2::common::EquipSlot slot,
                    entt::entity item_entity) {
    auto* equipment = registry.try_get<EquipmentSlotComponent>(character);
    if (equipment) {
        const std::size_t index = static_cast<std::size_t>(slot);
        if (index < equipment->slots.size() && equipment->slots[index] == item_entity) {
            equipment->slots[index] = entt::null;
        }
    }

    if (registry.valid(item_entity)) {
        registry.destroy(item_entity);
    }

    dirty_tracker::mark_items_dirty(registry, character);
    dirty_tracker::mark_equipment_dirty(registry, character);
}

}  // namespace

bool CheckAmulet(const entt::registry& registry, entt::entity character) {
    const ItemComponent* item = nullptr;
    const entt::entity item_entity =
        FindTalismanEntity(registry, character, nullptr, &item);
    if (item_entity == entt::null || !item) {
        return false;
    }

    return item->durability >= mir2::common::durability::kAmuletConsumePerCast;
}

bool ConsumeAmulet(entt::registry& registry, entt::entity character) {
    ItemComponent* item = nullptr;
    mir2::common::EquipSlot slot = mir2::common::EquipSlot::AMULET;
    const entt::entity item_entity =
        FindTalismanEntity(registry, character, &slot, &item);
    if (item_entity == entt::null || !item) {
        return false;
    }

    const int consume = mir2::common::durability::kAmuletConsumePerCast;
    if (item->durability < consume) {
        RemoveTalisman(registry, character, slot, item_entity);
        return false;
    }

    item->durability = std::max(0, item->durability - consume);

    if (item->durability < consume) {
        RemoveTalisman(registry, character, slot, item_entity);
        return true;
    }

    dirty_tracker::mark_items_dirty(registry, character);
    dirty_tracker::mark_equipment_dirty(registry, character);
    return true;
}

}  // namespace mir2::ecs
