/**
 * @file combat_component.h
 * @brief ECS 战斗组件定义
 */

#ifndef LEGEND2_SERVER_ECS_COMBAT_COMPONENT_H
#define LEGEND2_SERVER_ECS_COMBAT_COMPONENT_H

namespace mir2::ecs {

/**
 * @brief 战斗属性组件
 */
struct CombatComponent {
    float critical_chance = 0.05f;  ///< 暴击率
    float evasion_chance = 0.05f;   ///< 闪避率
    int attack_range = 1;           ///< 攻击范围（瓦片）

    // 元素抗性
    int fire_resistance = 0;       ///< 火抗
    int ice_resistance = 0;        ///< 冰抗
    int lightning_resistance = 0;  ///< 雷抗
    int poison_resistance = 0;     ///< 毒抗
};

}  // namespace mir2::ecs

#endif  // LEGEND2_SERVER_ECS_COMBAT_COMPONENT_H
