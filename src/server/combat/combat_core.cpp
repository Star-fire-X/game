#include "combat/combat_core.h"

#include <algorithm>
#include <chrono>
#include <cmath>

namespace legend2::combat {

mir2::common::AttackType get_attack_type_for_skill(uint32_t skill_id) {
    // 剑术技能ID到AttackType的映射
    switch (skill_id) {
        case 3:  // 攻杀剑术 - 重击+50%伤害
            return mir2::common::AttackType::kHeavyHit;
        case 4:  // 刺杀剑术 - 远距离近战2-3格
            return mir2::common::AttackType::kLongHit;
        case 7:  // 半月弯刀 - 广域攻击3x3
            return mir2::common::AttackType::kWideHit;
        case 12: // 烈火剑法 - 火焰攻击
            return mir2::common::AttackType::kFireHit;
        case 25: // 莲月剑法 - 双倍攻击
            return mir2::common::AttackType::kTwnHit;
        case 26: // 开天斩 - 蓄力攻击+100%伤害
            return mir2::common::AttackType::kPowerHit;
        case 27: // 逐日剑法 - 远程剑气
            return mir2::common::AttackType::kLongHit;
        default:
            return mir2::common::AttackType::kHit;
    }
}

AttackTypeModifier get_attack_modifier(mir2::common::AttackType attack_type) {
    AttackTypeModifier modifier;

    switch (attack_type) {
        case mir2::common::AttackType::kHit:
            modifier.damage_multiplier = 1.0f;
            break;
        case mir2::common::AttackType::kHeavyHit:
            modifier.damage_multiplier = 1.5f;
            break;
        case mir2::common::AttackType::kBigHit:
            modifier.hit_plus_multiplier = 1;
            break;
        case mir2::common::AttackType::kPowerHit:
            modifier.damage_multiplier = 2.0f;
            break;
        case mir2::common::AttackType::kLongHit:
            modifier.range = 3;
            break;
        case mir2::common::AttackType::kTwnHit:
            modifier.hit_count = 2;
            break;
        case mir2::common::AttackType::kWideHit:
            modifier.is_aoe = true;
            modifier.aoe_radius = 1;
            break;
        case mir2::common::AttackType::kFireHit:
            modifier.fire_damage_bonus = 20;
            break;
        default:
            break;
    }

    return modifier;
}

int apply_attack_modifier(int base_damage, const AttackTypeModifier& modifier, int hit_plus) {
    const double scaled_damage = static_cast<double>(base_damage) *
        static_cast<double>(modifier.damage_multiplier);
    int adjusted_damage = static_cast<int>(std::round(scaled_damage));

    if (modifier.hit_plus_multiplier != 0 && hit_plus > 0) {
        adjusted_damage += hit_plus * modifier.hit_plus_multiplier;
    }

    if (modifier.fire_damage_bonus > 0) {
        adjusted_damage += modifier.fire_damage_bonus;
    }

    return std::max(0, adjusted_damage);
}

CombatRandom::CombatRandom()
    : CombatRandom(static_cast<uint32_t>(
        std::chrono::steady_clock::now().time_since_epoch().count())) {}

CombatRandom::CombatRandom(uint32_t seed)
    : rng_(seed)
    , chance_distribution_(0.0f, 1.0f)
    , int_distribution_(0, 0) {}

void CombatRandom::seed(uint32_t seed) {
    rng_.seed(seed);
}

DamageRolls CombatRandom::roll_damage(int base_damage, const CombatConfig& config) {
    DamageRolls rolls;

    if (base_damage > 0) {
        int min_var = (base_damage * config.min_variance_percent) / 100;
        int max_var = (base_damage * config.max_variance_percent) / 100;
        if (min_var < max_var) {
            rolls.variance = roll_int(min_var, max_var);
        }
    }

    rolls.critical_roll = roll_chance();
    rolls.miss_roll = roll_chance();
    return rolls;
}

float CombatRandom::roll_chance() {
    return chance_distribution_(rng_);
}

int CombatRandom::roll_int(int min_value, int max_value) {
    int low = std::min(min_value, max_value);
    int high = std::max(min_value, max_value);
    int_distribution_.param(std::uniform_int_distribution<int>::param_type(low, high));
    return int_distribution_(rng_);
}

DamageResult DamageCalculator::calculate(const DamageInput& input,
                                         const CombatConfig& config,
                                         const DamageRolls& rolls) {
    DamageResult result;

    const float critical_chance = std::clamp(input.critical_chance, 0.0f, 1.0f);
    const float miss_chance = std::clamp(input.miss_chance, 0.0f, 1.0f);

    if (rolls.miss_roll < miss_chance) {
        return DamageResult::miss();
    }

    result.base_damage = input.attack;
    const int raw_damage = input.attack - input.defense;

    if (raw_damage > 0) {
        const int min_var = (raw_damage * config.min_variance_percent) / 100;
        const int max_var = (raw_damage * config.max_variance_percent) / 100;
        if (min_var < max_var) {
            result.variance = std::clamp(rolls.variance, min_var, max_var);
        }
    }

    int damage_with_variance = raw_damage + result.variance;

    result.is_critical = rolls.critical_roll < critical_chance;
    if (result.is_critical) {
        damage_with_variance = static_cast<int>(
            damage_with_variance * config.critical_multiplier);
    }

    result.final_damage = std::max(config.minimum_damage, damage_with_variance);
    return result;
}

int RangeChecker::distance_squared(const mir2::common::Position& a, const mir2::common::Position& b) {
    const int dx = a.x - b.x;
    const int dy = a.y - b.y;
    return dx * dx + dy * dy;
}

bool RangeChecker::is_in_range(const mir2::common::Position& attacker_pos,
                               const mir2::common::Position& target_pos,
                               int range) {
    const int safe_range = std::max(0, range);
    const int range_squared = safe_range * safe_range;
    return distance_squared(attacker_pos, target_pos) <= range_squared;
}

std::vector<LootDrop> LootGenerator::generate(const LootTable& table,
                                              const mir2::common::Position& drop_position,
                                              CombatRandom& random) {
    std::vector<LootDrop> drops;
    if (table.entries.empty()) {
        return drops;
    }

    for (const auto& entry : table.entries) {
        if (entry.item_template_id == 0 || entry.drop_rate <= 0.0f) {
            continue;
        }

        const float rate = std::clamp(entry.drop_rate, 0.0f, 1.0f);
        if (random.roll_chance() >= rate) {
            continue;
        }

        const int min_qty = std::max(1, entry.min_quantity);
        const int max_qty = std::max(min_qty, entry.max_quantity);
        const int quantity = random.roll_int(min_qty, max_qty);

        LootDrop drop;
        drop.item_template_id = entry.item_template_id;
        drop.quantity = quantity;
        drop.position = drop_position;
        drops.push_back(drop);
    }

    return drops;
}

}  // namespace legend2::combat
