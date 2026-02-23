#ifndef MIR2_ECS_EFFECT_COMPONENT_H_
#define MIR2_ECS_EFFECT_COMPONENT_H_

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <vector>

#include <entt/entt.hpp>

namespace mir2::ecs {

static_assert(sizeof(entt::entity) >= sizeof(uint32_t),
              "entt::entity size must not be narrower than uint32_t");

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

constexpr std::size_t kEffectCategoryCount =
    static_cast<std::size_t>(EffectCategory::DEFENSE_BOOST) + 1;

constexpr std::size_t ToEffectCategoryIndex(EffectCategory category) {
    return static_cast<std::size_t>(category);
}

/**
 * @brief Runtime data for an active effect instance.
 */
struct ActiveEffect {
    uint32_t skill_id = 0;
    entt::entity source_entity = entt::null;
    EffectCategory category = EffectCategory::STAT_BUFF;
    int value = 0;
    int magic_attack_bonus = 0;
    int sc_bonus = 0;
    int speed_bonus = 0;
    int max_hp_bonus = 0;
    int max_mp_bonus = 0;
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
    mutable std::array<uint32_t, kEffectCategoryCount> category_counts{};
    mutable bool category_counts_dirty = true;
    mutable int64_t next_expire_time_ms = 0;
    mutable bool next_expire_time_dirty = true;
    int applied_attack_bonus = 0;
    int applied_defense_penalty = 0;
    int applied_magic_attack_bonus = 0;
    int applied_sc_bonus = 0;
    int applied_speed_bonus = 0;
    int applied_max_hp_bonus = 0;
    int applied_max_mp_bonus = 0;
    int applied_frenzy_attack_bonus = 0;    // 疯狂攻击加成
    int applied_frenzy_defense_penalty = 0; // 疯狂防御减少

    /**
     * @brief Add a new effect entry.
     */
    void add_effect(const ActiveEffect& effect) {
        effects.push_back(effect);
        mark_effects_dirty();
    }

    /**
     * @brief Remove all effects that match the provided skill id.
     */
    void remove_effects_by_skill(uint32_t skill_id) {
        const std::size_t original_size = effects.size();
        effects.erase(
            std::remove_if(
                effects.begin(),
                effects.end(),
                [skill_id](const ActiveEffect& effect) {
                    return effect.skill_id == skill_id;
                }),
            effects.end());
        if (effects.size() != original_size) {
            mark_effects_dirty();
        }
    }

    /**
     * @brief Remove all effects whose end time has passed.
     */
    void remove_expired(int64_t now_ms) {
        const std::size_t original_size = effects.size();
        effects.erase(
            std::remove_if(
                effects.begin(),
                effects.end(),
                [now_ms](const ActiveEffect& effect) {
                    return effect.end_time_ms > 0 && effect.end_time_ms <= now_ms;
                }),
            effects.end());
        if (effects.size() != original_size) {
            mark_effects_dirty();
        }
    }

    /**
     * @brief Get mutable pointers to effects in the given category.
     */
    [[deprecated("Use for_each_effect_by_category to avoid per-call allocations.")]]
    std::vector<ActiveEffect*> get_effects_by_category(EffectCategory cat) {
        std::vector<ActiveEffect*> matches;
        matches.reserve(effects.size());
        for (auto& effect : effects) {
            if (effect.category == cat) {
                matches.push_back(&effect);
            }
        }
        return matches;
    }

    template <typename Callback>
    void for_each_effect_by_category(EffectCategory cat, Callback&& callback) {
        for (auto& effect : effects) {
            if (effect.category == cat) {
                callback(effect);
            }
        }
    }

    template <typename Callback>
    void for_each_effect_by_category(EffectCategory cat, Callback&& callback) const {
        for (const auto& effect : effects) {
            if (effect.category == cat) {
                callback(effect);
            }
        }
    }

    void mark_effects_dirty() {
        category_counts_dirty = true;
        next_expire_time_dirty = true;
    }

    bool has_category(EffectCategory category) const {
        ensure_category_counts();
        return category_counts[ToEffectCategoryIndex(category)] > 0;
    }

    bool has_any_category(std::initializer_list<EffectCategory> categories) const {
        ensure_category_counts();
        for (EffectCategory category : categories) {
            if (category_counts[ToEffectCategoryIndex(category)] > 0) {
                return true;
            }
        }
        return false;
    }

    bool has_expired(int64_t now_ms) const {
        ensure_next_expire_time();
        return next_expire_time_ms > 0 && next_expire_time_ms <= now_ms;
    }

private:
    void ensure_category_counts() const {
        if (!category_counts_dirty) {
            return;
        }
        category_counts.fill(0);
        for (const auto& effect : effects) {
            ++category_counts[ToEffectCategoryIndex(effect.category)];
        }
        category_counts_dirty = false;
    }

    void ensure_next_expire_time() const {
        if (!next_expire_time_dirty) {
            return;
        }
        int64_t nearest = 0;
        for (const auto& effect : effects) {
            if (effect.end_time_ms <= 0) {
                continue;
            }
            if (nearest == 0 || effect.end_time_ms < nearest) {
                nearest = effect.end_time_ms;
            }
        }
        next_expire_time_ms = nearest;
        next_expire_time_dirty = false;
    }
};

} // namespace mir2::ecs

#endif // MIR2_ECS_EFFECT_COMPONENT_H_
