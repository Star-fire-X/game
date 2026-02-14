/**
 * @file inventory_snapshot_component.h
 * @brief Inventory compatibility snapshot component definitions.
 *
 * Runtime inventory/equipment/storage/trade logic uses entity-item links
 * (`ItemComponent` + owner/slot relationships). These snapshot structs are
 * retained only for persistence/network compatibility boundaries.
 */

#ifndef MIR2_SERVER_ECS_INVENTORY_SNAPSHOT_COMPONENT_H_
#define MIR2_SERVER_ECS_INVENTORY_SNAPSHOT_COMPONENT_H_

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace mir2::ecs {

/**
 * @brief Compatibility snapshot for item instance data.
 *
 * This is not the runtime source of truth; runtime uses `ItemComponent`.
 */
struct InventorySnapshotItemData {
    uint64_t instance_id = 0;
    uint32_t item_id = 0;
    int count = 1;
    int durability = 0;
    int max_durability = 0;
    int enhancement_level = 0;

    // Runtime-compat fields kept for snapshot round-trip.
    int shape = 0;
    int looks = 0;
    int std_mode = 0;
    int luck = 0;
    int equip_slot = -1;
};

/**
 * @brief Compatibility snapshot for learned skill data.
 */
struct InventorySnapshotSkillData {
    uint32_t skill_id = 0;
    uint8_t level = 0;
    uint64_t cooldown_end_ms = 0;
};

/**
 * @brief Compatibility snapshot component for inventory/equipment/skills.
 *
 * Only used at JSON/DB conversion boundaries.
 */
struct InventorySnapshotComponent {
    static constexpr std::size_t kMaxSlots = 46;
    static constexpr std::size_t kMaxEquipmentSlots = 13;

    std::array<std::optional<InventorySnapshotItemData>, kMaxSlots> slots;
    std::array<std::optional<InventorySnapshotItemData>, kMaxEquipmentSlots> equipment;
    std::vector<InventorySnapshotSkillData> skills;
};

// Backward-compatible aliases.
using ItemData = InventorySnapshotItemData;
using SkillData = InventorySnapshotSkillData;
using InventoryComponent = InventorySnapshotComponent;

}  // namespace mir2::ecs

#endif  // MIR2_SERVER_ECS_INVENTORY_SNAPSHOT_COMPONENT_H_
