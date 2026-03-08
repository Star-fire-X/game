/**
 * @file growth_formulas.h
 * @brief 原版传奇2成长公式
 *
 * 纯函数工具类，实现原版 Pascal 源码中的属性计算公式。
 */

#ifndef MIR2_SERVER_ECS_SYSTEMS_GROWTH_FORMULAS_H_
#define MIR2_SERVER_ECS_SYSTEMS_GROWTH_FORMULAS_H_

#include "common/enums.h"
#include "common/types/growth_constants.h"
#include <cstdint>

namespace mir2::ecs {

/**
 * @brief 裸装属性（不含装备加成）
 */
struct NakedAbility {
    int dc_lo = 0;   ///< 物理攻击下限
    int dc_hi = 0;   ///< 物理攻击上限
    int mc_lo = 0;   ///< 魔法攻击下限
    int mc_hi = 0;   ///< 魔法攻击上限
    int sc_lo = 0;   ///< 精神力下限
    int sc_hi = 0;   ///< 精神力上限
    int ac_lo = 0;   ///< 物理防御下限
    int ac_hi = 0;   ///< 物理防御上限
    int mac_lo = 0;  ///< 魔法防御下限
    int mac_hi = 0;  ///< 魔法防御上限
    int max_hp = 0;  ///< 最大HP
    int max_mp = 0;  ///< 最大MP
    int max_weight = 0;       ///< 最大负重
    int max_wear_weight = 0;  ///< 最大穿戴负重
    int max_hand_weight = 0;  ///< 最大腕力
};

/**
 * @brief 成长公式工具类（全静态方法）
 */
class GrowthFormulas {
public:
    GrowthFormulas() = delete;

    /**
     * @brief 计算指定职业、等级的裸装属性
     *
     * 基于原版 Pascal CalcLevelAbility 函数实现。
     * 使用二次多项式公式计算各项属性。
     */
    static NakedAbility CalcLevelAbility(common::CharacterClass char_class, int level);

    /**
     * @brief 获取升到下一级所需经验值
     * @param level 当前等级
     * @return 所需经验值，若等级超出范围返回 INT64_MAX
     */
    static int64_t GetExpForLevel(int level);

    /**
     * @brief 计算实际获得的经验（含等级差惩罚）
     *
     * 当玩家等级远高于怪物等级时，经验会递减。
     * @param player_lv 玩家等级
     * @param monster_lv 怪物等级
     * @param base_exp 基础经验值
     * @return 实际获得经验（最低为1）
     */
    static int CalcGetExp(int player_lv, int monster_lv, int base_exp);

    /**
     * @brief 获取指定职业到指定等级的累计奖励点数
     *
     * 21级起每级获得奖励点数，不同职业每级获得的点数不同。
     * @param char_class 职业
     * @param level 当前等级
     * @return 累计可用奖励点数
     */
    static int GetBonusPoint(common::CharacterClass char_class, int level);

    /**
     * @brief 获取组队经验加成倍率
     * @param member_count 队伍中有效成员数
     * @return 加成倍率
     */
    static double GetPartyExpBonus(int member_count);
};

}  // namespace mir2::ecs

#endif  // MIR2_SERVER_ECS_SYSTEMS_GROWTH_FORMULAS_H_
