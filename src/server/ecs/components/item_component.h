/**
 * @file item_component.h
 * @brief ECS item instance components.
 *
 * Items are standalone entities. InventoryOwnerComponent links them to a character
 * and a bag slot. Static/template data lives in tables referenced by item_id.
 */

#ifndef LEGEND2_SERVER_ECS_ITEM_COMPONENT_H
#define LEGEND2_SERVER_ECS_ITEM_COMPONENT_H

#include <cstdint>

#include <entt/entt.hpp>

namespace mir2::ecs {

/**
 * @brief Per-item instance state (POD).
 *
 * Mirrors common Legend2/Mir2 instance fields: template id, stack count,
 * durability, and upgrade/luck modifiers.
 */
struct ItemComponent {
    uint64_t instance_id = 0;   ///< Persistent instance id (legacy compatibility).
    uint32_t item_id = 0;        ///< Item template id.
    int count = 1;               ///< Stack count (1 for non-stackable equipment).
    int durability = 0;          ///< Current durability (0..max_durability).
    int max_durability = 0;      ///< Maximum durability for this instance.
    int shape = 0;               ///< Item shape code (used for amulet type checks).
    int enhancement_level = 0;   ///< Enhancement/refine level (+1, +2, ...).
    int luck = 0;                ///< Luck/curse value (may be negative).
    int equip_slot = -1;         ///< Equip slot index (mir2::common::EquipSlot), -1 means not equippable.

    // 装备属性加成
    int attack_bonus = 0;
    int defense_bonus = 0;
    int magic_attack_bonus = 0;
    int magic_defense_bonus = 0;
    int hp_bonus = 0;
    int mp_bonus = 0;
    int hit_rate_bonus = 0;
    int dodge_bonus = 0;
    int speed_bonus = 0;

    // 特殊效果
    int lifesteal_percent = 0;    ///< 吸血百分比
    int reflect_percent = 0;      ///< 反伤百分比
    int elemental_damage = 0;     ///< 元素附加伤害
    int elemental_type = 0;       ///< 元素类型 (0=无, 1=火, 2=冰, 3=雷, 4=毒)
};

/**
 * @brief Inventory/skill ownership marker (POD).
 *
 * Attach to item or skill entities. owner points to the character entity.
 * slot_index is the bag slot index; -1 means not in the bag (equipped/learned).
 */
struct InventoryOwnerComponent {
    entt::entity owner = entt::null;  ///< Owning character entity.
    int slot_index = -1;              ///< Bag slot index (-1 means not in bag).
};

}  // namespace mir2::ecs

#endif  // LEGEND2_SERVER_ECS_ITEM_COMPONENT_H
