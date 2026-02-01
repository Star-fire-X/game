/**
 * @file combat_core.h
 * @brief Combat core algorithms shared by legacy and ECS.
 */

#ifndef LEGEND2_COMMON_COMBAT_COMBAT_CORE_H
#define LEGEND2_COMMON_COMBAT_COMBAT_CORE_H

#include <cstdint>
#include <random>
#include <vector>

#include "common/enums.h"
#include "common/types.h"

namespace legend2 {

// =============================================================================
// 战斗结果结构 (Combat Result Structures)
// =============================================================================

/**
 * @brief 伤害计算结果
 */
struct DamageResult {
    int base_damage = 0;      ///< 基础伤害（防御计算前）
    int final_damage = 0;     ///< 最终伤害（防御计算后）
    int variance = 0;         ///< 随机浮动值
    bool is_critical = false; ///< 是否暴击
    bool is_miss = false;     ///< 是否未命中

    /// 创建未命中结果
    static DamageResult miss() {
        DamageResult result;
        result.is_miss = true;
        return result;
    }
};

/**
 * @brief 攻击动作结果
 */
struct AttackResult {
    bool success = false;                        ///< 是否成功
    mir2::common::ErrorCode error_code = mir2::common::ErrorCode::SUCCESS;   ///< 错误码
    DamageResult damage;                         ///< 伤害结果
    bool target_died = false;                    ///< 目标是否死亡

    /// 创建成功结果
    static AttackResult ok(const DamageResult& dmg, bool died = false) {
        return {true, mir2::common::ErrorCode::SUCCESS, dmg, died};
    }

    /// 创建失败结果
    static AttackResult error(mir2::common::ErrorCode code) {
        return {false, code, {}, false};
    }
};

/**
 * @brief 掉落物品信息
 */
struct LootDrop {
    uint32_t item_template_id = 0;  ///< 物品模板ID
    int quantity = 1;               ///< 数量
    mir2::common::Position position;              ///< 掉落位置
};

/**
 * @brief 死亡事件信息
 */
struct DeathEvent {
    uint32_t entity_id = 0;                    ///< 实体ID
    mir2::common::EntityType entity_type = mir2::common::EntityType::MONSTER;  ///< 实体类型
    mir2::common::Position death_position;                   ///< 死亡位置
    std::vector<LootDrop> loot_drops;          ///< 掉落物品列表
    int experience_reward = 0;                 ///< 经验奖励
    int gold_reward = 0;                       ///< 金币奖励
};

/**
 * @brief 复活信息
 */
struct RespawnInfo {
    uint32_t character_id = 0;      ///< 角色ID
    mir2::common::Position respawn_position;      ///< 复活位置
    uint32_t respawn_map_id = 0;    ///< 复活地图ID
    float hp_percent = 1.0f;        ///< HP恢复百分比
    float mp_percent = 1.0f;        ///< MP恢复百分比
};

// =============================================================================
// 战斗配置 (Combat Configuration)
// =============================================================================

/**
 * @brief 战斗系统配置
 */
struct CombatConfig {
    // 伤害浮动范围（百分比）
    int min_variance_percent = -10;  ///< 最小浮动 -10%
    int max_variance_percent = 10;   ///< 最大浮动 +10%

    // 最小伤害（保证至少造成这么多伤害）
    int minimum_damage = 1;          ///< 最小伤害值

    // 暴击设置
    float base_critical_chance = 0.05f;  ///< 基础暴击率 5%
    float critical_multiplier = 1.5f;    ///< 暴击伤害倍率 150%

    // 未命中设置
    float base_miss_chance = 0.05f;      ///< 基础未命中率 5%

    // 默认近战攻击范围
    int default_melee_range = 1;         ///< 默认近战范围

    // 复活设置
    float default_respawn_hp_percent = 1.0f;   ///< 默认复活HP百分比
    float default_respawn_mp_percent = 1.0f;   ///< 默认复活MP百分比
    mir2::common::Position default_respawn_position = {100, 100};  ///< 默认复活位置
    uint32_t default_respawn_map_id = 1;       ///< 默认复活地图ID
};

}  // namespace legend2

namespace legend2::combat {

// =============================================================================
// 核心战斗算法 (Combat Core Algorithms)
// =============================================================================

/**
 * @brief 伤害输入参数
 */
struct DamageInput {
    int attack = 0;             ///< 攻击力
    int defense = 0;            ///< 防御力
    float critical_chance = 0;  ///< 暴击率
    float miss_chance = 0;      ///< 未命中率
};

/**
 * @brief 攻击类型修正参数
 */
struct AttackTypeModifier {
    float damage_multiplier = 1.0f;   ///< 伤害倍率
    int range = 1;                     ///< 攻击范围
    int hit_count = 1;                 ///< 攻击次数
    bool is_aoe = false;               ///< 是否范围攻击
    int aoe_radius = 0;                ///< 范围攻击半径
    int fire_damage_bonus = 0;         ///< 火焰附加伤害
    int hit_plus_multiplier = 0;       ///< hit_plus加成倍率
};

/**
 * @brief 获取攻击类型修正参数
 */
AttackTypeModifier get_attack_modifier(mir2::common::AttackType attack_type);

/**
 * @brief 根据技能ID获取对应的攻击类型
 * @param skill_id 技能ID
 * @return 对应的攻击类型，如果不是剑术技能则返回kHit
 */
mir2::common::AttackType get_attack_type_for_skill(uint32_t skill_id);

/**
 * @brief 应用攻击类型修正计算最终伤害
 */
int apply_attack_modifier(int base_damage, const AttackTypeModifier& modifier,
                          int hit_plus = 0);

/**
 * @brief 伤害随机结果
 */
struct DamageRolls {
    int variance = 0;           ///< 伤害浮动值
    float critical_roll = 0.0f; ///< 暴击随机值
    float miss_roll = 0.0f;     ///< 未命中随机值
};

/**
 * @brief 战斗随机数生成器（缓存分布以提升性能）
 */
class CombatRandom {
 public:
    CombatRandom();
    explicit CombatRandom(uint32_t seed);

    void seed(uint32_t seed);

    DamageRolls roll_damage(int base_damage, const CombatConfig& config);
    float roll_chance();
    int roll_int(int min_value, int max_value);

 private:
    std::mt19937 rng_;
    std::uniform_real_distribution<float> chance_distribution_;
    std::uniform_int_distribution<int> int_distribution_;
};

/**
 * @brief 伤害计算器（纯函数）
 */
class DamageCalculator {
 public:
    static DamageResult calculate(const DamageInput& input,
                                  const CombatConfig& config,
                                  const DamageRolls& rolls);
};

/**
 * @brief 范围检测工具
 */
class RangeChecker {
 public:
    static int distance_squared(const mir2::common::Position& a, const mir2::common::Position& b);
    static bool is_in_range(const mir2::common::Position& attacker_pos,
                            const mir2::common::Position& target_pos,
                            int range);
};

/**
 * @brief 掉落表条目
 */
struct LootEntry {
    uint32_t item_template_id = 0;  ///< 物品模板ID
    int min_quantity = 1;           ///< 最小数量
    int max_quantity = 1;           ///< 最大数量
    float drop_rate = 0.0f;         ///< 掉落概率 [0,1]
};

/**
 * @brief 掉落表
 */
struct LootTable {
    std::vector<LootEntry> entries;
};

/**
 * @brief 掉落生成器
 */
class LootGenerator {
 public:
    static std::vector<LootDrop> generate(const LootTable& table,
                                          const mir2::common::Position& drop_position,
                                          CombatRandom& random);
};

}  // namespace legend2::combat

#endif  // LEGEND2_COMMON_COMBAT_COMBAT_CORE_H
