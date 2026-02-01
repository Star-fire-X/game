#include "ecs/systems/damage_calculator.h"

#include "server/combat/combat_core.h"

#include <algorithm>
#include <cmath>

namespace mir2::ecs {

namespace {

int scale_power(int value, int train_level, int skill_level) {
    const int clamped_train = std::max(0, train_level);
    const int clamped_skill = std::max(0, skill_level);
    const double scaled = static_cast<double>(value) / (clamped_train + 1) * (clamped_skill + 1);
    return static_cast<int>(std::round(scaled));
}

} // namespace

int DamageCalculator::get_power(const SkillTemplate& skill, int skill_level) {
    const int scaled = scale_power(skill.min_power, static_cast<int>(skill.train_lv), skill_level);
    return scaled + skill.def_power;
}

int DamageCalculator::get_random_power(const SkillTemplate& skill, int skill_level) {
    static thread_local legend2::combat::CombatRandom random;
    const int min_scaled = scale_power(skill.min_power, static_cast<int>(skill.train_lv), skill_level);
    const int max_scaled = scale_power(skill.max_power, static_cast<int>(skill.train_lv), skill_level);
    const int range = std::max(0, max_scaled - min_scaled);
    const int roll = range > 0 ? random.roll_int(0, range) : 0;
    return min_scaled + roll + skill.def_power;
}

int DamageCalculator::get_power_13(int base_value, int skill_level, int train_level) {
    const int clamped_train = std::max(0, train_level);
    const int clamped_skill = std::max(0, skill_level);
    const double base = static_cast<double>(base_value);
    const double scaled = (base * 2.0 / 3.0) / (clamped_train + 1) * (clamped_skill + 1);
    const double guaranteed = base / 3.0;
    return static_cast<int>(std::round(scaled + guaranteed));
}

int DamageCalculator::get_attack_power(int base_power, int power_range, int luck) {
    static thread_local legend2::combat::CombatRandom random;
    const int clamped_range = std::max(0, power_range);
    const int max_damage = base_power + clamped_range;
    const int roll = random.roll_int(0, 9);

    if (luck >= roll) {
        return max_damage;
    }

    if (clamped_range == 0) {
        return base_power;
    }

    return random.roll_int(base_power, max_damage);
}

int DamageCalculator::calculate_physical_damage(const CharacterAttributesComponent& attacker,
                                                const CharacterAttributesComponent& defender,
                                                const SkillTemplate& skill,
                                                int skill_level) {
    const int base_power = attacker.attack + get_power(skill, skill_level);
    const int scaled_range = scale_power(skill.max_power - skill.min_power,
                                         static_cast<int>(skill.train_lv),
                                         skill_level);
    const int attack_power = get_attack_power(base_power, scaled_range, attacker.luck);
    int damage = std::max(1, attack_power - defender.defense);

    // 应用剑术技能的攻击类型修正
    const mir2::common::AttackType attack_type = legend2::combat::get_attack_type_for_skill(skill.id);
    const legend2::combat::AttackTypeModifier modifier = legend2::combat::get_attack_modifier(attack_type);

    // 应用伤害倍率修正
    damage = legend2::combat::apply_attack_modifier(damage, modifier, 0);

    return damage;
}

int DamageCalculator::calculate_magic_damage(const CharacterAttributesComponent& attacker,
                                             const CharacterAttributesComponent& defender,
                                             const SkillTemplate& skill,
                                             int skill_level,
                                             bool is_undead) {
    const int magic_power = attacker.magic_attack + get_power(skill, skill_level);
    const double multiplier = is_undead ? 1.5 : 1.0;
    const int adjusted_power = static_cast<int>(std::round(magic_power * multiplier));
    return std::max(1, adjusted_power - defender.magic_defense);
}

int DamageCalculator::calculate_healing(const CharacterAttributesComponent& caster,
                                        const SkillTemplate& skill,
                                        int skill_level) {
    const double base_heal = static_cast<double>(get_power(skill, skill_level));
    const double scaled = base_heal * (1.0 + static_cast<double>(caster.sc) / 100.0);
    return static_cast<int>(std::round(scaled));
}

int DamageCalculator::apply_elemental_resistance(int damage, int resistance) {
    if (damage <= 0) {
        return 0;
    }
    // 抗性减免公式：damage * (100 - resistance) / 100
    // 抗性上限100%
    const int clamped_resistance = std::clamp(resistance, 0, 100);
    const int reduced = damage * (100 - clamped_resistance) / 100;
    return std::max(1, reduced);
}

} // namespace mir2::ecs
