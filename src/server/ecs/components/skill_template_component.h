#ifndef LEGEND2_SERVER_ECS_COMPONENTS_SKILL_TEMPLATE_COMPONENT_H
#define LEGEND2_SERVER_ECS_COMPONENTS_SKILL_TEMPLATE_COMPONENT_H

#include <array>
#include <cstdint>
#include <string>

#include "common/types.h"

namespace mir2::ecs {

/**
 * @brief 技能模板数据
 */
struct SkillTemplate {
    /** @brief 技能ID */
    uint32_t id = 0;
    /** @brief 技能名称 */
    std::string name;
    /** @brief 技能描述 */
    std::string description;
    /** @brief 职业要求 */
    mir2::common::CharacterClass required_class = mir2::common::CharacterClass::WARRIOR;
    /** @brief 等级要求 */
    uint8_t required_level = 0;
    /** @brief 最大技能等级 */
    uint8_t max_level = 3;
    /** @brief 各等级修炼等级要求 */
    std::array<uint8_t, 4> train_level_req{};
    /** @brief 各等级修炼点数要求 */
    std::array<int32_t, 4> train_points_req{};
    /** @brief 技能类型 */
    mir2::common::SkillType skill_type = mir2::common::SkillType::PHYSICAL;
    /** @brief 目标类型 */
    mir2::common::SkillTarget target_type = mir2::common::SkillTarget::SELF;
    /** @brief 是否被动技能 */
    bool is_passive = false;
    /** @brief 魔法消耗 */
    int mp_cost = 0;
    /** @brief 是否消耗符咒 */
    bool consumes_talisman = false;
    /** @brief 符咒消耗数量 */
    int talisman_cost = 0;
    /** @brief 需要的护身符类型 */
    mir2::common::AmuletType required_amulet = mir2::common::AmuletType::NONE;
    /** @brief 护身符耐久消耗 */
    int amulet_cost = 0;
    /** @brief 冷却时间(毫秒) */
    int cooldown_ms = 0;
    /** @brief 施法时间(毫秒) */
    int cast_time_ms = 0;
    /** @brief 是否可被打断 */
    bool can_be_interrupted = true;
    /** @brief 施法范围 */
    float range = 0.0f;
    /** @brief 作用半径 */
    float aoe_radius = 0.0f;
    /** @brief 最小伤害 */
    int min_power = 0;
    /** @brief 最大伤害 */
    int max_power = 0;
    /** @brief 防御力加成 */
    int def_power = 0;
    /** @brief 最大防御力加成 */
    int def_max_power = 0;
    /** @brief 当前修炼等级 */
    uint8_t train_lv = 0;
    /** @brief 持续时间(毫秒) */
    int duration_ms = 0;
    /** @brief 属性修正 */
    int stat_modifier = 0;
    /** @brief 持续伤害 */
    int dot_damage = 0;
    /** @brief 持续伤害间隔(毫秒) */
    int dot_interval_ms = 1000;
    /** @brief 特效类型 */
    uint8_t effect_type = 0;
    /** @brief 特效ID */
    uint8_t effect_id = 0;
    /** @brief 动画ID */
    std::string animation_id;
    /** @brief 音效ID */
    std::string sound_id;
};

} // namespace mir2::ecs

#endif // LEGEND2_SERVER_ECS_COMPONENTS_SKILL_TEMPLATE_COMPONENT_H
