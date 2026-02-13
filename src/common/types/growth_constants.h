/**
 * @file growth_constants.h
 * @brief 原版传奇2成长系统常量定义
 *
 * 包含经验表、组队加成系数、职业成长参数等硬编码常量。
 * 所有数值来源于原版 Pascal 源码。
 */

#ifndef MIR2_COMMON_TYPES_GROWTH_CONSTANTS_H_
#define MIR2_COMMON_TYPES_GROWTH_CONSTANTS_H_

#include <array>
#include <cstdint>

namespace mir2::common {

/// 最大角色等级
constexpr int MAX_LEVEL = 61;

/// 默认初始HP（1级，无职业修正前）
constexpr int DEFHP = 14;

/// 默认初始MP（1级，无职业修正前）
constexpr int DEFMP = 11;

/// 奖励点数起始等级（21级起开始获得奖励点数）
constexpr int ADJ_LEVEL = 20;

/// 组队最大人数
constexpr int GROUP_MAX = 11;

/// 最大学习技能数
constexpr int MAX_USER_MAGIC = 20;

/// 组队经验分享范围（格子数）
constexpr int PARTY_SHARE_RANGE = 12;

/**
 * @brief 硬编码经验表 (0~61)
 *
 * NEED_EXPS[level] 表示从 level 升到 level+1 所需经验值。
 * NEED_EXPS[0] 未使用（占位），NEED_EXPS[1]=100 表示1级升2级需要100经验。
 */
constexpr std::array<int64_t, MAX_LEVEL + 1> NEED_EXPS = {{
    0,              // level 0 (unused)
    100,            // level 1
    200,            // level 2
    300,            // level 3
    400,            // level 4
    600,            // level 5
    900,            // level 6
    1200,           // level 7
    1700,           // level 8
    2500,           // level 9
    6000,           // level 10
    8000,           // level 11
    10000,          // level 12
    15000,          // level 13
    30000,          // level 14
    40000,          // level 15
    50000,          // level 16
    70000,          // level 17
    100000,         // level 18
    120000,         // level 19
    140000,         // level 20
    250000,         // level 21
    300000,         // level 22
    350000,         // level 23
    400000,         // level 24
    500000,         // level 25
    700000,         // level 26
    1000000,        // level 27
    1400000,        // level 28
    1800000,        // level 29
    2000000,        // level 30
    2400000,        // level 31
    2800000,        // level 32
    3200000,        // level 33
    3600000,        // level 34
    4000000,        // level 35
    4800000,        // level 36
    5600000,        // level 37
    8200000,        // level 38
    9000000,        // level 39
    12000000,       // level 40
    16000000,       // level 41
    30000000,       // level 42
    50000000,       // level 43
    80000000,       // level 44
    120000000,      // level 45
    280000000,      // level 46
    360000000,      // level 47
    400000000,      // level 48
    420000000,      // level 49
    430000000,      // level 50
    480000000,      // level 51
    520000000,      // level 52
    550000000,      // level 53
    600000000,      // level 54
    680000000,      // level 55
    750000000,      // level 56
    820000000,      // level 57
    900000000,      // level 58
    1000000000,     // level 59
    1200000000,     // level 60
    1500000000LL,   // level 61 (满级)
}};

/**
 * @brief 组队经验加成系数表
 *
 * PARTY_EXP_BONUS[n-1] 表示 n 人组队时的经验加成倍率。
 * 例如：3人组队加成 = PARTY_EXP_BONUS[2] = 1.3
 */
constexpr std::array<double, GROUP_MAX> PARTY_EXP_BONUS = {{
    1.0,    // 1人（无加成）
    1.2,    // 2人
    1.3,    // 3人
    1.4,    // 4人
    1.5,    // 5人
    1.6,    // 6人
    1.7,    // 7人
    1.8,    // 8人
    1.9,    // 9人
    2.0,    // 10人
    2.1,    // 11人
}};

}  // namespace mir2::common

#endif  // MIR2_COMMON_TYPES_GROWTH_CONSTANTS_H_
