#ifndef LEGEND2_ECS_EFFECT_COMPONENT_H
#define LEGEND2_ECS_EFFECT_COMPONENT_H

#include <algorithm>
#include <cstdint>
#include <vector>

namespace mir2::ecs {

/**
 * @brief Categories of timed or persistent effects.
 */
enum class EffectCategory {
    STAT_BUFF,
    STAT_DEBUFF,
    DAMAGE_OVER_TIME,
    HEAL_OVER_TIME,
    SHIELD,
    INVISIBLE,
    STUN,
    SLOW,
    HOLY_SEIZE,      // 困魔咒定身效果
    POISON,          // 中毒效果
    FRENZY,          // 疯狂状态
    PARALYSIS,       // 麻痹效果
    SPEED_BOOST,     // 速度提升
    DEFENSE_BOOST,   // 防御提升
};

/**
 * @brief Runtime data for an active effect instance.
 */
struct ActiveEffect {
    uint32_t skill_id = 0;
    uint32_t source_entity = 0;
    EffectCategory category = EffectCategory::STAT_BUFF;
    int value = 0;
    int64_t start_time_ms = 0;
    int64_t end_time_ms = 0;
    int64_t last_tick_ms = 0;
    int tick_interval_ms = 1000;
    int shield_remaining = 0;
    int poison_percent = 0;           // 中毒伤害百分比
    float attack_multiplier = 1.0f;   // 攻击力倍率（疯狂用）
    float defense_multiplier = 1.0f;  // 防御力倍率（疯狂用）
};

/**
 * @brief Component storing all effects applied to an entity.
 */
struct EffectListComponent {
    std::vector<ActiveEffect> effects;
    int applied_attack_bonus = 0;
    int applied_defense_penalty = 0;
    int applied_frenzy_attack_bonus = 0;    // 疯狂攻击加成
    int applied_frenzy_defense_penalty = 0; // 疯狂防御减少

    /**
     * @brief Add a new effect entry.
     */
    void add_effect(const ActiveEffect& effect) {
        effects.push_back(effect);
    }

    /**
     * @brief Remove all effects that match the provided skill id.
     */
    void remove_effects_by_skill(uint32_t skill_id) {
        effects.erase(
            std::remove_if(
                effects.begin(),
                effects.end(),
                [skill_id](const ActiveEffect& effect) {
                    return effect.skill_id == skill_id;
                }),
            effects.end());
    }

    /**
     * @brief Remove all effects whose end time has passed.
     */
    void remove_expired(int64_t now_ms) {
        effects.erase(
            std::remove_if(
                effects.begin(),
                effects.end(),
                [now_ms](const ActiveEffect& effect) {
                    return effect.end_time_ms > 0 && effect.end_time_ms <= now_ms;
                }),
            effects.end());
    }

    /**
     * @brief Get mutable pointers to effects in the given category.
     */
    std::vector<ActiveEffect*> get_effects_by_category(EffectCategory cat) {
        std::vector<ActiveEffect*> matches;
        for (auto& effect : effects) {
            if (effect.category == cat) {
                matches.push_back(&effect);
            }
        }
        return matches;
    }
};

} // namespace mir2::ecs

#endif // LEGEND2_ECS_EFFECT_COMPONENT_H
