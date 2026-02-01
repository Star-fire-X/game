/**
 * @file monster.h
 * @brief Legend2 怪物结构定义
 */

#ifndef LEGEND2_MONSTER_H
#define LEGEND2_MONSTER_H

#include "common/types.h"

#include <algorithm>
#include <cstdint>
#include <string>

namespace legend2 {

// =============================================================================
// 怪物结构 (Monster Structure)
// =============================================================================

/**
 * @brief 怪物实体（用于战斗）
 */
struct Monster {
    uint32_t id = 0;                ///< 怪物实例ID
    uint32_t template_id = 0;       ///< 怪物模板ID
    std::string name;               ///< 怪物名称
    mir2::common::CharacterStats stats;           ///< 怪物属性
    mir2::common::Position position;              ///< 当前位置
    mir2::common::Position spawn_position;        ///< 出生点位置
    uint32_t map_id = 0;            ///< 所在地图ID
    mir2::common::MonsterState state = mir2::common::MonsterState::IDLE;  ///< AI状态
    int attack_range = 1;           ///< 攻击范围（瓦片）
    int aggro_range = 5;            ///< 仇恨检测范围
    int experience_reward = 10;     ///< 经验奖励
    int gold_reward = 5;            ///< 金币奖励
    float respawn_time = 30.0f;     ///< 复活时间（秒）

    /// 是否存活
    bool is_alive() const { return stats.hp > 0 && state != mir2::common::MonsterState::DEAD; }

    /// 是否死亡
    bool is_dead() const { return stats.hp <= 0 || state == mir2::common::MonsterState::DEAD; }

    /**
     * @brief 受到伤害
     * @param damage 伤害值
     * @return 实际受到的伤害
     */
    int take_damage(int damage) {
        if (damage <= 0 || is_dead()) return 0;
        int actual = std::max(1, damage);
        stats.hp = std::max(0, stats.hp - actual);
        if (stats.hp <= 0) {
            state = mir2::common::MonsterState::DEAD;
        }
        return actual;
    }

    /// 死亡
    void die() {
        stats.hp = 0;
        state = mir2::common::MonsterState::DEAD;
    }
};

}  // namespace legend2

#endif  // LEGEND2_MONSTER_H
