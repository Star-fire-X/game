#include "ecs/systems/passive_skill_system.h"

#include "common/skill_ids.h"
#include "ecs/skill_registry.h"

#include <algorithm>

namespace {

namespace PassiveBonus {
// 基础剑术(ONESWORD): 每级命中率加成
constexpr int ONESWORD_HIT_PER_LEVEL = 1;
// 护体神功(BODYGUARD): 每级防御加成
constexpr int BODYGUARD_DEF_PER_LEVEL = 5;
// 嗜血术(BLOODLUST): 每级攻击加成
constexpr int BLOODLUST_ATK_PER_LEVEL = 3;
// 无极真气: 每级攻击/防御加成
constexpr int WUJI_ATK_PER_LEVEL = 2;
constexpr int WUJI_DEF_PER_LEVEL = 2;
// 幽灵盾: 每级魔防加成
constexpr int GHOSTSHIELD_MDEF_PER_LEVEL = 3;
// 神圣战甲: 每级防御加成
constexpr int DEJIWONHO_DEF_PER_LEVEL = 4;
}  // namespace PassiveBonus

}  // namespace

namespace mir2::ecs {

PassiveSkillSystem::PassiveSkillSystem(entt::registry& registry)
    : registry_(registry) {}

void PassiveSkillSystem::recalculate_passives(entt::entity entity) {
    if (!registry_.valid(entity)) {
        return;
    }

    AttributeModifiers totals{};
    const auto* skill_list = registry_.try_get<SkillListComponent>(entity);
    if (skill_list) {
        for (const auto& slot : skill_list->skills) {
            if (!slot) {
                continue;
            }

            const auto* skill = SkillRegistry::instance().get_skill(slot->skill_id);
            if (!skill || !skill->is_passive) {
                continue;
            }

            const AttributeModifiers bonus = calculate_skill_bonus(slot->skill_id, slot->level);
            totals.attack_bonus += bonus.attack_bonus;
            totals.defense_bonus += bonus.defense_bonus;
            totals.magic_attack_bonus += bonus.magic_attack_bonus;
            totals.magic_defense_bonus += bonus.magic_defense_bonus;
            totals.hit_rate_bonus += bonus.hit_rate_bonus;
            totals.dodge_bonus += bonus.dodge_bonus;
            totals.speed_bonus += bonus.speed_bonus;
        }
    }

    registry_.emplace_or_replace<AttributeModifiers>(entity, totals);
}

AttributeModifiers PassiveSkillSystem::get_passive_bonuses(entt::entity entity) const {
    if (!registry_.valid(entity)) {
        return {};
    }

    const auto* cached = registry_.try_get<AttributeModifiers>(entity);
    return cached ? *cached : AttributeModifiers{};
}

AttributeModifiers PassiveSkillSystem::trigger_on_attack(entt::entity attacker) const {
    return get_passive_bonuses(attacker);
}

AttributeModifiers PassiveSkillSystem::calculate_skill_bonus(uint32_t skill_id, uint8_t level) const {
    AttributeModifiers bonus{};
    const int clamped_level = std::min(3, static_cast<int>(level));

    switch (skill_id) {
    case mir2::common::SkillId::ONESWORD: {
        // 基础剑术：命中率加成
        bonus.hit_rate_bonus = std::max(
            0, clamped_level * PassiveBonus::ONESWORD_HIT_PER_LEVEL);
        break;
    }
    case mir2::common::SkillId::BODYGUARD: {
        // 护体神功：防御加成
        bonus.defense_bonus =
            clamped_level * PassiveBonus::BODYGUARD_DEF_PER_LEVEL;
        break;
    }
    case mir2::common::SkillId::BLOODLUST: {
        // 嗜血术：攻击加成
        bonus.attack_bonus =
            clamped_level * PassiveBonus::BLOODLUST_ATK_PER_LEVEL;
        break;
    }
    case mir2::common::SkillId::WUJIZHENQI:
    case mir2::common::SkillId::TAOIST_WUJI: {
        // 无极真气：攻击和防御加成
        bonus.attack_bonus =
            clamped_level * PassiveBonus::WUJI_ATK_PER_LEVEL;
        bonus.defense_bonus =
            clamped_level * PassiveBonus::WUJI_DEF_PER_LEVEL;
        break;
    }
    case mir2::common::SkillId::GHOSTSHIELD: {
        // 幽灵盾：魔防加成
        bonus.magic_defense_bonus =
            clamped_level * PassiveBonus::GHOSTSHIELD_MDEF_PER_LEVEL;
        break;
    }
    case mir2::common::SkillId::DEJIWONHO: {
        // 神圣战甲：防御加成
        bonus.defense_bonus =
            clamped_level * PassiveBonus::DEJIWONHO_DEF_PER_LEVEL;
        break;
    }
    default:
        break;
    }

    return bonus;
}

} // namespace mir2::ecs
