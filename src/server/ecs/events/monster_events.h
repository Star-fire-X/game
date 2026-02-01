/**
 * @file monster_events.h
 * @brief 怪物相关事件定义
 */

#ifndef MIR2_ECS_EVENTS_MONSTER_EVENTS_H
#define MIR2_ECS_EVENTS_MONSTER_EVENTS_H

#include <entt/entt.hpp>
#include <cstdint>
#include "common/types.h"

namespace mir2::ecs::events {

/**
 * @brief 怪物死亡事件
 */
struct MonsterDeadEvent {
    uint64_t monster_id = 0;            ///< 怪物ID
    uint32_t spawn_point_id = 0;        ///< 关联的刷新点ID
    entt::entity killer = entt::null;   ///< 击杀者实体
    mir2::common::Position death_position;   ///< 死亡位置
};

/**
 * @brief 怪物复活事件
 */
struct MonsterRespawnEvent {
    uint64_t monster_id = 0;            ///< 怪物ID
    uint32_t spawn_point_id = 0;        ///< 刷新点ID
    mir2::common::Position respawn_pos;      ///< 复活位置
};

/**
 * @brief 怪物召唤事件
 */
struct MonsterSummonEvent {
    entt::entity summoner = entt::null; ///< 召唤者实体
    mir2::common::Position position;         ///< 召唤位置
    uint32_t map_id = 0;                ///< 地图ID
};

/**
 * @brief 动态刷新触发事件
 */
struct DynamicSpawnTriggerEvent {
    uint32_t spawn_id = 0;              ///< 刷新点ID
    int32_t wave_count = 1;             ///< 批次数量
    float wave_interval = 5.0f;         ///< 批次间隔
};

}  // namespace mir2::ecs::events

#endif  // MIR2_ECS_EVENTS_MONSTER_EVENTS_H
