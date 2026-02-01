/**
 * @file dirty_tracker.h
 * @brief 角色脏标记辅助函数
 */

#ifndef LEGEND2_SERVER_ECS_DIRTY_TRACKER_H
#define LEGEND2_SERVER_ECS_DIRTY_TRACKER_H

#include "ecs/components/character_components.h"

#include <entt/entt.hpp>

namespace mir2::ecs {

namespace dirty_tracker {

inline void mark_identity_dirty(entt::registry& registry, entt::entity entity) {
    auto& dirty = registry.get_or_emplace<DirtyComponent>(entity);
    dirty.identity_dirty = true;
}

inline void mark_attributes_dirty(entt::registry& registry, entt::entity entity) {
    auto& dirty = registry.get_or_emplace<DirtyComponent>(entity);
    dirty.attributes_dirty = true;
}

inline void mark_state_dirty(entt::registry& registry, entt::entity entity) {
    auto& dirty = registry.get_or_emplace<DirtyComponent>(entity);
    dirty.state_dirty = true;
}

/// 兼容旧背包标记：同时标记物品/装备/技能
inline void mark_inventory_dirty(entt::registry& registry, entt::entity entity) {
    auto& dirty = registry.get_or_emplace<DirtyComponent>(entity);
    dirty.inventory_dirty = true;
    dirty.items_dirty = true;
    dirty.equipment_dirty = true;
    dirty.skills_dirty = true;
}

/// 标记物品脏数据，同时保持旧背包标记可用
inline void mark_items_dirty(entt::registry& registry, entt::entity entity) {
    auto& dirty = registry.get_or_emplace<DirtyComponent>(entity);
    dirty.items_dirty = true;
    dirty.inventory_dirty = true;
}

/// 标记装备脏数据，同时保持旧背包标记可用
inline void mark_equipment_dirty(entt::registry& registry, entt::entity entity) {
    auto& dirty = registry.get_or_emplace<DirtyComponent>(entity);
    dirty.equipment_dirty = true;
    dirty.inventory_dirty = true;
}

/// 标记技能脏数据，同时保持旧背包标记可用
inline void mark_skills_dirty(entt::registry& registry, entt::entity entity) {
    auto& dirty = registry.get_or_emplace<DirtyComponent>(entity);
    dirty.skills_dirty = true;
    dirty.inventory_dirty = true;
}

inline bool is_dirty(entt::registry& registry, entt::entity entity) {
    auto* dirty = registry.try_get<DirtyComponent>(entity);
    if (!dirty) {
        return false;
    }
    return dirty->identity_dirty || dirty->attributes_dirty ||
           dirty->state_dirty || dirty->inventory_dirty ||
           dirty->items_dirty || dirty->equipment_dirty ||
           dirty->skills_dirty;
}

inline void clear_dirty(entt::registry& registry, entt::entity entity) {
    if (registry.all_of<DirtyComponent>(entity)) {
        registry.remove<DirtyComponent>(entity);
    }
}

}  // namespace dirty_tracker

}  // namespace mir2::ecs

#endif  // LEGEND2_SERVER_ECS_DIRTY_TRACKER_H
