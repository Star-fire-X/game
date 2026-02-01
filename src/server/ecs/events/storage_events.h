/**
 * @file storage_events.h
 * @brief 仓库事件定义
 */

#ifndef LEGEND2_SERVER_ECS_EVENTS_STORAGE_EVENTS_H
#define LEGEND2_SERVER_ECS_EVENTS_STORAGE_EVENTS_H

#include <cstdint>

#include <entt/entt.hpp>

namespace mir2::ecs::events {

/**
 * @brief 物品存入仓库事件
 */
struct ItemDepositedEvent {
    entt::entity character;
    entt::entity item;
    uint32_t item_id;
    int count;
    int inventory_slot_index;
    int storage_slot_index;
};

/**
 * @brief 物品从仓库取出事件
 */
struct ItemWithdrawnEvent {
    entt::entity character;
    entt::entity item;
    uint32_t item_id;
    int count;
    int storage_slot_index;
    int inventory_slot_index;
};

}  // namespace mir2::ecs::events

#endif  // LEGEND2_SERVER_ECS_EVENTS_STORAGE_EVENTS_H
