/**
 * @file inventory_events.h
 * @brief 物品/装备/技能变更事件定义
 */

#ifndef MIR2_ECS_EVENTS_INVENTORY_EVENTS_H
#define MIR2_ECS_EVENTS_INVENTORY_EVENTS_H

#include <entt/entt.hpp>

#include "common/enums.h"

namespace mir2::ecs::events {

/**
 * @brief 背包新增物品事件
 */
struct ItemAddedEvent {
    entt::entity character;
    entt::entity item;
    uint32_t item_id;
    int count;
    int slot_index;
};

/**
 * @brief 物品被使用事件
 */
struct ItemUsedEvent {
    entt::entity character;
    entt::entity item;
    uint32_t item_id;
    int used_count;
    int remaining_count;
    int slot_index;
};

/**
 * @brief 物品被丢弃事件
 */
struct ItemDroppedEvent {
    entt::entity character;
    entt::entity item;
    uint32_t item_id;
    int count;
    int slot_index;
};

/**
 * @brief 物品拾取事件
 */
struct ItemPickedUpEvent {
    entt::entity character;
    entt::entity item;
    uint32_t item_id;
    int count;
    int slot_index;
    int32_t pickup_x;
    int32_t pickup_y;
};

/**
 * @brief 物品装备事件
 */
struct ItemEquippedEvent {
    entt::entity character;
    entt::entity item;
    uint32_t item_id;
    mir2::common::EquipSlot slot;
};

/**
 * @brief 物品卸下事件
 */
struct ItemUnequippedEvent {
    entt::entity character;
    entt::entity item;
    uint32_t item_id;
    mir2::common::EquipSlot slot;
    int slot_index;
};

/**
 * @brief 技能学习事件
 */
struct SkillLearnedEvent {
    entt::entity character;
    entt::entity skill;
    uint32_t skill_id;
    int level;
};

/**
 * @brief 技能升级事件
 */
struct SkillUpgradedEvent {
    entt::entity character;
    entt::entity skill;
    uint32_t skill_id;
    int old_level;
    int new_level;
};

}  // namespace mir2::ecs::events

#endif  // MIR2_ECS_EVENTS_INVENTORY_EVENTS_H
