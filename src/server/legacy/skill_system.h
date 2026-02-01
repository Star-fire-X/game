/**
 * @file skill_system.h
 * @brief Legend2 技能系统
 * 
 * 本文件包含技能系统的定义，包括：
 * - 技能模板定义
 * - MP消耗和冷却管理
 * - 技能效果（伤害、治疗、增益、减益）
 * - 持续效果管理
 */

#ifndef LEGEND2_SKILL_SYSTEM_H
#define LEGEND2_SKILL_SYSTEM_H

#include "common/types.h"
#include "monster.h"
#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>
#include <chrono>
#include <optional>
#include <functional>

namespace legend2 {

class Character;

// =============================================================================
// 技能模板数据结构 (Skill Template Data Structure)
// =============================================================================

/**
 * @brief 技能模板定义（静态数据）
 */
struct SkillTemplate {
    uint32_t id = 0;                    ///< 技能ID
    std::string name;                   ///< 技能名称
    std::string description;            ///< 技能描述
    mir2::common::CharacterClass required_class = mir2::common::CharacterClass::WARRIOR;  ///< 职业要求
    int required_level = 1;             ///< 等级要求
    mir2::common::SkillType type = mir2::common::SkillType::PHYSICAL;  ///< 技能类型
    mir2::common::SkillTarget target_type = mir2::common::SkillTarget::SINGLE_ENEMY;  ///< 目标类型
    
    // 资源消耗
    int mp_cost = 10;                   ///< MP消耗
    
    // 时间相关
    float cooldown = 1.0f;              ///< 冷却时间（秒）
    float cast_time = 0.0f;             ///< 施法时间（秒，0为瞬发）
    
    // 范围和区域
    float range = 1.0f;                 ///< 最大施法距离（瓦片）
    float aoe_radius = 0.0f;            ///< AOE半径（0为单体）
    
    // 伤害/治疗
    int base_damage = 0;                ///< 基础伤害/治疗量
    float damage_scaling = 1.0f;        ///< 攻击力/魔攻缩放系数
    bool uses_magic_attack = false;     ///< true=魔攻，false=物攻
    
    // 增益/减益
    int duration_ms = 0;                ///< 效果持续时间（毫秒，0为瞬时）
    int stat_modifier = 0;              ///< 属性修改量
    
    // 动画/视觉
    uint32_t animation_id = 0;          ///< 动画ID
    uint32_t effect_id = 0;             ///< 特效ID
    uint32_t sound_id = 0;              ///< 音效ID
    
    /// 是否为瞬发技能
    bool is_instant() const { return cast_time <= 0.0f; }
    
    /// 是否为AOE技能
    bool is_aoe() const { return aoe_radius > 0.0f; }
    
    /// 是否造成伤害
    bool deals_damage() const { 
        return type == mir2::common::SkillType::PHYSICAL || type == mir2::common::SkillType::MAGICAL; 
    }
    
    /// 是否为治疗技能
    bool is_heal() const { return type == mir2::common::SkillType::HEAL; }
    
    /// 是否为增益技能
    bool is_buff() const { return type == mir2::common::SkillType::BUFF; }
    
    /// 是否为减益技能
    bool is_debuff() const { return type == mir2::common::SkillType::DEBUFF; }
};

// JSON serialization for SkillTemplate
void to_json(nlohmann::json& j, const SkillTemplate& skill);
void from_json(const nlohmann::json& j, SkillTemplate& skill);

// =============================================================================
// 活跃增益/减益效果 (Active Buff/Debuff Effect)
// =============================================================================

/**
 * @brief 角色身上的活跃增益或减益效果
 */
struct ActiveEffect {
    uint32_t skill_id = 0;              ///< 技能ID
    uint32_t source_id = 0;             ///< 施加者ID
    uint32_t target_id = 0;             ///< 目标ID
    mir2::common::SkillType effect_type = mir2::common::SkillType::BUFF;  ///< 效果类型
    int stat_modifier = 0;              ///< 属性修改量
    int64_t start_time_ms = 0;          ///< 效果开始时间
    int64_t end_time_ms = 0;            ///< 效果结束时间
    bool is_active = true;              ///< 是否激活
    
    /// 检查效果是否已过期
    bool is_expired(int64_t current_time_ms) const {
        return current_time_ms >= end_time_ms;
    }
    
    /// 获取剩余持续时间（毫秒）
    int64_t remaining_duration_ms(int64_t current_time_ms) const {
        return std::max(int64_t(0), end_time_ms - current_time_ms);
    }
};

// =============================================================================
// 技能冷却追踪 (Skill Cooldown Tracking)
// =============================================================================

/**
 * @brief 单个技能的冷却状态
 */
struct SkillCooldown {
    uint32_t skill_id = 0;          ///< 技能ID
    int64_t cooldown_end_ms = 0;    ///< 冷却结束时间
    
    /// 检查冷却是否已结束
    bool is_ready(int64_t current_time_ms) const {
        return current_time_ms >= cooldown_end_ms;
    }
    
    /// 获取剩余冷却时间（毫秒）
    int64_t remaining_ms(int64_t current_time_ms) const {
        return std::max(int64_t(0), cooldown_end_ms - current_time_ms);
    }
};

// =============================================================================
// 技能施放结果 (Skill Cast Result)
// =============================================================================

/**
 * @brief 尝试施放技能的结果
 */
struct SkillCastResult {
    bool success = false;                        ///< 是否成功
    mir2::common::ErrorCode error_code = mir2::common::ErrorCode::SUCCESS;   ///< 错误码
    std::string error_message;                   ///< 错误信息
    
    // 伤害/治疗结果（如果适用）
    int damage_dealt = 0;                        ///< 造成的伤害
    int healing_done = 0;                        ///< 治疗量
    bool target_died = false;                    ///< 目标是否死亡
    
    // 施加的效果（如果是增益/减益）
    std::optional<ActiveEffect> applied_effect;  ///< 施加的效果
    
    // 消耗的MP
    int mp_consumed = 0;                         ///< 消耗的MP
    
    static SkillCastResult ok() {
        return {true, mir2::common::ErrorCode::SUCCESS, "", 0, 0, false, std::nullopt, 0};
    }
    
    static SkillCastResult ok_damage(int damage, int mp, bool died = false) {
        SkillCastResult result;
        result.success = true;
        result.damage_dealt = damage;
        result.mp_consumed = mp;
        result.target_died = died;
        return result;
    }
    
    static SkillCastResult ok_heal(int healing, int mp) {
        SkillCastResult result;
        result.success = true;
        result.healing_done = healing;
        result.mp_consumed = mp;
        return result;
    }
    
    static SkillCastResult ok_effect(const ActiveEffect& effect, int mp) {
        SkillCastResult result;
        result.success = true;
        result.applied_effect = effect;
        result.mp_consumed = mp;
        return result;
    }
    
    static SkillCastResult error(mir2::common::ErrorCode code, const std::string& msg = "") {
        return {false, code, msg, 0, 0, false, std::nullopt, 0};
    }
};

// =============================================================================
// Skill System Interface
// =============================================================================

/// Interface for skill system
class ISkillSystem {
public:
    virtual ~ISkillSystem() = default;
    
    // --- Skill Template Management ---
    virtual void register_skill(const SkillTemplate& skill) = 0;
    virtual const SkillTemplate* get_skill(uint32_t skill_id) const = 0;
    virtual std::vector<SkillTemplate> get_class_skills(mir2::common::CharacterClass char_class) const = 0;
    
    // --- MP Validation ---
    virtual bool has_enough_mp(const Character& caster, uint32_t skill_id) const = 0;
    virtual bool has_enough_mp(int current_mp, uint32_t skill_id) const = 0;
    
    // --- Cooldown Management ---
    virtual bool is_skill_ready(uint32_t character_id, uint32_t skill_id) const = 0;
    virtual int64_t get_remaining_cooldown_ms(uint32_t character_id, uint32_t skill_id) const = 0;
    virtual void start_cooldown(uint32_t character_id, uint32_t skill_id) = 0;
    virtual void reset_cooldown(uint32_t character_id, uint32_t skill_id) = 0;
    virtual void reset_all_cooldowns(uint32_t character_id) = 0;
    
    // --- Skill Casting ---
    virtual SkillCastResult cast_skill(
        Character& caster,
        uint32_t skill_id,
        Character* target = nullptr,
        const mir2::common::Position* target_pos = nullptr) = 0;
    
    virtual SkillCastResult cast_skill_on_monster(
        Character& caster,
        uint32_t skill_id,
        Monster& target) = 0;
    
    // --- Effect Management ---
    virtual void apply_effect(uint32_t target_id, const ActiveEffect& effect) = 0;
    virtual void remove_effect(uint32_t target_id, uint32_t skill_id) = 0;
    virtual std::vector<ActiveEffect> get_active_effects(uint32_t target_id) const = 0;
    virtual void update_effects(int64_t current_time_ms) = 0;
    
    // --- Time Management ---
    virtual int64_t get_current_time_ms() const = 0;
};

// =============================================================================
// Skill System Implementation
// =============================================================================

/// Default skill system implementation
class SkillSystem : public ISkillSystem {
public:
    SkillSystem();
    
    // --- ISkillSystem Implementation ---
    
    // Skill Template Management
    void register_skill(const SkillTemplate& skill) override;
    const SkillTemplate* get_skill(uint32_t skill_id) const override;
    std::vector<SkillTemplate> get_class_skills(mir2::common::CharacterClass char_class) const override;
    
    // MP Validation
    bool has_enough_mp(const Character& caster, uint32_t skill_id) const override;
    bool has_enough_mp(int current_mp, uint32_t skill_id) const override;
    
    // Cooldown Management
    bool is_skill_ready(uint32_t character_id, uint32_t skill_id) const override;
    int64_t get_remaining_cooldown_ms(uint32_t character_id, uint32_t skill_id) const override;
    void start_cooldown(uint32_t character_id, uint32_t skill_id) override;
    void reset_cooldown(uint32_t character_id, uint32_t skill_id) override;
    void reset_all_cooldowns(uint32_t character_id) override;
    
    // Skill Casting
    SkillCastResult cast_skill(
        Character& caster,
        uint32_t skill_id,
        Character* target = nullptr,
        const mir2::common::Position* target_pos = nullptr) override;
    
    SkillCastResult cast_skill_on_monster(
        Character& caster,
        uint32_t skill_id,
        Monster& target) override;
    
    // Effect Management
    void apply_effect(uint32_t target_id, const ActiveEffect& effect) override;
    void remove_effect(uint32_t target_id, uint32_t skill_id) override;
    std::vector<ActiveEffect> get_active_effects(uint32_t target_id) const override;
    void update_effects(int64_t current_time_ms) override;
    
    // Time Management
    int64_t get_current_time_ms() const override;
    
    // --- Additional Methods ---
    
    /// Set a custom time provider (for testing)
    void set_time_provider(std::function<int64_t()> provider);
    
    /// Get all registered skills
    std::vector<SkillTemplate> get_all_skills() const;
    
    /// Clear all registered skills
    void clear_skills();
    
    /// Clear all cooldowns
    void clear_all_cooldowns();
    
    /// Clear all effects
    void clear_all_effects();
    
    /// Register default skills for all classes
    void register_default_skills();
    
    // --- Callbacks ---
    using EffectExpiredCallback = std::function<void(uint32_t target_id, const ActiveEffect& effect)>;
    void set_effect_expired_callback(EffectExpiredCallback callback);
    
private:
    // Skill templates indexed by ID
    std::unordered_map<uint32_t, SkillTemplate> skills_;
    
    // Cooldowns: character_id -> (skill_id -> cooldown)
    std::unordered_map<uint32_t, std::unordered_map<uint32_t, SkillCooldown>> cooldowns_;
    
    // Active effects: target_id -> list of effects
    std::unordered_map<uint32_t, std::vector<ActiveEffect>> active_effects_;
    
    // Time provider (can be overridden for testing)
    std::function<int64_t()> time_provider_;
    
    // Callbacks
    EffectExpiredCallback effect_expired_callback_;
    
    // --- Helper Methods ---
    
    /// Validate skill cast prerequisites
    mir2::common::ErrorCode validate_cast(
        const Character& caster,
        uint32_t skill_id,
        const SkillTemplate& skill) const;
    
    /// Calculate skill damage
    int calculate_skill_damage(
        const Character& caster,
        const SkillTemplate& skill,
        const mir2::common::CharacterStats& target_stats) const;
    
    /// Calculate skill healing
    int calculate_skill_healing(
        const Character& caster,
        const SkillTemplate& skill) const;
    
    /// Apply damage skill effect
    SkillCastResult apply_damage_skill(
        Character& caster,
        const SkillTemplate& skill,
        Character& target);
    
    /// Apply damage skill to monster
    SkillCastResult apply_damage_skill_to_monster(
        Character& caster,
        const SkillTemplate& skill,
        Monster& target);
    
    /// Apply healing skill effect
    SkillCastResult apply_heal_skill(
        Character& caster,
        const SkillTemplate& skill,
        Character& target);
    
    /// Apply buff skill effect
    SkillCastResult apply_buff_skill(
        Character& caster,
        const SkillTemplate& skill,
        Character& target);
    
    /// Apply debuff skill effect
    SkillCastResult apply_debuff_skill(
        Character& caster,
        const SkillTemplate& skill,
        Character& target);
    
    /// Check if target is in range
    bool is_in_range(
        const mir2::common::Position& caster_pos,
        const mir2::common::Position& target_pos,
        float range) const;
};

// =============================================================================
// 默认技能定义 (Default Skill Definitions)
// =============================================================================

/// 获取默认战士技能
std::vector<SkillTemplate> get_default_warrior_skills();

/// 获取默认法师技能
std::vector<SkillTemplate> get_default_mage_skills();

/// 获取默认道士技能
std::vector<SkillTemplate> get_default_taoist_skills();

} // namespace legend2

#endif // LEGEND2_SKILL_SYSTEM_H
