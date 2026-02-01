/**
 * @file monster_spawn_config.h
 * @brief 怪物刷新配置数据结构
 */

#ifndef MIR2_GAME_ENTITY_MONSTER_SPAWN_CONFIG_H
#define MIR2_GAME_ENTITY_MONSTER_SPAWN_CONFIG_H

#include <cstdint>
#include "common/types.h"

namespace mir2::game::entity {

/**
 * @brief 怪物刷新点配置
 */
struct MonsterSpawnPoint {
    uint32_t spawn_id = 0;              ///< 刷新点ID
    uint32_t map_id = 0;                ///< 地图ID
    int32_t center_x = 0;               ///< 中心X坐标
    int32_t center_y = 0;               ///< 中心Y坐标
    int32_t spawn_radius = 5;           ///< 刷新半径
    uint32_t monster_template_id = 0;   ///< 怪物模板ID
    int32_t patrol_radius = 5;          ///< 巡逻范围
    float respawn_interval = 30.0f;     ///< 复活间隔（秒）
    int32_t max_count = 1;              ///< 最大怪物数
    int32_t current_count = 0;          ///< 当前数量
    float last_spawn_time = 0.0f;       ///< 上次刷新时间（秒）
    int32_t aggro_range = 12;           ///< 仇恨范围
    int32_t attack_range = 3;           ///< 攻击范围
};

/**
 * @brief 动态刷新事件配置
 */
struct DynamicSpawnEvent {
    uint32_t spawn_id = 0;              ///< 刷新点ID
    int32_t wave_count = 1;             ///< 刷新批次
    float wave_interval = 5.0f;         ///< 批次间隔（秒）
    float duration = 60.0f;             ///< 总持续时间（秒）
    bool repeat = false;                ///< 是否重复
};

}  // namespace mir2::game::entity

#endif  // MIR2_GAME_ENTITY_MONSTER_SPAWN_CONFIG_H
