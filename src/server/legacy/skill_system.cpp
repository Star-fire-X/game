/**
 * @file skill_system.cpp
 * @brief Legend2 技能系统实现
 * 
 * 本文件实现技能系统的核心功能，包括：
 * - 技能模板管理和JSON序列化
 * - MP消耗验证和冷却管理
 * - 技能施放（伤害、治疗、增益、减益）
 * - 持续效果管理和过期处理
 * - 默认技能定义（战士、法师、道士）
 */

#include "skill_system.h"
#include "legacy/character.h"
#include <algorithm>
#include <cmath>
#include <chrono>

namespace legend2 {

// =============================================================================
// JSON 序列化实现
// =============================================================================

/**
 * @brief 将 SkillTemplate 序列化为 JSON
 * @param j 输出的 JSON 对象
 * @param skill 技能模板
 */
void to_json(nlohmann::json& j, const SkillTemplate& skill) {
    j = nlohmann::json{
        {"id", skill.id},
        {"name", skill.name},
        {"description", skill.description},
        {"required_class", static_cast<uint8_t>(skill.required_class)},
        {"required_level", skill.required_level},
        {"type", static_cast<uint8_t>(skill.type)},
        {"target_type", static_cast<uint8_t>(skill.target_type)},
        {"mp_cost", skill.mp_cost},
        {"cooldown", skill.cooldown},
        {"cast_time", skill.cast_time},
        {"range", skill.range},
        {"aoe_radius", skill.aoe_radius},
        {"base_damage", skill.base_damage},
        {"damage_scaling", skill.damage_scaling},
        {"uses_magic_attack", skill.uses_magic_attack},
        {"duration_ms", skill.duration_ms},
        {"stat_modifier", skill.stat_modifier},
        {"animation_id", skill.animation_id},
        {"effect_id", skill.effect_id},
        {"sound_id", skill.sound_id}
    };
}

/**
 * @brief 从 JSON 反序列化 SkillTemplate
 * @param j 输入的 JSON 对象
 * @param skill 输出的技能模板
 */
void from_json(const nlohmann::json& j, SkillTemplate& skill) {
    j.at("id").get_to(skill.id);
    j.at("name").get_to(skill.name);
    if (j.contains("description")) j.at("description").get_to(skill.description);
    skill.required_class = static_cast<mir2::common::CharacterClass>(j.at("required_class").get<uint8_t>());
    j.at("required_level").get_to(skill.required_level);
    skill.type = static_cast<mir2::common::SkillType>(j.at("type").get<uint8_t>());
    skill.target_type = static_cast<mir2::common::SkillTarget>(j.at("target_type").get<uint8_t>());
    j.at("mp_cost").get_to(skill.mp_cost);
    j.at("cooldown").get_to(skill.cooldown);
    if (j.contains("cast_time")) j.at("cast_time").get_to(skill.cast_time);
    j.at("range").get_to(skill.range);
    if (j.contains("aoe_radius")) j.at("aoe_radius").get_to(skill.aoe_radius);
    j.at("base_damage").get_to(skill.base_damage);
    if (j.contains("damage_scaling")) j.at("damage_scaling").get_to(skill.damage_scaling);
    if (j.contains("uses_magic_attack")) j.at("uses_magic_attack").get_to(skill.uses_magic_attack);
    if (j.contains("duration_ms")) j.at("duration_ms").get_to(skill.duration_ms);
    if (j.contains("stat_modifier")) j.at("stat_modifier").get_to(skill.stat_modifier);
    if (j.contains("animation_id")) j.at("animation_id").get_to(skill.animation_id);
    if (j.contains("effect_id")) j.at("effect_id").get_to(skill.effect_id);
    if (j.contains("sound_id")) j.at("sound_id").get_to(skill.sound_id);
}

// =============================================================================
// SkillSystem 类实现
// =============================================================================

/**
 * @brief 构造函数 - 初始化时间提供器
 */
SkillSystem::SkillSystem() {
    // Default time provider using system clock
    time_provider_ = []() {
        auto now = std::chrono::steady_clock::now();
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()).count();
    };
}

/**
 * @brief 注册技能模板
 * @param skill 技能模板
 */
void SkillSystem::register_skill(const SkillTemplate& skill) {
    skills_[skill.id] = skill;
}

/**
 * @brief 获取技能模板
 * @param skill_id 技能 ID
 * @return const SkillTemplate* 技能指针，不存在返回 nullptr
 */
const SkillTemplate* SkillSystem::get_skill(uint32_t skill_id) const {
    auto it = skills_.find(skill_id);
    if (it != skills_.end()) {
        return &it->second;
    }
    return nullptr;
}

/**
 * @brief 获取职业可用技能列表
 * @param char_class 职业类型
 * @return 技能模板列表
 */
std::vector<SkillTemplate> SkillSystem::get_class_skills(mir2::common::CharacterClass char_class) const {
    std::vector<SkillTemplate> result;
    for (const auto& [id, skill] : skills_) {
        if (skill.required_class == char_class) {
            result.push_back(skill);
        }
    }
    return result;
}

std::vector<SkillTemplate> SkillSystem::get_all_skills() const {
    std::vector<SkillTemplate> result;
    result.reserve(skills_.size());
    for (const auto& [id, skill] : skills_) {
        result.push_back(skill);
    }
    return result;
}

void SkillSystem::clear_skills() {
    skills_.clear();
}

// -----------------------------------------------------------------------------
// MP 验证方法
// -----------------------------------------------------------------------------

/**
 * @brief 检查角色是否有足够 MP
 * @param caster 施法者
 * @param skill_id 技能 ID
 * @return true 如果 MP 足够
 */
bool SkillSystem::has_enough_mp(const Character& caster, uint32_t skill_id) const {
    return has_enough_mp(caster.get_mp(), skill_id);
}

/**
 * @brief 检查 MP 是否足够（使用当前 MP 值）
 * @param current_mp 当前 MP
 * @param skill_id 技能 ID
 * @return true 如果 MP 足够
 */
bool SkillSystem::has_enough_mp(int current_mp, uint32_t skill_id) const {
    const SkillTemplate* skill = get_skill(skill_id);
    if (!skill) {
        return false;
    }
    return current_mp >= skill->mp_cost;
}

// -----------------------------------------------------------------------------
// 冷却管理方法
// -----------------------------------------------------------------------------

/**
 * @brief 检查技能是否就绪
 * @param character_id 角色 ID
 * @param skill_id 技能 ID
 * @return true 如果技能不在冷却中
 */
bool SkillSystem::is_skill_ready(uint32_t character_id, uint32_t skill_id) const {
    auto char_it = cooldowns_.find(character_id);
    if (char_it == cooldowns_.end()) {
        return true; // No cooldowns recorded = ready
    }
    
    auto skill_it = char_it->second.find(skill_id);
    if (skill_it == char_it->second.end()) {
        return true; // No cooldown for this skill = ready
    }
    
    return skill_it->second.is_ready(get_current_time_ms());
}

/**
 * @brief 获取剩余冷却时间
 * @param character_id 角色 ID
 * @param skill_id 技能 ID
 * @return 剩余毫秒数，无冷却返回 0
 */
int64_t SkillSystem::get_remaining_cooldown_ms(uint32_t character_id, uint32_t skill_id) const {
    auto char_it = cooldowns_.find(character_id);
    if (char_it == cooldowns_.end()) {
        return 0;
    }
    
    auto skill_it = char_it->second.find(skill_id);
    if (skill_it == char_it->second.end()) {
        return 0;
    }
    
    return skill_it->second.remaining_ms(get_current_time_ms());
}

/**
 * @brief 开始技能冷却
 * @param character_id 角色 ID
 * @param skill_id 技能 ID
 */
void SkillSystem::start_cooldown(uint32_t character_id, uint32_t skill_id) {
    const SkillTemplate* skill = get_skill(skill_id);
    if (!skill) {
        return;
    }
    
    int64_t cooldown_ms = static_cast<int64_t>(skill->cooldown * 1000.0f);
    int64_t current_time = get_current_time_ms();
    
    SkillCooldown cd;
    cd.skill_id = skill_id;
    cd.cooldown_end_ms = current_time + cooldown_ms;
    
    cooldowns_[character_id][skill_id] = cd;
}

/**
 * @brief 重置技能冷却
 * @param character_id 角色 ID
 * @param skill_id 技能 ID
 */
void SkillSystem::reset_cooldown(uint32_t character_id, uint32_t skill_id) {
    auto char_it = cooldowns_.find(character_id);
    if (char_it != cooldowns_.end()) {
        char_it->second.erase(skill_id);
    }
}

void SkillSystem::reset_all_cooldowns(uint32_t character_id) {
    cooldowns_.erase(character_id);
}

void SkillSystem::clear_all_cooldowns() {
    cooldowns_.clear();
}

// -----------------------------------------------------------------------------
// 技能施放方法
// -----------------------------------------------------------------------------

/**
 * @brief 验证技能施放条件
 * @param caster 施法者
 * @param skill_id 技能 ID
 * @param skill 技能模板
 * @return mir2::common::ErrorCode 验证结果
 */
mir2::common::ErrorCode SkillSystem::validate_cast(
    const Character& caster,
    uint32_t skill_id,
    const SkillTemplate& skill) const
{
    // Check if caster is alive
    if (caster.is_dead()) {
        return mir2::common::ErrorCode::CHARACTER_DEAD;
    }
    
    // Check level requirement
    if (caster.get_level() < skill.required_level) {
        return mir2::common::ErrorCode::LEVEL_REQUIREMENT_NOT_MET;
    }
    
    // Check class requirement
    if (skill.required_class != caster.get_class()) {
        return mir2::common::ErrorCode::CLASS_REQUIREMENT_NOT_MET;
    }
    
    // Check MP
    if (!has_enough_mp(caster, skill_id)) {
        return mir2::common::ErrorCode::INSUFFICIENT_MP;
    }
    
    // Check cooldown
    if (!is_skill_ready(caster.get_id(), skill_id)) {
        return mir2::common::ErrorCode::SKILL_ON_COOLDOWN;
    }
    
    return mir2::common::ErrorCode::SUCCESS;
}

/**
 * @brief 检查是否在技能范围内
 * @param caster_pos 施法者位置
 * @param target_pos 目标位置
 * @param range 技能范围
 * @return true 如果在范围内
 */
bool SkillSystem::is_in_range(
    const mir2::common::Position& caster_pos,
    const mir2::common::Position& target_pos,
    float range) const
{
    int dx = caster_pos.x - target_pos.x;
    int dy = caster_pos.y - target_pos.y;
    float dist_squared = static_cast<float>(dx * dx + dy * dy);
    return dist_squared <= range * range;
}

/**
 * @brief 计算技能伤害
 * @param caster 施法者
 * @param skill 技能模板
 * @param target_stats 目标属性
 * @return 最终伤害值
 */
int SkillSystem::calculate_skill_damage(
    const Character& caster,
    const SkillTemplate& skill,
    const mir2::common::CharacterStats& target_stats) const
{
    // Get base stat for scaling
    int attack_stat = skill.uses_magic_attack ? 
        caster.get_magic_attack() : caster.get_attack();
    
    // Calculate scaled damage
    int scaled_damage = skill.base_damage + 
        static_cast<int>(attack_stat * skill.damage_scaling);
    
    // Apply defense reduction
    int defense = skill.uses_magic_attack ? 
        target_stats.magic_defense : target_stats.defense;
    
    int final_damage = scaled_damage - defense;
    
    // Minimum damage of 1
    return std::max(1, final_damage);
}

/**
 * @brief 计算技能治疗量
 * @param caster 施法者
 * @param skill 技能模板
 * @return 治疗量
 */
int SkillSystem::calculate_skill_healing(
    const Character& caster,
    const SkillTemplate& skill) const
{
    // Healing scales with magic attack
    int healing = skill.base_damage + 
        static_cast<int>(caster.get_magic_attack() * skill.damage_scaling);
    
    return std::max(1, healing);
}

SkillCastResult SkillSystem::apply_damage_skill(
    Character& caster,
    const SkillTemplate& skill,
    Character& target)
{
    // Check range
    if (!is_in_range(caster.get_position(), target.get_position(), skill.range)) {
        return SkillCastResult::error(mir2::common::ErrorCode::TARGET_OUT_OF_RANGE);
    }
    
    // Check if target is alive
    if (target.is_dead()) {
        return SkillCastResult::error(mir2::common::ErrorCode::TARGET_NOT_FOUND);
    }
    
    // Calculate and apply damage
    int damage = calculate_skill_damage(caster, skill, target.get_stats());
    target.take_damage(damage);
    
    // Consume MP and start cooldown
    caster.consume_mp(skill.mp_cost);
    start_cooldown(caster.get_id(), skill.id);
    
    return SkillCastResult::ok_damage(damage, skill.mp_cost, target.is_dead());
}

SkillCastResult SkillSystem::apply_damage_skill_to_monster(
    Character& caster,
    const SkillTemplate& skill,
    Monster& target)
{
    // Check range
    if (!is_in_range(caster.get_position(), target.position, skill.range)) {
        return SkillCastResult::error(mir2::common::ErrorCode::TARGET_OUT_OF_RANGE);
    }
    
    // Check if target is alive
    if (target.is_dead()) {
        return SkillCastResult::error(mir2::common::ErrorCode::TARGET_NOT_FOUND);
    }
    
    // Calculate and apply damage
    int damage = calculate_skill_damage(caster, skill, target.stats);
    target.take_damage(damage);
    
    // Consume MP and start cooldown
    caster.consume_mp(skill.mp_cost);
    start_cooldown(caster.get_id(), skill.id);
    
    return SkillCastResult::ok_damage(damage, skill.mp_cost, target.is_dead());
}

SkillCastResult SkillSystem::apply_heal_skill(
    Character& caster,
    const SkillTemplate& skill,
    Character& target)
{
    // Check range
    if (!is_in_range(caster.get_position(), target.get_position(), skill.range)) {
        return SkillCastResult::error(mir2::common::ErrorCode::TARGET_OUT_OF_RANGE);
    }
    
    // Check if target is alive (can't heal dead)
    if (target.is_dead()) {
        return SkillCastResult::error(mir2::common::ErrorCode::TARGET_NOT_FOUND);
    }
    
    // Calculate and apply healing
    int healing = calculate_skill_healing(caster, skill);
    int actual_healing = target.heal(healing);
    
    // Consume MP and start cooldown
    caster.consume_mp(skill.mp_cost);
    start_cooldown(caster.get_id(), skill.id);
    
    return SkillCastResult::ok_heal(actual_healing, skill.mp_cost);
}

SkillCastResult SkillSystem::apply_buff_skill(
    Character& caster,
    const SkillTemplate& skill,
    Character& target)
{
    // Check range
    if (!is_in_range(caster.get_position(), target.get_position(), skill.range)) {
        return SkillCastResult::error(mir2::common::ErrorCode::TARGET_OUT_OF_RANGE);
    }
    
    // Check if target is alive
    if (target.is_dead()) {
        return SkillCastResult::error(mir2::common::ErrorCode::TARGET_NOT_FOUND);
    }
    
    // Create the buff effect
    int64_t current_time = get_current_time_ms();
    ActiveEffect effect;
    effect.skill_id = skill.id;
    effect.source_id = caster.get_id();
    effect.target_id = target.get_id();
    effect.effect_type = mir2::common::SkillType::BUFF;
    effect.stat_modifier = skill.stat_modifier;
    effect.start_time_ms = current_time;
    effect.end_time_ms = current_time + skill.duration_ms;
    effect.is_active = true;
    
    // Apply the effect
    apply_effect(target.get_id(), effect);
    
    // Apply stat bonus to target
    mir2::common::CharacterStats bonus;
    bonus.attack = skill.stat_modifier;  // Simplified: buff adds to attack
    target.add_stats(bonus);
    
    // Consume MP and start cooldown
    caster.consume_mp(skill.mp_cost);
    start_cooldown(caster.get_id(), skill.id);
    
    return SkillCastResult::ok_effect(effect, skill.mp_cost);
}

SkillCastResult SkillSystem::apply_debuff_skill(
    Character& caster,
    const SkillTemplate& skill,
    Character& target)
{
    // Check range
    if (!is_in_range(caster.get_position(), target.get_position(), skill.range)) {
        return SkillCastResult::error(mir2::common::ErrorCode::TARGET_OUT_OF_RANGE);
    }
    
    // Check if target is alive
    if (target.is_dead()) {
        return SkillCastResult::error(mir2::common::ErrorCode::TARGET_NOT_FOUND);
    }
    
    // Create the debuff effect
    int64_t current_time = get_current_time_ms();
    ActiveEffect effect;
    effect.skill_id = skill.id;
    effect.source_id = caster.get_id();
    effect.target_id = target.get_id();
    effect.effect_type = mir2::common::SkillType::DEBUFF;
    effect.stat_modifier = skill.stat_modifier;
    effect.start_time_ms = current_time;
    effect.end_time_ms = current_time + skill.duration_ms;
    effect.is_active = true;
    
    // Apply the effect
    apply_effect(target.get_id(), effect);
    
    // Apply stat penalty to target
    mir2::common::CharacterStats penalty;
    penalty.defense = -skill.stat_modifier;  // Simplified: debuff reduces defense
    target.add_stats(penalty);
    
    // Consume MP and start cooldown
    caster.consume_mp(skill.mp_cost);
    start_cooldown(caster.get_id(), skill.id);
    
    return SkillCastResult::ok_effect(effect, skill.mp_cost);
}

SkillCastResult SkillSystem::cast_skill(
    Character& caster,
    uint32_t skill_id,
    Character* target,
    const mir2::common::Position* /* target_pos */)  // Reserved for AOE targeting
{
    // Get skill template
    const SkillTemplate* skill = get_skill(skill_id);
    if (!skill) {
        return SkillCastResult::error(mir2::common::ErrorCode::SKILL_NOT_LEARNED, "Skill not found");
    }
    
    // Validate cast prerequisites
    mir2::common::ErrorCode validation = validate_cast(caster, skill_id, *skill);
    if (validation != mir2::common::ErrorCode::SUCCESS) {
        return SkillCastResult::error(validation);
    }
    
    // Handle different skill types
    switch (skill->type) {
        case mir2::common::SkillType::PHYSICAL:
        case mir2::common::SkillType::MAGICAL:
            if (!target) {
                return SkillCastResult::error(mir2::common::ErrorCode::TARGET_NOT_FOUND, "No target specified");
            }
            return apply_damage_skill(caster, *skill, *target);
            
        case mir2::common::SkillType::HEAL:
            // If no target, heal self
            if (!target) {
                return apply_heal_skill(caster, *skill, caster);
            }
            return apply_heal_skill(caster, *skill, *target);
            
        case mir2::common::SkillType::BUFF:
            // If no target, buff self
            if (!target) {
                return apply_buff_skill(caster, *skill, caster);
            }
            return apply_buff_skill(caster, *skill, *target);
            
        case mir2::common::SkillType::DEBUFF:
            if (!target) {
                return SkillCastResult::error(mir2::common::ErrorCode::TARGET_NOT_FOUND, "No target specified");
            }
            return apply_debuff_skill(caster, *skill, *target);
            
        default:
            return SkillCastResult::error(mir2::common::ErrorCode::INVALID_ACTION, "Unknown skill type");
    }
}

SkillCastResult SkillSystem::cast_skill_on_monster(
    Character& caster,
    uint32_t skill_id,
    Monster& target)
{
    // Get skill template
    const SkillTemplate* skill = get_skill(skill_id);
    if (!skill) {
        return SkillCastResult::error(mir2::common::ErrorCode::SKILL_NOT_LEARNED, "Skill not found");
    }
    
    // Validate cast prerequisites
    mir2::common::ErrorCode validation = validate_cast(caster, skill_id, *skill);
    if (validation != mir2::common::ErrorCode::SUCCESS) {
        return SkillCastResult::error(validation);
    }
    
    // Only damage skills can target monsters
    if (skill->type != mir2::common::SkillType::PHYSICAL && skill->type != mir2::common::SkillType::MAGICAL) {
        return SkillCastResult::error(mir2::common::ErrorCode::INVALID_ACTION, "Cannot use this skill on monsters");
    }
    
    return apply_damage_skill_to_monster(caster, *skill, target);
}

// -----------------------------------------------------------------------------
// 效果管理方法
// -----------------------------------------------------------------------------

/**
 * @brief 应用效果到目标
 * @param target_id 目标 ID
 * @param effect 效果对象
 */
void SkillSystem::apply_effect(uint32_t target_id, const ActiveEffect& effect) {
    // Remove any existing effect from the same skill
    remove_effect(target_id, effect.skill_id);
    
    // Add the new effect
    active_effects_[target_id].push_back(effect);
}

/**
 * @brief 移除效果
 * @param target_id 目标 ID
 * @param skill_id 技能 ID
 */
void SkillSystem::remove_effect(uint32_t target_id, uint32_t skill_id) {
    auto it = active_effects_.find(target_id);
    if (it == active_effects_.end()) {
        return;
    }
    
    auto& effects = it->second;
    effects.erase(
        std::remove_if(effects.begin(), effects.end(),
            [skill_id](const ActiveEffect& e) { return e.skill_id == skill_id; }),
        effects.end());
    
    // Clean up empty entries
    if (effects.empty()) {
        active_effects_.erase(it);
    }
}

/**
 * @brief 获取目标的活跃效果
 * @param target_id 目标 ID
 * @return 活跃效果列表
 */
std::vector<ActiveEffect> SkillSystem::get_active_effects(uint32_t target_id) const {
    auto it = active_effects_.find(target_id);
    if (it == active_effects_.end()) {
        return {};
    }
    return it->second;
}

/**
 * @brief 更新所有效果
 * @param current_time_ms 当前时间戳
 * 
 * 检查并移除过期效果，触发过期回调。
 */
void SkillSystem::update_effects(int64_t current_time_ms) {
    std::vector<std::pair<uint32_t, ActiveEffect>> expired_effects;
    
    // Find all expired effects
    for (auto& [target_id, effects] : active_effects_) {
        for (auto& effect : effects) {
            if (effect.is_active && effect.is_expired(current_time_ms)) {
                effect.is_active = false;
                expired_effects.push_back({target_id, effect});
            }
        }
    }
    
    // Notify about expired effects and remove them
    for (const auto& [target_id, effect] : expired_effects) {
        if (effect_expired_callback_) {
            effect_expired_callback_(target_id, effect);
        }
        remove_effect(target_id, effect.skill_id);
    }
}

void SkillSystem::clear_all_effects() {
    active_effects_.clear();
}

// -----------------------------------------------------------------------------
// 时间管理方法
// -----------------------------------------------------------------------------

/**
 * @brief 获取当前时间戳
 * @return 毫秒级时间戳
 */
int64_t SkillSystem::get_current_time_ms() const {
    return time_provider_();
}

/**
 * @brief 设置时间提供器
 * @param provider 时间提供函数
 */
void SkillSystem::set_time_provider(std::function<int64_t()> provider) {
    time_provider_ = std::move(provider);
}

/**
 * @brief 设置效果过期回调
 * @param callback 回调函数
 */
void SkillSystem::set_effect_expired_callback(EffectExpiredCallback callback) {
    effect_expired_callback_ = std::move(callback);
}

// -----------------------------------------------------------------------------
// 默认技能注册
// -----------------------------------------------------------------------------

/**
 * @brief 注册所有默认技能
 * 
 * 注册战士、法师、道士的默认技能。
 */
void SkillSystem::register_default_skills() {
    // Register warrior skills
    for (const auto& skill : get_default_warrior_skills()) {
        register_skill(skill);
    }
    
    // Register mage skills
    for (const auto& skill : get_default_mage_skills()) {
        register_skill(skill);
    }
    
    // Register taoist skills
    for (const auto& skill : get_default_taoist_skills()) {
        register_skill(skill);
    }
}

// =============================================================================
// 默认技能定义
// =============================================================================

/**
 * @brief 获取战士默认技能
 * @return 战士技能列表
 * 
 * 包含：基础攻击、烈火剑法、战吼
 */
std::vector<SkillTemplate> get_default_warrior_skills() {
    std::vector<SkillTemplate> skills;
    
    // Skill 1: Basic Strike (基础攻击)
    SkillTemplate basic_strike;
    basic_strike.id = 1001;
    basic_strike.name = "Basic Strike";
    basic_strike.description = "A basic melee attack";
    basic_strike.required_class = mir2::common::CharacterClass::WARRIOR;
    basic_strike.required_level = 1;
    basic_strike.type = mir2::common::SkillType::PHYSICAL;
    basic_strike.target_type = mir2::common::SkillTarget::SINGLE_ENEMY;
    basic_strike.mp_cost = 5;
    basic_strike.cooldown = 1.0f;
    basic_strike.range = 1.5f;
    basic_strike.base_damage = 10;
    basic_strike.damage_scaling = 1.2f;
    basic_strike.uses_magic_attack = false;
    skills.push_back(basic_strike);
    
    // Skill 2: Power Slash (烈火剑法)
    SkillTemplate power_slash;
    power_slash.id = 1002;
    power_slash.name = "Power Slash";
    power_slash.description = "A powerful slashing attack";
    power_slash.required_class = mir2::common::CharacterClass::WARRIOR;
    power_slash.required_level = 7;
    power_slash.type = mir2::common::SkillType::PHYSICAL;
    power_slash.target_type = mir2::common::SkillTarget::SINGLE_ENEMY;
    power_slash.mp_cost = 15;
    power_slash.cooldown = 3.0f;
    power_slash.range = 2.0f;
    power_slash.base_damage = 30;
    power_slash.damage_scaling = 1.5f;
    power_slash.uses_magic_attack = false;
    skills.push_back(power_slash);
    
    // Skill 3: War Cry (战吼) - Buff
    SkillTemplate war_cry;
    war_cry.id = 1003;
    war_cry.name = "War Cry";
    war_cry.description = "Increases attack power temporarily";
    war_cry.required_class = mir2::common::CharacterClass::WARRIOR;
    war_cry.required_level = 14;
    war_cry.type = mir2::common::SkillType::BUFF;
    war_cry.target_type = mir2::common::SkillTarget::SELF;
    war_cry.mp_cost = 20;
    war_cry.cooldown = 30.0f;
    war_cry.range = 0.0f;
    war_cry.base_damage = 0;
    war_cry.duration_ms = 60000;  // 60 seconds
    war_cry.stat_modifier = 10;   // +10 attack
    skills.push_back(war_cry);
    
    return skills;
}

/**
 * @brief 获取法师默认技能
 * @return 法师技能列表
 * 
 * 包含：火球术、雷电术、冰咆哮、诅咒术
 */
std::vector<SkillTemplate> get_default_mage_skills() {
    std::vector<SkillTemplate> skills;
    
    // Skill 1: Fire Ball (火球术)
    SkillTemplate fire_ball;
    fire_ball.id = 2001;
    fire_ball.name = "Fire Ball";
    fire_ball.description = "Launches a ball of fire at the enemy";
    fire_ball.required_class = mir2::common::CharacterClass::MAGE;
    fire_ball.required_level = 1;
    fire_ball.type = mir2::common::SkillType::MAGICAL;
    fire_ball.target_type = mir2::common::SkillTarget::SINGLE_ENEMY;
    fire_ball.mp_cost = 8;
    fire_ball.cooldown = 1.5f;
    fire_ball.range = 6.0f;
    fire_ball.base_damage = 15;
    fire_ball.damage_scaling = 1.3f;
    fire_ball.uses_magic_attack = true;
    skills.push_back(fire_ball);
    
    // Skill 2: Lightning Bolt (雷电术)
    SkillTemplate lightning;
    lightning.id = 2002;
    lightning.name = "Lightning Bolt";
    lightning.description = "Strikes the enemy with lightning";
    lightning.required_class = mir2::common::CharacterClass::MAGE;
    lightning.required_level = 11;
    lightning.type = mir2::common::SkillType::MAGICAL;
    lightning.target_type = mir2::common::SkillTarget::SINGLE_ENEMY;
    lightning.mp_cost = 25;
    lightning.cooldown = 4.0f;
    lightning.range = 8.0f;
    lightning.base_damage = 50;
    lightning.damage_scaling = 1.8f;
    lightning.uses_magic_attack = true;
    skills.push_back(lightning);
    
    // Skill 3: Ice Storm (冰咆哮) - AOE
    SkillTemplate ice_storm;
    ice_storm.id = 2003;
    ice_storm.name = "Ice Storm";
    ice_storm.description = "Creates a storm of ice in an area";
    ice_storm.required_class = mir2::common::CharacterClass::MAGE;
    ice_storm.required_level = 21;
    ice_storm.type = mir2::common::SkillType::MAGICAL;
    ice_storm.target_type = mir2::common::SkillTarget::AOE_ENEMY;
    ice_storm.mp_cost = 40;
    ice_storm.cooldown = 8.0f;
    ice_storm.range = 7.0f;
    ice_storm.aoe_radius = 3.0f;
    ice_storm.base_damage = 35;
    ice_storm.damage_scaling = 1.5f;
    ice_storm.uses_magic_attack = true;
    skills.push_back(ice_storm);
    
    // Skill 4: Curse (诅咒术) - Debuff
    SkillTemplate curse;
    curse.id = 2004;
    curse.name = "Curse";
    curse.description = "Reduces enemy defense";
    curse.required_class = mir2::common::CharacterClass::MAGE;
    curse.required_level = 17;
    curse.type = mir2::common::SkillType::DEBUFF;
    curse.target_type = mir2::common::SkillTarget::SINGLE_ENEMY;
    curse.mp_cost = 18;
    curse.cooldown = 15.0f;
    curse.range = 6.0f;
    curse.base_damage = 0;
    curse.duration_ms = 30000;  // 30 seconds
    curse.stat_modifier = 8;    // -8 defense
    skills.push_back(curse);
    
    return skills;
}

/**
 * @brief 获取道士默认技能
 * @return 道士技能列表
 * 
 * 包含：灵魂火符、治愈术、群体治愈术、神圣战甲术、施毒术
 */
std::vector<SkillTemplate> get_default_taoist_skills() {
    std::vector<SkillTemplate> skills;
    
    // Skill 1: Soul Fire (灵魂火符)
    SkillTemplate soul_fire;
    soul_fire.id = 3001;
    soul_fire.name = "Soul Fire";
    soul_fire.description = "Launches a spiritual fire attack";
    soul_fire.required_class = mir2::common::CharacterClass::TAOIST;
    soul_fire.required_level = 1;
    soul_fire.type = mir2::common::SkillType::MAGICAL;
    soul_fire.target_type = mir2::common::SkillTarget::SINGLE_ENEMY;
    soul_fire.mp_cost = 6;
    soul_fire.cooldown = 1.2f;
    soul_fire.range = 5.0f;
    soul_fire.base_damage = 12;
    soul_fire.damage_scaling = 1.1f;
    soul_fire.uses_magic_attack = true;
    skills.push_back(soul_fire);
    
    // Skill 2: Healing Light (治愈术)
    SkillTemplate healing;
    healing.id = 3002;
    healing.name = "Healing Light";
    healing.description = "Restores HP to the target";
    healing.required_class = mir2::common::CharacterClass::TAOIST;
    healing.required_level = 7;
    healing.type = mir2::common::SkillType::HEAL;
    healing.target_type = mir2::common::SkillTarget::SINGLE_ALLY;
    healing.mp_cost = 12;
    healing.cooldown = 2.0f;
    healing.range = 6.0f;
    healing.base_damage = 30;  // Used as base healing
    healing.damage_scaling = 0.8f;
    healing.uses_magic_attack = true;
    skills.push_back(healing);
    
    // Skill 3: Group Heal (群体治愈术)
    SkillTemplate group_heal;
    group_heal.id = 3003;
    group_heal.name = "Group Heal";
    group_heal.description = "Heals all nearby allies";
    group_heal.required_class = mir2::common::CharacterClass::TAOIST;
    group_heal.required_level = 21;
    group_heal.type = mir2::common::SkillType::HEAL;
    group_heal.target_type = mir2::common::SkillTarget::AOE_ALLY;
    group_heal.mp_cost = 35;
    group_heal.cooldown = 10.0f;
    group_heal.range = 0.0f;  // Centered on caster
    group_heal.aoe_radius = 5.0f;
    group_heal.base_damage = 25;
    group_heal.damage_scaling = 0.6f;
    group_heal.uses_magic_attack = true;
    skills.push_back(group_heal);
    
    // Skill 4: Divine Shield (神圣战甲术) - Buff
    SkillTemplate divine_shield;
    divine_shield.id = 3004;
    divine_shield.name = "Divine Shield";
    divine_shield.description = "Increases defense temporarily";
    divine_shield.required_class = mir2::common::CharacterClass::TAOIST;
    divine_shield.required_level = 14;
    divine_shield.type = mir2::common::SkillType::BUFF;
    divine_shield.target_type = mir2::common::SkillTarget::SINGLE_ALLY;
    divine_shield.mp_cost = 15;
    divine_shield.cooldown = 20.0f;
    divine_shield.range = 6.0f;
    divine_shield.base_damage = 0;
    divine_shield.duration_ms = 45000;  // 45 seconds
    divine_shield.stat_modifier = 8;    // +8 defense (stored, applied differently)
    skills.push_back(divine_shield);
    
    // Skill 5: Poison Cloud (施毒术) - Debuff
    SkillTemplate poison;
    poison.id = 3005;
    poison.name = "Poison Cloud";
    poison.description = "Poisons the enemy, reducing their stats";
    poison.required_class = mir2::common::CharacterClass::TAOIST;
    poison.required_level = 11;
    poison.type = mir2::common::SkillType::DEBUFF;
    poison.target_type = mir2::common::SkillTarget::SINGLE_ENEMY;
    poison.mp_cost = 10;
    poison.cooldown = 8.0f;
    poison.range = 5.0f;
    poison.base_damage = 0;
    poison.duration_ms = 20000;  // 20 seconds
    poison.stat_modifier = 5;    // -5 defense
    skills.push_back(poison);
    
    return skills;
}

} // namespace legend2
