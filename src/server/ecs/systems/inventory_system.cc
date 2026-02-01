#include "ecs/systems/inventory_system.h"

#include "common/types/constants.h"
#include "ecs/components/character_components.h"
#include "ecs/dirty_tracker.h"
#include "ecs/event_bus.h"
#include "ecs/events/inventory_events.h"

#include <array>
#include <limits>

namespace mir2::ecs {

namespace {

constexpr int kMaxInventorySlots = mir2::common::constants::MAX_INVENTORY_SIZE;
constexpr int kMaxEquipmentSlots = mir2::common::constants::MAX_EQUIPMENT_SLOTS;

bool is_valid_inventory_slot(int slot_index) {
    return slot_index >= 0 && slot_index < kMaxInventorySlots;
}

bool is_valid_equipment_slot(int slot_index) {
    return slot_index >= 0 && slot_index < kMaxEquipmentSlots;
}

std::optional<mir2::common::EquipSlot> resolve_equip_slot(const ItemComponent& item) {
    if (!is_valid_equipment_slot(item.equip_slot)) {
        return std::nullopt;
    }
    return static_cast<mir2::common::EquipSlot>(item.equip_slot);
}

}  // namespace

InventorySystem::InventorySystem()
    : System(SystemPriority::kInventory) {}

void InventorySystem::Update(entt::registry& /*registry*/, float /*delta_time*/) {
    // TODO: 根据后续需求增加自动整理或过期物品处理逻辑。
}

int InventorySystem::FindFreeSlot(entt::registry& registry, entt::entity character) {
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

entt::entity InventorySystem::FindItemInSlot(entt::registry& registry,
                                             entt::entity character,
                                             int slot_index) {
    if (!is_valid_inventory_slot(slot_index)) {
        return entt::null;
    }

    auto view = registry.view<InventoryOwnerComponent, ItemComponent>();
    for (auto entity : view) {
        const auto& owner = view.get<InventoryOwnerComponent>(entity);
        if (owner.owner == character && owner.slot_index == slot_index) {
            return entity;
        }
    }

    return entt::null;
}

entt::entity InventorySystem::FindSkill(entt::registry& registry,
                                        entt::entity character,
                                        uint32_t skill_id) {
    auto view = registry.view<InventoryOwnerComponent, SkillComponent>();
    for (auto entity : view) {
        const auto& owner = view.get<InventoryOwnerComponent>(entity);
        const auto& skill = view.get<SkillComponent>(entity);
        if (owner.owner == character && skill.skill_id == skill_id) {
            return entity;
        }
    }
    return entt::null;
}

int InventorySystem::CountItem(entt::registry& registry,
                               entt::entity character,
                               uint32_t item_id) {
    if (!registry.valid(character) || item_id == 0) {
        return 0;
    }

    int total = 0;
    auto view = registry.view<InventoryOwnerComponent, ItemComponent>();
    for (auto entity : view) {
        const auto& owner = view.get<InventoryOwnerComponent>(entity);
        if (owner.owner != character) {
            continue;
        }
        const auto& item = view.get<ItemComponent>(entity);
        if (item.item_id != item_id || item.count <= 0) {
            continue;
        }
        if (total > std::numeric_limits<int>::max() - item.count) {
            return std::numeric_limits<int>::max();
        }
        total += item.count;
    }

    return total;
}

bool InventorySystem::HasItem(entt::registry& registry,
                              entt::entity character,
                              uint32_t item_id,
                              int count) {
    if (count <= 0) {
        return true;
    }

    return CountItem(registry, character, item_id) >= count;
}

std::optional<entt::entity> InventorySystem::AddItem(entt::registry& registry,
                                                     entt::entity character,
                                                     uint32_t item_id,
                                                     int count,
                                                     EventBus* event_bus) {
    if (!registry.valid(character)) {
        return std::nullopt;
    }
    if (item_id == 0 || count <= 0) {
        return std::nullopt;
    }

    const int free_slot = FindFreeSlot(registry, character);
    if (free_slot < 0) {
        return std::nullopt;
    }

    entt::entity item = registry.create();
    auto& item_component = registry.emplace<ItemComponent>(item);
    item_component.item_id = item_id;
    item_component.count = count;

    auto& owner = registry.emplace<InventoryOwnerComponent>(item);
    owner.owner = character;
    owner.slot_index = free_slot;

    dirty_tracker::mark_items_dirty(registry, character);

    if (event_bus) {
        events::ItemAddedEvent event;
        event.character = character;
        event.item = item;
        event.item_id = item_id;
        event.count = count;
        event.slot_index = free_slot;
        event_bus->Publish(event);
    }

    return item;
}

bool InventorySystem::EquipItem(entt::registry& registry,
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

    const auto equip_slot = resolve_equip_slot(*item_component);
    if (!equip_slot.has_value()) {
        return false;
    }

    if (item_component->count != 1) {
        return false;
    }

    auto& equipment = registry.get_or_emplace<EquipmentSlotComponent>(character);
    const std::size_t slot_index = static_cast<std::size_t>(*equip_slot);

    if (equipment.slots[slot_index] == item && owner->slot_index == -1) {
        return true;
    }

    if (!is_valid_inventory_slot(owner->slot_index)) {
        return false;
    }

    const int from_slot = owner->slot_index;
    entt::entity equipped_item = equipment.slots[slot_index];
    if (equipped_item != entt::null && equipped_item != item) {
        auto* equipped_owner = registry.try_get<InventoryOwnerComponent>(equipped_item);
        auto* equipped_component = registry.try_get<ItemComponent>(equipped_item);
        if (!equipped_owner || !equipped_component) {
            return false;
        }
        if (equipped_owner->owner != character) {
            return false;
        }

        equipped_owner->slot_index = from_slot;

        if (event_bus) {
            events::ItemUnequippedEvent unequip_event;
            unequip_event.character = character;
            unequip_event.item = equipped_item;
            unequip_event.item_id = equipped_component->item_id;
            unequip_event.slot = *equip_slot;
            unequip_event.slot_index = from_slot;
            event_bus->Publish(unequip_event);
        }
    }

    equipment.slots[slot_index] = item;
    owner->slot_index = -1;

    dirty_tracker::mark_items_dirty(registry, character);
    dirty_tracker::mark_equipment_dirty(registry, character);

    if (event_bus) {
        events::ItemEquippedEvent equip_event;
        equip_event.character = character;
        equip_event.item = item;
        equip_event.item_id = item_component->item_id;
        equip_event.slot = *equip_slot;
        event_bus->Publish(equip_event);
    }

    return true;
}

bool InventorySystem::UnequipItem(entt::registry& registry,
                                  entt::entity character,
                                  int slot_index,
                                  EventBus* event_bus) {
    if (!registry.valid(character)) {
        return false;
    }

    if (!is_valid_equipment_slot(slot_index)) {
        return false;
    }

    auto* equipment = registry.try_get<EquipmentSlotComponent>(character);
    if (!equipment) {
        return false;
    }

    entt::entity item = equipment->slots[static_cast<std::size_t>(slot_index)];
    if (item == entt::null || !registry.valid(item)) {
        return false;
    }

    auto* item_component = registry.try_get<ItemComponent>(item);
    if (!item_component) {
        return false;
    }

    const int free_slot = FindFreeSlot(registry, character);
    if (free_slot < 0) {
        return false;
    }

    auto& owner = registry.get_or_emplace<InventoryOwnerComponent>(item);
    if (owner.owner != entt::null && owner.owner != character) {
        return false;
    }

    owner.owner = character;
    owner.slot_index = free_slot;
    equipment->slots[static_cast<std::size_t>(slot_index)] = entt::null;

    dirty_tracker::mark_items_dirty(registry, character);
    dirty_tracker::mark_equipment_dirty(registry, character);

    if (event_bus) {
        events::ItemUnequippedEvent event;
        event.character = character;
        event.item = item;
        event.item_id = item_component->item_id;
        event.slot = static_cast<mir2::common::EquipSlot>(slot_index);
        event.slot_index = free_slot;
        event_bus->Publish(event);
    }

    return true;
}

bool InventorySystem::UseItem(entt::registry& registry,
                              entt::entity character,
                              entt::entity item,
                              int count,
                              EventBus* event_bus) {
    if (!registry.valid(character) || !registry.valid(item)) {
        return false;
    }

    if (count <= 0) {
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

    if (FindItemInSlot(registry, character, owner->slot_index) != item) {
        return false;
    }

    if (item_component->count < count) {
        return false;
    }

    item_component->count -= count;

    dirty_tracker::mark_items_dirty(registry, character);

    if (event_bus) {
        events::ItemUsedEvent event;
        event.character = character;
        event.item = item;
        event.item_id = item_component->item_id;
        event.used_count = count;
        event.remaining_count = item_component->count;
        event.slot_index = owner->slot_index;
        event_bus->Publish(event);
    }

    if (item_component->count == 0) {
        registry.destroy(item);
    }

    return true;
}

bool InventorySystem::DropItem(entt::registry& registry,
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

    if (FindItemInSlot(registry, character, owner->slot_index) != item) {
        return false;
    }

    const int slot_index = owner->slot_index;
    owner->owner = entt::null;
    owner->slot_index = -1;

    dirty_tracker::mark_items_dirty(registry, character);

    if (event_bus) {
        events::ItemDroppedEvent event;
        event.character = character;
        event.item = item;
        event.item_id = item_component->item_id;
        event.count = item_component->count;
        event.slot_index = slot_index;
        event_bus->Publish(event);
    }

    return true;
}

bool InventorySystem::PickupItem(entt::registry& registry,
                                 entt::entity character,
                                 entt::entity ground_item,
                                 EventBus* event_bus) {
    if (!registry.valid(character) || !registry.valid(ground_item)) {
        return false;
    }

    auto* item_component = registry.try_get<ItemComponent>(ground_item);
    auto* owner = registry.try_get<InventoryOwnerComponent>(ground_item);
    if (!item_component || !owner) {
        return false;
    }

    if (owner->owner != entt::null) {
        return false;
    }

    const int free_slot = FindFreeSlot(registry, character);
    if (free_slot < 0) {
        return false;
    }

    int32_t pickup_x = 0;
    int32_t pickup_y = 0;
    if (auto* state = registry.try_get<CharacterStateComponent>(ground_item)) {
        pickup_x = state->position.x;
        pickup_y = state->position.y;
        registry.remove<CharacterStateComponent>(ground_item);
    } else if (auto* state = registry.try_get<CharacterStateComponent>(character)) {
        pickup_x = state->position.x;
        pickup_y = state->position.y;
    }

    owner->owner = character;
    owner->slot_index = free_slot;

    dirty_tracker::mark_items_dirty(registry, character);

    if (event_bus) {
        events::ItemPickedUpEvent event;
        event.character = character;
        event.item = ground_item;
        event.item_id = item_component->item_id;
        event.count = item_component->count;
        event.slot_index = free_slot;
        event.pickup_x = pickup_x;
        event.pickup_y = pickup_y;
        event_bus->Publish(event);
    }

    return true;
}

std::optional<entt::entity> InventorySystem::LearnSkill(entt::registry& registry,
                                                        entt::entity character,
                                                        uint32_t skill_id,
                                                        int level,
                                                        EventBus* event_bus) {
    if (!registry.valid(character)) {
        return std::nullopt;
    }

    if (skill_id == 0 || level <= 0) {
        return std::nullopt;
    }

    if (FindSkill(registry, character, skill_id) != entt::null) {
        return std::nullopt;
    }

    entt::entity skill = registry.create();
    auto& skill_component = registry.emplace<SkillComponent>(skill);
    skill_component.skill_id = skill_id;
    skill_component.level = level;
    skill_component.exp = 0;

    auto& owner = registry.emplace<InventoryOwnerComponent>(skill);
    owner.owner = character;
    owner.slot_index = -1;

    dirty_tracker::mark_skills_dirty(registry, character);

    if (event_bus) {
        events::SkillLearnedEvent event;
        event.character = character;
        event.skill = skill;
        event.skill_id = skill_id;
        event.level = level;
        event_bus->Publish(event);
    }

    return skill;
}

bool InventorySystem::UpgradeSkill(entt::registry& registry,
                                   entt::entity character,
                                   uint32_t skill_id,
                                   int levels,
                                   EventBus* event_bus) {
    if (!registry.valid(character)) {
        return false;
    }

    if (skill_id == 0 || levels <= 0) {
        return false;
    }

    entt::entity skill = FindSkill(registry, character, skill_id);
    if (skill == entt::null) {
        return false;
    }

    auto* skill_component = registry.try_get<SkillComponent>(skill);
    if (!skill_component) {
        return false;
    }

    const int old_level = skill_component->level;
    skill_component->level += levels;

    dirty_tracker::mark_skills_dirty(registry, character);

    if (event_bus) {
        events::SkillUpgradedEvent event;
        event.character = character;
        event.skill = skill;
        event.skill_id = skill_id;
        event.old_level = old_level;
        event.new_level = skill_component->level;
        event_bus->Publish(event);
    }

    return true;
}

}  // namespace mir2::ecs
