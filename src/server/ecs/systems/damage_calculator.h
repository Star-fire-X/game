#ifndef LEGEND2_SERVER_ECS_DAMAGE_CALCULATOR_H
#define LEGEND2_SERVER_ECS_DAMAGE_CALCULATOR_H

#include "ecs/components/skill_template_component.h"
#include "ecs/components/character_components.h"

namespace mir2::ecs {

class DamageCalculator {
public:
    // GetPower = ROUND(nPower / (TrainLv + 1) * (SkillLevel + 1)) + DefPower
    static int get_power(const SkillTemplate& skill, int skill_level);

    // GetRandomPower: min_power + Random(max_power - min_power)
    static int get_random_power(const SkillTemplate& skill, int skill_level);

    // GetPower13: 1/3 guarantee + 2/3 scaling
    static int get_power_13(int base_value, int skill_level, int train_level);

    // GetAttackPower: luck critical hit
    static int get_attack_power(int base_power, int power_range, int luck);

    // Physical damage calculation
    static int calculate_physical_damage(
        const CharacterAttributesComponent& attacker,
        const CharacterAttributesComponent& defender,
        const SkillTemplate& skill,
        int skill_level);

    // Magic damage calculation
    static int calculate_magic_damage(
        const CharacterAttributesComponent& attacker,
        const CharacterAttributesComponent& defender,
        const SkillTemplate& skill,
        int skill_level,
        bool is_undead = false);

    // Healing calculation
    static int calculate_healing(
        const CharacterAttributesComponent& caster,
        const SkillTemplate& skill,
        int skill_level);

    // Apply elemental resistance reduction
    static int apply_elemental_resistance(int damage, int resistance);
};

} // namespace mir2::ecs
#endif
