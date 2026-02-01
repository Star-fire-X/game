/**
 * @file monster_component.h
 * @brief 怪物ECS组件定义
 *
 * 包含怪物AI、仇恨系统、技能管理相关的组件
 */

#ifndef MIR2_ECS_COMPONENTS_MONSTER_COMPONENT_H
#define MIR2_ECS_COMPONENTS_MONSTER_COMPONENT_H

#include <cstdint>
#include <unordered_map>
#include <vector>

#include <entt/entt.hpp>

#include "common/types.h"
#include "game/entity/monster.h"

namespace mir2::ecs {

/**
 * @brief 怪物AI类型
 */
enum class MonsterAIType : uint8_t {
    kNormal = 0,      // 普通AI（现有5状态机）
    kAmbush = 1,      // 伏击AI - 隐藏等待玩家接近
    kRanged = 2,      // 远程AI - 保持距离攻击
    kSummoner = 3,    // 召唤AI - 召唤小怪
    kExplosive = 4,   // 自爆AI - 死亡时爆炸
    kPoisonous = 5,   // 毒素AI - 攻击附带毒素
    kGuard = 6,       // 守卫AI - 城堡防御逻辑
    kBossCowKing = 7  // 牛魔王BOSS - 瞬移+疯狂模式
};

/**
 * @brief 怪物标识组件
 *
 * 保存怪物模板与刷新点信息，供掉落等系统使用。
 */
struct MonsterIdentityComponent {
    uint32_t monster_template_id = 0;   ///< 怪物模板ID
    uint32_t spawn_point_id = 0;        ///< 关联刷新点ID
};

/**
 * @brief 怪物技能信息
 */
struct MonsterSkillInfo {
    uint32_t skill_id = 0;      ///< 技能ID
    float cooldown = 0.0f;      ///< 冷却时间（秒）
    int32_t range = 0;          ///< 施法范围
};

/**
 * @brief 怪物AI组件
 *
 * 管理怪物的AI状态机和行为逻辑
 */
struct MonsterAIComponent {
    MonsterAIType ai_type = MonsterAIType::kNormal;            ///< AI类型
    game::entity::MonsterState current_state = game::entity::MonsterState::kIdle;  ///< 当前AI状态
    entt::entity target_entity = entt::null;    ///< 目标实体（null表示无目标）
    float state_timer = 0.0f;                   ///< 当前状态持续时间（秒）
    float last_attack_time = 0.0f;              ///< 上次攻击时间（用于攻击冷却）
    float attack_cooldown = 1.0f;               ///< 攻击冷却时间（秒）
    float attack_cooldown_timer = 0.0f;         ///< 攻击冷却计时器
    bool is_hidden = false;                     ///< 伏击AI隐藏状态
    float preferred_distance = 0.0f;            ///< 远程AI首选距离
    uint32_t patrol_waypoint_index = 0;         ///< 巡逻路点索引
    mir2::common::Position return_position = {0, 0}; ///< 返回位置（通常是出生点）

    // BOSS特有字段
    bool is_crazy_mode = false;                 ///< 疯狂模式（牛魔王）
    float crazy_mode_timer = 0.0f;              ///< 疯狂模式持续时间
    float teleport_cooldown = 0.0f;             ///< 瞬移冷却时间（牛魔王）
};

/**
 * @brief 怪物仇恨组件
 *
 * 管理怪物的仇恨检测和仇恨值系统
 */
struct MonsterAggroComponent {
    int32_t aggro_range = 12;                   ///< 仇恨检测范围
    int32_t attack_range = 3;                   ///< 攻击范围
    std::unordered_map<entt::entity, int32_t> hate_list;  ///< 仇恨值表（实体 -> 仇恨值）
    mutable entt::entity cached_top_target_ = entt::null; ///< 缓存最高仇恨目标（只读方法可更新）
    float hate_decay_rate = 5.0f;               ///< 仇恨衰减速率（每秒）
    float accumulated_decay = 0.0f;            ///< 累积仇恨衰减，用于保留小数部分
    float hate_clear_time = 30.0f;              ///< 仇恨清除超时时间（秒）

    /**
     * @brief 增加仇恨值
     * @param attacker 攻击者实体
     * @param damage 伤害值
     */
    void AddHatred(entt::entity attacker, int32_t damage) {
        if (attacker == entt::null) return;
        // 仇恨值计算公式：伤害 * 1.5
        int32_t hatred = static_cast<int32_t>(damage * 1.5f);
        auto& total_hatred = hate_list[attacker];
        total_hatred += hatred;
        // 更新最高仇恨缓存以减少后续遍历
        if (cached_top_target_ == entt::null) {
            cached_top_target_ = attacker;
            return;
        }
        auto cached_it = hate_list.find(cached_top_target_);
        int32_t cached_hatred = (cached_it != hate_list.end()) ? cached_it->second : 0;
        if (total_hatred > cached_hatred) {
            cached_top_target_ = attacker;
        }
    }

    /**
     * @brief 根据仇恨值获取目标
     * @return 仇恨值最高的敌人，若为空返回entt::null
     */
    entt::entity GetTargetByHatred() const {
        if (hate_list.empty()) return entt::null;
        // 优先返回缓存目标以提升查询性能
        if (cached_top_target_ != entt::null) {
            auto cached_it = hate_list.find(cached_top_target_);
            if (cached_it != hate_list.end()) {
                return cached_top_target_;
            }
        }

        entt::entity target = entt::null;
        int32_t max_hatred = 0;
        for (const auto& [entity, hatred] : hate_list) {
            if (hatred > max_hatred) {
                max_hatred = hatred;
                target = entity;
            }
        }
        // 重新刷新缓存，避免下次全量遍历
        cached_top_target_ = target;
        return target;
    }

    /**
     * @brief 衰减仇恨值
     * @param dt 时间增量（秒）
     */
    void DecayHatred(float dt) {
        if (hate_list.empty()) {
            cached_top_target_ = entt::null;
            accumulated_decay = 0.0f;
            return;
        }
        if (dt <= 0.0f) {
            return;
        }
        accumulated_decay += hate_decay_rate * dt;
        if (accumulated_decay < 1.0f) {
            return;
        }
        const int32_t decay_amount = static_cast<int32_t>(accumulated_decay);
        if (decay_amount <= 0) {
            return;
        }
        accumulated_decay -= static_cast<float>(decay_amount);

        std::vector<entt::entity> to_remove;
        for (auto& [entity, hatred] : hate_list) {
            hatred -= decay_amount;
            if (hatred <= 0) {
                to_remove.push_back(entity);
            }
        }
        for (auto entity : to_remove) {
            hate_list.erase(entity);
        }
        // 若缓存目标被清理，重置缓存以便重新计算
        if (cached_top_target_ != entt::null && hate_list.find(cached_top_target_) == hate_list.end()) {
            cached_top_target_ = entt::null;
        }
    }

    /**
     * @brief 清除所有仇恨记录
     */
    void Clear() {
        hate_list.clear();
        // 同步清空缓存避免返回过期目标
        cached_top_target_ = entt::null;
        accumulated_decay = 0.0f;
    }
};

/**
 * @brief 怪物技能组件
 *
 * 管理怪物的技能列表和冷却时间
 */
struct MonsterSkillComponent {
    std::vector<MonsterSkillInfo> skills;                       ///< 技能列表
    std::unordered_map<uint32_t, float> last_cast_time;        ///< 技能ID -> 上次施法时间
};

}  // namespace mir2::ecs

#endif  // MIR2_ECS_COMPONENTS_MONSTER_COMPONENT_H
