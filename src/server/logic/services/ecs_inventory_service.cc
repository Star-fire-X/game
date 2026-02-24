#include "logic/services/ecs_inventory_service.h"

#include <array>
#include <optional>

#include <entt/entt.hpp>

#include "common/types/constants.h"
#include "data/item_template.h"
#include "ecs/character_entity_manager.h"
#include "ecs/components/character_components.h"
#include "ecs/components/equipment_component.h"
#include "ecs/components/ground_item_component.h"
#include "ecs/components/item_component.h"
#include "ecs/registry_manager.h"
#include "ecs/systems/inventory_system.h"
#include "ecs/systems/trade_system.h"
#include "ecs/world.h"
#include "log/logger.h"
#include "logic/services/error_code_adapter.h"
#include "monitor/metrics.h"

namespace mir2::logic {

namespace {

constexpr int kMaxInventorySlots = mir2::common::constants::MAX_INVENTORY_SIZE;
constexpr int kMaxEquipmentSlots = mir2::common::constants::MAX_EQUIPMENT_SLOTS;
constexpr const char* kMetricSaveCriticalCallsTotal =
    "logic.ecs.save_critical.pickup.calls_total";
constexpr const char* kMetricSaveCriticalFailTotal =
    "logic.ecs.save_critical.pickup.fail_total";

bool IsValidSlot(int slot) {
  return slot >= 0 && slot < kMaxInventorySlots;
}

bool IsValidEquipmentSlot(int slot) {
  return slot >= 0 && slot < kMaxEquipmentSlots;
}

int FindFreeSlot(entt::registry& registry, entt::entity character) {
  std::array<bool, static_cast<std::size_t>(kMaxInventorySlots)> occupied{};
  auto view = registry.view<mir2::ecs::InventoryOwnerComponent, mir2::ecs::ItemComponent>();
  for (auto entity : view) {
    const auto& owner = view.get<mir2::ecs::InventoryOwnerComponent>(entity);
    if (owner.owner != character) {
      continue;
    }
    if (IsValidSlot(owner.slot_index)) {
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

entt::entity FindItemInSlot(entt::registry& registry, entt::entity character, int slot_index) {
  if (!IsValidSlot(slot_index)) {
    return entt::null;
  }

  auto view = registry.view<mir2::ecs::InventoryOwnerComponent, mir2::ecs::ItemComponent>();
  for (auto entity : view) {
    const auto& owner = view.get<mir2::ecs::InventoryOwnerComponent>(entity);
    if (owner.owner == character && owner.slot_index == slot_index) {
      return entity;
    }
  }

  return entt::null;
}

bool IsEquipped(entt::registry& registry, entt::entity item, entt::entity owner) {
  if (owner == entt::null || !registry.valid(owner)) {
    return false;
  }
  auto* equipment = registry.try_get<mir2::ecs::EquipmentSlotComponent>(owner);
  if (!equipment) {
    return false;
  }
  for (const auto equipped : equipment->slots) {
    if (equipped == item) {
      return true;
    }
  }
  return false;
}

entt::entity FindGroundItem(entt::registry& registry,
                            uint32_t item_id,
                            std::optional<uint32_t> map_id) {
  if (item_id == 0) {
    return entt::null;
  }

  const auto is_candidate = [&](entt::entity entity) {
    if (!registry.valid(entity)) {
      return false;
    }
    auto* item = registry.try_get<mir2::ecs::ItemComponent>(entity);
    auto* owner = registry.try_get<mir2::ecs::InventoryOwnerComponent>(entity);
    if (!item || !owner) {
      return false;
    }
    if (item->instance_id != item_id) {
      return false;
    }
    if (owner->slot_index >= 0) {
      return false;
    }
    if (IsEquipped(registry, entity, owner->owner)) {
      return false;
    }
    if (map_id.has_value()) {
      if (auto* state = registry.try_get<mir2::ecs::CharacterStateComponent>(entity)) {
        if (state->map_id != *map_id) {
          return false;
        }
      } else if (auto* ground = registry.try_get<mir2::ecs::GroundItemComponent>(entity)) {
        if (ground->map_id != *map_id) {
          return false;
        }
      }
    }
    return true;
  };

  const entt::entity direct = entt::entity{static_cast<entt::id_type>(item_id)};
  if (is_candidate(direct)) {
    return direct;
  }

  auto view = registry.view<mir2::ecs::ItemComponent, mir2::ecs::InventoryOwnerComponent>();
  for (auto entity : view) {
    const auto& item = view.get<mir2::ecs::ItemComponent>(entity);
    if (item.instance_id != item_id) {
      continue;
    }
    if (is_candidate(entity)) {
      return entity;
    }
  }

  return entt::null;
}

mir2::ecs::EventBus* ResolveEventBus(mir2::ecs::RegistryManager& registry_manager,
                                    std::optional<uint32_t> map_id) {
  if (!map_id.has_value()) {
    return nullptr;
  }
  auto* world = registry_manager.GetWorld(*map_id);
  return world ? &world->GetEventBus() : nullptr;
}

bool ShouldForceCriticalSaveOnPickup(const mir2::ecs::ItemComponent& item_component) {
  constexpr int kCriticalItemPriceThreshold = 100000;

  const auto* tmpl = data::ItemTemplateManager::Instance().GetTemplate(item_component.item_id);
  if (!tmpl) {
    return item_component.enhancement_level > 0 ||
           item_component.luck > 0 ||
           item_component.max_durability > 0;
  }

  if (!tmpl->stackable) {
    return true;
  }

  if (tmpl->price >= kCriticalItemPriceThreshold) {
    return true;
  }

  return item_component.enhancement_level > 0 || item_component.luck > 0;
}

void TryPersistCriticalPickup(mir2::ecs::CharacterEntityManager& character_manager,
                              uint32_t character_id,
                              const mir2::ecs::ItemComponent& item_component) {
  if (!ShouldForceCriticalSaveOnPickup(item_component)) {
    return;
  }

  monitor::Metrics::Instance().IncrementCounter(kMetricSaveCriticalCallsTotal);
  auto save_result = character_manager.SaveCritical(character_id);
  if (save_result == mir2::ecs::CharacterEntityManager::SaveResult::kEntityNotFound) {
    character_manager.RebuildIndex();
    save_result = character_manager.SaveCritical(character_id);
  }
  if (save_result != mir2::ecs::CharacterEntityManager::SaveResult::kSuccess) {
    monitor::Metrics::Instance().IncrementCounter(kMetricSaveCriticalFailTotal);
    SYSLOG_WARN("EcsInventoryService::PickupItem SaveCritical failed (character_id={}, item_id={}, result={})",
                character_id, item_component.item_id, static_cast<int>(save_result));
  }
}

}  // namespace

EcsInventoryService::EcsInventoryService(mir2::ecs::RegistryManager& registry_manager)
    : registry_manager_(registry_manager) {}

ItemPickupResult EcsInventoryService::PickupItem(uint64_t character_id, uint32_t item_id) {
  ItemPickupResult result;
  result.item_id = item_id;

  if (character_id == 0 || item_id == 0) {
    SYSLOG_WARN("EcsInventoryService::PickupItem invalid input (character_id={}, item_id={})",
                character_id, item_id);
    result.code = mir2::common::ErrorCode::kInvalidAction;
    return result;
  }

  auto& character_manager = registry_manager_.GetCharacterManager();
  auto character_opt = character_manager.TryGet(static_cast<uint32_t>(character_id));
  entt::entity character = character_opt.has_value() ? *character_opt : entt::null;
  entt::registry* registry = character_manager.TryGetRegistry(static_cast<uint32_t>(character_id));
  if (!registry || character == entt::null || !registry->valid(character)) {
    SYSLOG_WARN("EcsInventoryService::PickupItem invalid registry (character_id={})",
                character_id);
    result.code = mir2::common::ErrorCode::kInvalidAction;
    return result;
  }

  std::optional<uint32_t> map_id = character_manager.TryGetMapId(static_cast<uint32_t>(character_id));
  if (!map_id.has_value()) {
    if (auto* state = registry->try_get<mir2::ecs::CharacterStateComponent>(character)) {
      map_id = state->map_id;
    }
  }

  entt::entity ground_item = FindGroundItem(*registry, item_id, map_id);
  if (ground_item == entt::null) {
    SYSLOG_WARN("EcsInventoryService::PickupItem ground item not found (character_id={}, item_id={})",
                character_id, item_id);
    result.code = mir2::common::ErrorCode::kTargetNotFound;
    return result;
  }

  std::optional<mir2::ecs::ItemComponent> picked_item_snapshot;
  if (auto* picked_item = registry->try_get<mir2::ecs::ItemComponent>(ground_item)) {
    picked_item_snapshot = *picked_item;
  }

  const int free_slot = FindFreeSlot(*registry, character);
  if (free_slot < 0) {
    SYSLOG_WARN("EcsInventoryService::PickupItem inventory full (character_id={})",
                character_id);
    result.code = ToLegacyError(mir2::common::ErrorCode::INVENTORY_FULL);
    return result;
  }

  mir2::ecs::EventBus* event_bus = ResolveEventBus(registry_manager_, map_id);
  if (!mir2::ecs::InventorySystem::PickupItem(*registry, character, ground_item, event_bus)) {
    SYSLOG_WARN("EcsInventoryService::PickupItem failed (character_id={}, item_id={})",
                character_id, item_id);
    result.code = mir2::common::ErrorCode::kInvalidAction;
    return result;
  }

  result.code = mir2::common::ErrorCode::kOk;

  if (picked_item_snapshot.has_value()) {
    TryPersistCriticalPickup(character_manager,
                             static_cast<uint32_t>(character_id),
                             *picked_item_snapshot);
  }

  SYSLOG_DEBUG("EcsInventoryService::PickupItem character_id={} item_id={} slot={}",
               character_id, item_id, free_slot);

  return result;
}

ItemUseResult EcsInventoryService::UseItem(uint64_t character_id,
                                           uint16_t slot,
                                           uint32_t item_id) {
  ItemUseResult result;
  result.slot = slot;
  result.item_id = item_id;

  if (character_id == 0 || item_id == 0) {
    SYSLOG_WARN("EcsInventoryService::UseItem invalid input (character_id={}, item_id={})",
                character_id, item_id);
    result.code = mir2::common::ErrorCode::kInvalidAction;
    return result;
  }

  if (!IsValidSlot(slot)) {
    SYSLOG_WARN("EcsInventoryService::UseItem invalid slot (character_id={}, slot={})",
                character_id, slot);
    result.code = mir2::common::ErrorCode::kInvalidAction;
    return result;
  }

  auto& character_manager = registry_manager_.GetCharacterManager();
  auto character_opt = character_manager.TryGet(static_cast<uint32_t>(character_id));
  entt::entity character = character_opt.has_value() ? *character_opt : entt::null;
  entt::registry* registry = character_manager.TryGetRegistry(static_cast<uint32_t>(character_id));
  if (!registry || character == entt::null || !registry->valid(character)) {
    SYSLOG_WARN("EcsInventoryService::UseItem invalid registry (character_id={})",
                character_id);
    result.code = mir2::common::ErrorCode::kInvalidAction;
    return result;
  }

  entt::entity item_entity = FindItemInSlot(*registry, character, static_cast<int>(slot));
  if (item_entity == entt::null) {
    SYSLOG_WARN("EcsInventoryService::UseItem item not found (character_id={}, slot={})",
                character_id, slot);
    result.code = ToLegacyError(mir2::common::ErrorCode::ITEM_NOT_FOUND);
    return result;
  }

  auto* item_component = registry->try_get<mir2::ecs::ItemComponent>(item_entity);
  if (!item_component || item_component->item_id != item_id) {
    SYSLOG_WARN("EcsInventoryService::UseItem item mismatch (character_id={}, slot={}, item_id={})",
                character_id, slot, item_id);
    result.code = ToLegacyError(mir2::common::ErrorCode::ITEM_NOT_FOUND);
    return result;
  }

  const auto map_id = character_manager.TryGetMapId(static_cast<uint32_t>(character_id));
  mir2::ecs::EventBus* event_bus = ResolveEventBus(registry_manager_, map_id);
  if (!mir2::ecs::InventorySystem::UseItem(*registry, character, item_entity, 1, event_bus)) {
    SYSLOG_WARN("EcsInventoryService::UseItem failed (character_id={}, slot={}, item_id={})",
                character_id, slot, item_id);
    result.code = mir2::common::ErrorCode::kInvalidAction;
    return result;
  }

  uint32_t remaining = 0;
  if (registry->valid(item_entity)) {
    if (auto* remaining_item = registry->try_get<mir2::ecs::ItemComponent>(item_entity)) {
      remaining = remaining_item->count < 0
                      ? 0
                      : static_cast<uint32_t>(remaining_item->count);
    }
  }
  result.remaining = remaining;
  result.code = mir2::common::ErrorCode::kOk;

  SYSLOG_DEBUG("EcsInventoryService::UseItem character_id={} slot={} item_id={} remaining={}",
               character_id, slot, item_id, result.remaining);

  return result;
}

ItemDropResult EcsInventoryService::DropItem(uint64_t character_id,
                                            uint16_t slot,
                                            uint32_t item_id,
                                            uint32_t count) {
  ItemDropResult result;
  result.item_id = item_id;
  result.count = count;

  if (character_id == 0 || item_id == 0 || count == 0) {
    SYSLOG_WARN("EcsInventoryService::DropItem invalid input (character_id={}, item_id={}, count={})",
                character_id, item_id, count);
    result.code = mir2::common::ErrorCode::kInvalidAction;
    return result;
  }

  if (!IsValidSlot(slot)) {
    SYSLOG_WARN("EcsInventoryService::DropItem invalid slot (character_id={}, slot={})",
                character_id, slot);
    result.code = mir2::common::ErrorCode::kInvalidAction;
    return result;
  }

  auto& character_manager = registry_manager_.GetCharacterManager();
  auto character_opt = character_manager.TryGet(static_cast<uint32_t>(character_id));
  entt::entity character = character_opt.has_value() ? *character_opt : entt::null;
  entt::registry* registry = character_manager.TryGetRegistry(static_cast<uint32_t>(character_id));
  if (!registry || character == entt::null || !registry->valid(character)) {
    SYSLOG_WARN("EcsInventoryService::DropItem invalid registry (character_id={})",
                character_id);
    result.code = mir2::common::ErrorCode::kInvalidAction;
    return result;
  }

  entt::entity item_entity = FindItemInSlot(*registry, character, static_cast<int>(slot));
  if (item_entity == entt::null) {
    SYSLOG_WARN("EcsInventoryService::DropItem item not found (character_id={}, slot={})",
                character_id, slot);
    result.code = ToLegacyError(mir2::common::ErrorCode::ITEM_NOT_FOUND);
    return result;
  }

  auto* item_component = registry->try_get<mir2::ecs::ItemComponent>(item_entity);
  if (!item_component || item_component->item_id != item_id) {
    SYSLOG_WARN("EcsInventoryService::DropItem item mismatch (character_id={}, slot={}, item_id={})",
                character_id, slot, item_id);
    result.code = ToLegacyError(mir2::common::ErrorCode::ITEM_NOT_FOUND);
    return result;
  }

  if (count != static_cast<uint32_t>(item_component->count)) {
    SYSLOG_WARN("EcsInventoryService::DropItem partial drop not supported (character_id={}, slot={}, count={}, stack={})",
                character_id, slot, count, item_component->count);
    result.code = mir2::common::ErrorCode::kInvalidAction;
    return result;
  }

  const auto map_id = character_manager.TryGetMapId(static_cast<uint32_t>(character_id));
  mir2::ecs::EventBus* event_bus = ResolveEventBus(registry_manager_, map_id);
  if (!mir2::ecs::InventorySystem::DropItem(*registry, character, item_entity, event_bus)) {
    SYSLOG_WARN("EcsInventoryService::DropItem failed (character_id={}, slot={}, item_id={}, count={})",
                character_id, slot, item_id, count);
    result.code = mir2::common::ErrorCode::kInvalidAction;
    return result;
  }

  result.code = mir2::common::ErrorCode::kOk;

  SYSLOG_DEBUG("EcsInventoryService::DropItem character_id={} slot={} item_id={} count={}",
               character_id, slot, item_id, count);

  return result;
}

ItemEquipResult EcsInventoryService::EquipItem(uint64_t character_id,
                                               uint16_t slot,
                                               uint32_t item_id) {
  ItemEquipResult result;
  result.slot = slot;
  result.item_id = item_id;

  if (character_id == 0 || item_id == 0) {
    SYSLOG_WARN("EcsInventoryService::EquipItem invalid input (character_id={}, item_id={})",
                character_id, item_id);
    result.code = mir2::common::ErrorCode::kInvalidAction;
    return result;
  }

  if (!IsValidSlot(slot)) {
    SYSLOG_WARN("EcsInventoryService::EquipItem invalid inventory slot (character_id={}, slot={})",
                character_id, slot);
    result.code = mir2::common::ErrorCode::kInvalidAction;
    return result;
  }

  auto& character_manager = registry_manager_.GetCharacterManager();
  auto character_opt = character_manager.TryGet(static_cast<uint32_t>(character_id));
  entt::entity character = character_opt.has_value() ? *character_opt : entt::null;
  entt::registry* registry = character_manager.TryGetRegistry(static_cast<uint32_t>(character_id));
  if (!registry || character == entt::null || !registry->valid(character)) {
    SYSLOG_WARN("EcsInventoryService::EquipItem invalid registry (character_id={})",
                character_id);
    result.code = mir2::common::ErrorCode::kInvalidAction;
    return result;
  }

  entt::entity item_entity = FindItemInSlot(*registry, character, static_cast<int>(slot));
  if (item_entity == entt::null) {
    SYSLOG_WARN("EcsInventoryService::EquipItem item not found (character_id={}, slot={})",
                character_id, slot);
    result.code = ToLegacyError(mir2::common::ErrorCode::ITEM_NOT_FOUND);
    return result;
  }

  auto* item_component = registry->try_get<mir2::ecs::ItemComponent>(item_entity);
  if (!item_component || item_component->item_id != item_id) {
    SYSLOG_WARN("EcsInventoryService::EquipItem item mismatch (character_id={}, slot={}, item_id={})",
                character_id, slot, item_id);
    result.code = ToLegacyError(mir2::common::ErrorCode::ITEM_NOT_FOUND);
    return result;
  }

  if (!IsValidEquipmentSlot(item_component->equip_slot)) {
    SYSLOG_WARN("EcsInventoryService::EquipItem invalid equip slot (character_id={}, slot={}, equip_slot={})",
                character_id, slot, item_component->equip_slot);
    result.code = mir2::common::ErrorCode::kInvalidAction;
    return result;
  }

  result.slot = static_cast<uint16_t>(item_component->equip_slot);
  const auto map_id = character_manager.TryGetMapId(static_cast<uint32_t>(character_id));
  mir2::ecs::EventBus* event_bus = ResolveEventBus(registry_manager_, map_id);
  if (!mir2::ecs::InventorySystem::EquipItem(*registry, character, item_entity, event_bus)) {
    SYSLOG_WARN("EcsInventoryService::EquipItem failed (character_id={}, slot={}, item_id={})",
                character_id, slot, item_id);
    result.code = mir2::common::ErrorCode::kInvalidAction;
    return result;
  }

  result.code = mir2::common::ErrorCode::kOk;
  SYSLOG_DEBUG("EcsInventoryService::EquipItem character_id={} slot={} equip_slot={} item_id={}",
               character_id, slot, result.slot, item_id);

  return result;
}

ItemUnequipResult EcsInventoryService::UnequipItem(uint64_t character_id, uint16_t slot) {
  ItemUnequipResult result;
  result.slot = slot;

  if (character_id == 0) {
    SYSLOG_WARN("EcsInventoryService::UnequipItem invalid character_id=0");
    result.code = mir2::common::ErrorCode::kInvalidAction;
    return result;
  }

  if (!IsValidEquipmentSlot(slot)) {
    SYSLOG_WARN("EcsInventoryService::UnequipItem invalid equip slot (character_id={}, slot={})",
                character_id, slot);
    result.code = mir2::common::ErrorCode::kInvalidAction;
    return result;
  }

  auto& character_manager = registry_manager_.GetCharacterManager();
  auto character_opt = character_manager.TryGet(static_cast<uint32_t>(character_id));
  entt::entity character = character_opt.has_value() ? *character_opt : entt::null;
  entt::registry* registry = character_manager.TryGetRegistry(static_cast<uint32_t>(character_id));
  if (!registry || character == entt::null || !registry->valid(character)) {
    SYSLOG_WARN("EcsInventoryService::UnequipItem invalid registry (character_id={})",
                character_id);
    result.code = mir2::common::ErrorCode::kInvalidAction;
    return result;
  }

  auto* equipment = registry->try_get<mir2::ecs::EquipmentSlotComponent>(character);
  if (!equipment) {
    SYSLOG_WARN("EcsInventoryService::UnequipItem missing equipment component (character_id={})",
                character_id);
    result.code = mir2::common::ErrorCode::kInvalidAction;
    return result;
  }

  const std::size_t equipment_slot = static_cast<std::size_t>(slot);
  entt::entity equipped_item = equipment->slots[equipment_slot];
  if (equipped_item == entt::null || !registry->valid(equipped_item)) {
    SYSLOG_WARN("EcsInventoryService::UnequipItem item not found (character_id={}, slot={})",
                character_id, slot);
    result.code = ToLegacyError(mir2::common::ErrorCode::ITEM_NOT_FOUND);
    return result;
  }

  auto* item_component = registry->try_get<mir2::ecs::ItemComponent>(equipped_item);
  if (!item_component) {
    SYSLOG_WARN("EcsInventoryService::UnequipItem invalid item entity (character_id={}, slot={})",
                character_id, slot);
    result.code = mir2::common::ErrorCode::kInvalidAction;
    return result;
  }

  result.item_id = item_component->item_id;
  if (FindFreeSlot(*registry, character) < 0) {
    SYSLOG_WARN("EcsInventoryService::UnequipItem inventory full (character_id={}, slot={})",
                character_id, slot);
    result.code = ToLegacyError(mir2::common::ErrorCode::INVENTORY_FULL);
    return result;
  }

  const auto map_id = character_manager.TryGetMapId(static_cast<uint32_t>(character_id));
  mir2::ecs::EventBus* event_bus = ResolveEventBus(registry_manager_, map_id);
  if (!mir2::ecs::InventorySystem::UnequipItem(
          *registry, character, static_cast<int>(slot), event_bus)) {
    SYSLOG_WARN("EcsInventoryService::UnequipItem failed (character_id={}, slot={})",
                character_id, slot);
    result.code = mir2::common::ErrorCode::kInvalidAction;
    return result;
  }

  result.code = mir2::common::ErrorCode::kOk;
  SYSLOG_DEBUG("EcsInventoryService::UnequipItem character_id={} slot={} item_id={}",
               character_id, slot, result.item_id);

  return result;
}

bool EcsInventoryService::ExecuteTradeAtomic(entt::registry& registry,
                                             entt::entity trader_a,
                                             entt::entity trader_b,
                                             mir2::ecs::EventBus* event_bus) {
  if (trader_a == entt::null || trader_b == entt::null || !registry.valid(trader_a) ||
      !registry.valid(trader_b)) {
    SYSLOG_WARN("EcsInventoryService::ExecuteTradeAtomic invalid entities");
    return false;
  }

  return mir2::ecs::TradeSystem::ExecuteTrade(registry, trader_a, trader_b, event_bus);
}

}  // namespace mir2::logic
