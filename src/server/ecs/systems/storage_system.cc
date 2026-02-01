#include "ecs/systems/storage_system.h"

#include "common/types/constants.h"
#include "ecs/dirty_tracker.h"
#include "ecs/event_bus.h"
#include "ecs/events/npc_events.h"
#include "ecs/events/storage_events.h"

#include <array>

namespace mir2::ecs {

namespace {

constexpr int kMaxInventorySlots = mir2::common::constants::MAX_INVENTORY_SIZE;

bool is_valid_inventory_slot(int slot_index) {
    return slot_index >= 0 && slot_index < kMaxInventorySlots;
}

bool is_valid_storage_slot(int slot_index) {
    return slot_index >= 0 && slot_index < kMaxStorageSlots;
}

int find_free_inventory_slot(entt::registry& registry, entt::entity character) {
    std::array<bool, static_cast<std::size_t>(kMaxInventorySlots)> occupied{};
    auto view = registry.view<InventoryOwnerComponent, ItemComponent>();
    for (auto entity : view) {
        const auto& owner = view.get<InventoryOwnerComponent>(entity);
        if (owner.owner != character) {
            continue;
        }
        if (is_valid_inventory_slot(owner.slot_index)) {
            occupied[static_cast<std::size_t>(owner.slot_index)] = true;
        }
    }

    for (int i = 0; i < kMaxInventorySlots; ++i) {
        if (!occupied[static_cast<std::size_t>(i)]) {
            return i;
        }
    }

    return -1;
}

void cleanup_storage(entt::registry& registry, StorageComponent& storage) {
    for (auto& slot : storage.slots) {
        if (slot != entt::null && !registry.valid(slot)) {
            slot = entt::null;
        }
    }
}

}  // namespace

StorageSystem::StorageSystem()
    : System(SystemPriority::kInventory) {}

StorageSystem::StorageSystem(entt::registry& registry, EventBus& event_bus)
    : System(SystemPriority::kInventory) {
    RegisterHandlers(registry, event_bus);
}

void StorageSystem::Update(entt::registry& /*registry*/, float /*delta_time*/) {
    // TODO: 根据后续需求增加仓库整理/过期物品处理逻辑。
}

void StorageSystem::RegisterHandlers(entt::registry& registry, EventBus& event_bus) {
    if (handlers_registered_) {
        return;
    }

    event_bus.Subscribe<events::NpcOpenStorageEvent>(
        [this, &registry, &event_bus](const events::NpcOpenStorageEvent& event) {
            HandleOpenStorage(registry, event_bus, event);
        });

    handlers_registered_ = true;
}

void StorageSystem::HandleOpenStorage(entt::registry& registry,
                                      EventBus& /*event_bus*/,
                                      const events::NpcOpenStorageEvent& event) {
    if (!registry.valid(event.player)) {
        return;
    }

    auto& storage = registry.get_or_emplace<StorageComponent>(event.player);
    cleanup_storage(registry, storage);
}

bool StorageSystem::DepositItem(entt::registry& registry,
                                entt::entity character,
                                entt::entity item,
                                EventBus* event_bus) {
    if (!registry.valid(character) || !registry.valid(item)) {
        return false;
    }

    auto* item_component = registry.try_get<ItemComponent>(item);
    auto* owner = registry.try_get<InventoryOwnerComponent>(item);
    if (!item_component || !owner) {
        return false;
    }
    if (owner->owner != character) {
        return false;
    }
    if (!is_valid_inventory_slot(owner->slot_index)) {
        return false;
    }

    auto& storage = registry.get_or_emplace<StorageComponent>(character);
    cleanup_storage(registry, storage);
    const int storage_slot = storage.FindFreeSlot();
    if (storage_slot < 0) {
        return false;
    }

    const int inventory_slot_index = owner->slot_index;
    storage.slots[static_cast<std::size_t>(storage_slot)] = item;
    registry.remove<InventoryOwnerComponent>(item);

    dirty_tracker::mark_items_dirty(registry, character);

    if (event_bus) {
        events::ItemDepositedEvent event;
        event.character = character;
        event.item = item;
        event.item_id = item_component->item_id;
        event.count = item_component->count;
        event.inventory_slot_index = inventory_slot_index;
        event.storage_slot_index = storage_slot;
        event_bus->Publish(event);
    }

    return true;
}

bool StorageSystem::WithdrawItem(entt::registry& registry,
                                 entt::entity character,
                                 int storage_slot,
                                 EventBus* event_bus) {
    if (!registry.valid(character)) {
        return false;
    }
    if (!is_valid_storage_slot(storage_slot)) {
        return false;
    }

    auto* storage = registry.try_get<StorageComponent>(character);
    if (!storage) {
        return false;
    }

    cleanup_storage(registry, *storage);
    entt::entity item = storage->slots[static_cast<std::size_t>(storage_slot)];
    if (item == entt::null || !registry.valid(item)) {
        storage->slots[static_cast<std::size_t>(storage_slot)] = entt::null;
        return false;
    }

    auto* item_component = registry.try_get<ItemComponent>(item);
    if (!item_component) {
        return false;
    }

    const int inventory_slot = find_free_inventory_slot(registry, character);
    if (inventory_slot < 0) {
        return false;
    }

    auto* owner = registry.try_get<InventoryOwnerComponent>(item);
    if (owner && owner->owner != entt::null && owner->owner != character) {
        return false;
    }

    storage->slots[static_cast<std::size_t>(storage_slot)] = entt::null;

    if (owner) {
        owner->owner = character;
        owner->slot_index = inventory_slot;
    } else {
        auto& new_owner = registry.emplace<InventoryOwnerComponent>(item);
        new_owner.owner = character;
        new_owner.slot_index = inventory_slot;
    }

    dirty_tracker::mark_items_dirty(registry, character);

    if (event_bus) {
        events::ItemWithdrawnEvent event;
        event.character = character;
        event.item = item;
        event.item_id = item_component->item_id;
        event.count = item_component->count;
        event.storage_slot_index = storage_slot;
        event.inventory_slot_index = inventory_slot;
        event_bus->Publish(event);
    }

    return true;
}

std::vector<entt::entity> StorageSystem::GetStorageItems(entt::registry& registry,
                                                         entt::entity character) {
    std::vector<entt::entity> result;
    if (!registry.valid(character)) {
        return result;
    }

    auto* storage = registry.try_get<StorageComponent>(character);
    if (!storage) {
        return result;
    }

    cleanup_storage(registry, *storage);
    result.reserve(storage->slots.size());
    for (const auto& slot : storage->slots) {
        if (slot == entt::null) {
            continue;
        }
        if (!registry.valid(slot)) {
            continue;
        }
        result.push_back(slot);
    }

    return result;
}

}  // namespace mir2::ecs
