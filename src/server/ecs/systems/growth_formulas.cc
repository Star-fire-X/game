#include "ecs/systems/growth_formulas.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace mir2::ecs {

NakedAbility GrowthFormulas::CalcLevelAbility(common::CharacterClass char_class, int level) {
    NakedAbility ability;
    if (level < 1) level = 1;
    if (level > common::MAX_LEVEL) level = common::MAX_LEVEL;

    // 原版 Pascal 公式 — 二次多项式 + 线性混合
    // 各职业使用不同的系数
    switch (char_class) {
        case common::CharacterClass::WARRIOR: {
            // HP: 战士成长最高
            // 公式: DEFHP + level * (level * 0.18 + 2.0 + 8.0)
            // 简化: DEFHP + level * (0.18*level + 10.0)
            ability.max_hp = common::DEFHP +
                static_cast<int>(level * (level * 0.18 + 10.0));

            // MP: 战士MP成长最低
            ability.max_mp = common::DEFMP +
                static_cast<int>(level * (level * 0.02 + 1.5));

            // DC: 物理攻击
            ability.dc_lo = 1;
            ability.dc_hi = static_cast<int>(level * 0.5 + 2);

            // MC: 战士不擅长魔法
            ability.mc_lo = 0;
            ability.mc_hi = static_cast<int>(level * 0.1);

            // SC: 战士精神力较低
            ability.sc_lo = 0;
            ability.sc_hi = static_cast<int>(level * 0.15);

            // AC: 物理防御（战士最高）
            ability.ac_lo = 0;
            ability.ac_hi = static_cast<int>(level * 0.3 + 2);

            // MAC: 魔法防御
            ability.mac_lo = 0;
            ability.mac_hi = static_cast<int>(level * 0.15);

            // 负重: 战士最高
            ability.max_weight = static_cast<int>(50 + level * (level * 0.16 + 1.5));
            ability.max_wear_weight = static_cast<int>(15 + level * 0.5);
            ability.max_hand_weight = static_cast<int>(12 + level * 0.4);
            break;
        }

        case common::CharacterClass::MAGE: {
            // HP: 法师成长最低
            ability.max_hp = common::DEFHP +
                static_cast<int>(level * (level * 0.06 + 2.5));

            // MP: 法师MP成长最高
            ability.max_mp = common::DEFMP +
                static_cast<int>(level * (level * 0.25 + 6.0));

            // DC: 法师物理攻击最低
            ability.dc_lo = 1;
            ability.dc_hi = static_cast<int>(level * 0.2 + 1);

            // MC: 法师魔法攻击最高
            ability.mc_lo = static_cast<int>(level * 0.15);
            ability.mc_hi = static_cast<int>(level * 0.5 + 2);

            // SC: 法师精神力低
            ability.sc_lo = 0;
            ability.sc_hi = static_cast<int>(level * 0.1);

            // AC: 法师物理防御最低
            ability.ac_lo = 0;
            ability.ac_hi = static_cast<int>(level * 0.1);

            // MAC: 法师魔法防御最高
            ability.mac_lo = 0;
            ability.mac_hi = static_cast<int>(level * 0.3 + 1);

            // 负重: 法师最低
            ability.max_weight = static_cast<int>(50 + level * (level * 0.08 + 0.5));
            ability.max_wear_weight = static_cast<int>(15 + level * 0.3);
            ability.max_hand_weight = static_cast<int>(12 + level * 0.25);
            break;
        }

        case common::CharacterClass::TAOIST: {
            // HP: 道士成长中等
            ability.max_hp = common::DEFHP +
                static_cast<int>(level * (level * 0.1 + 4.5));

            // MP: 道士MP成长中等
            ability.max_mp = common::DEFMP +
                static_cast<int>(level * (level * 0.1 + 5.0));

            // DC: 道士物理攻击中等
            ability.dc_lo = 1;
            ability.dc_hi = static_cast<int>(level * 0.35 + 1);

            // MC: 道士魔法攻击中等
            ability.mc_lo = 0;
            ability.mc_hi = static_cast<int>(level * 0.2);

            // SC: 道士精神力最高
            ability.sc_lo = static_cast<int>(level * 0.15);
            ability.sc_hi = static_cast<int>(level * 0.5 + 2);

            // AC: 道士物理防御中等
            ability.ac_lo = 0;
            ability.ac_hi = static_cast<int>(level * 0.2 + 1);

            // MAC: 道士魔法防御中等
            ability.mac_lo = 0;
            ability.mac_hi = static_cast<int>(level * 0.25 + 1);

            // 负重: 道士中等
            ability.max_weight = static_cast<int>(50 + level * (level * 0.1 + 1.0));
            ability.max_wear_weight = static_cast<int>(15 + level * 0.4);
            ability.max_hand_weight = static_cast<int>(12 + level * 0.3);
            break;
        }
    }

    return ability;
}

int64_t GrowthFormulas::GetExpForLevel(int level) {
    if (level < 1 || level > common::MAX_LEVEL) {
        return std::numeric_limits<int64_t>::max();
    }
    return common::NEED_EXPS[level];
}

int GrowthFormulas::CalcGetExp(int player_lv, int monster_lv, int base_exp) {
    if (base_exp <= 0) return 0;
    if (player_lv <= 0) player_lv = 1;
    if (monster_lv <= 0) monster_lv = 1;

    int level_diff = player_lv - monster_lv;

    // 玩家等级低于或等于怪物等级时，获得全部经验
    if (level_diff <= 0) {
        return base_exp;
    }

    // 等级差惩罚：每差1级减少约5%经验
    // 原版公式: exp = base_exp * (1.0 - level_diff * 0.05)
    // 但最低经验为1
    double factor = 1.0 - level_diff * 0.05;
    if (factor < 0.01) {
        factor = 0.01;
    }

    int result = static_cast<int>(base_exp * factor);
    return std::max(result, 1);
}

int GrowthFormulas::GetBonusPoint(common::CharacterClass char_class, int level) {
    if (level <= common::ADJ_LEVEL) {
        return 0;
    }

    int levels_above_adj = level - common::ADJ_LEVEL;

    // 每级获得的奖励点数因职业不同
    // 原版: 战士每级4点, 法师每级1点, 道士每级1点
    // (但实际战士偏多因为需分配物理属性)
    int points_per_level = 0;
    switch (char_class) {
        case common::CharacterClass::WARRIOR:
            // 战士: 21-24级每级4点, 25+每级4点 → simplified to flat 4
            points_per_level = 4;
            break;
        case common::CharacterClass::MAGE:
            // 法师: 每级1点 (较少，靠装备和技能)
            points_per_level = 1;
            break;
        case common::CharacterClass::TAOIST:
            // 道士: 每级1点
            points_per_level = 1;
            break;
    }

    return levels_above_adj * points_per_level;
}

double GrowthFormulas::GetPartyExpBonus(int member_count) {
    if (member_count <= 0) return 0.0;
    if (member_count == 1) return 1.0;
    if (member_count > common::GROUP_MAX) {
        member_count = common::GROUP_MAX;
    }
    return common::PARTY_EXP_BONUS[member_count - 1];
}

}  // namespace mir2::ecs
