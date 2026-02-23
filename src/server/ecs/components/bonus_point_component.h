/**
 * @file bonus_point_component.h
 * @brief 奖励点数分配组件
 *
 * 角色21级起每级获得奖励点数，可自由分配到各属性。
 */

#ifndef MIR2_SERVER_ECS_COMPONENTS_BONUS_POINT_COMPONENT_H_
#define MIR2_SERVER_ECS_COMPONENTS_BONUS_POINT_COMPONENT_H_

namespace mir2::ecs {

/**
 * @brief 已分配的奖励属性
 */
struct BonusAbility {
    int dc = 0;     ///< 物理攻击
    int mc = 0;     ///< 魔法攻击
    int sc = 0;     ///< 精神力
    int ac = 0;     ///< 物理防御
    int mac = 0;    ///< 魔法防御
    int hp = 0;     ///< 体力（影响HP）
    int mp = 0;     ///< 魔力（影响MP）
    int hit = 0;    ///< 命中
    int speed = 0;  ///< 速度

    int total_spent() const {
        return dc + mc + sc + ac + mac + hp + mp + hit + speed;
    }
};

/**
 * @brief 奖励点数组件
 *
 * 21级起每级获得职业对应的奖励点数。
 * 点数可分配到 DC/MC/SC/AC/MAC/HP/MP/HIT/SPEED 属性。
 */
struct BonusPointComponent {
    int total_available = 0;     ///< 累计可用总点数
    BonusAbility allocated;      ///< 已分配的点数

    int remaining() const {
        return total_available - allocated.total_spent();
    }
};

}  // namespace mir2::ecs

#endif  // MIR2_SERVER_ECS_COMPONENTS_BONUS_POINT_COMPONENT_H_
