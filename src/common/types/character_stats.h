// =============================================================================
// Legend2 角色属性定义 (Character Stats)
//
// 功能说明:
//   - 角色属性结构
//   - 职业基础属性
//   - JSON序列化支持
// =============================================================================

#ifndef LEGEND2_COMMON_TYPES_CHARACTER_STATS_H
#define LEGEND2_COMMON_TYPES_CHARACTER_STATS_H

#include <nlohmann/json.hpp>
#include "common/enums.h"

namespace mir2::common {

/// 角色属性/状态
struct CharacterStats {
    int level = 1;
    int hp = 100;
    int max_hp = 100;
    int mp = 50;
    int max_mp = 50;
    int attack = 10;
    int defense = 5;
    int magic_attack = 10;
    int magic_defense = 5;
    int speed = 5;
    int64_t experience = 0;
    int gold = 0;
    int body_luck = 0;
    int max_weight = 0;
    int max_wear_weight = 0;
    int max_hand_weight = 0;
    int bonus_remaining = 0;
    int bonus_dc = 0;
    int bonus_mc = 0;
    int bonus_sc = 0;
    int bonus_ac = 0;
    int bonus_mac = 0;
    int bonus_hp = 0;
    int bonus_mp = 0;
    int bonus_hit = 0;
    int bonus_speed = 0;

    bool operator==(const CharacterStats& other) const {
        return level == other.level &&
               hp == other.hp && max_hp == other.max_hp &&
               mp == other.mp && max_mp == other.max_mp &&
               attack == other.attack && defense == other.defense &&
               magic_attack == other.magic_attack && magic_defense == other.magic_defense &&
               speed == other.speed && experience == other.experience &&
               gold == other.gold && body_luck == other.body_luck &&
               max_weight == other.max_weight &&
               max_wear_weight == other.max_wear_weight &&
               max_hand_weight == other.max_hand_weight &&
               bonus_remaining == other.bonus_remaining;
    }
};

// JSON serialization
inline void to_json(nlohmann::json& j, const CharacterStats& s) {
    j = nlohmann::json{
        {"level", s.level},
        {"hp", s.hp}, {"max_hp", s.max_hp},
        {"mp", s.mp}, {"max_mp", s.max_mp},
        {"attack", s.attack}, {"defense", s.defense},
        {"magic_attack", s.magic_attack}, {"magic_defense", s.magic_defense},
        {"speed", s.speed}, {"experience", s.experience},
        {"gold", s.gold}, {"body_luck", s.body_luck},
        {"max_weight", s.max_weight},
        {"max_wear_weight", s.max_wear_weight},
        {"max_hand_weight", s.max_hand_weight},
        {"bonus_remaining", s.bonus_remaining},
        {"bonus_dc", s.bonus_dc}, {"bonus_mc", s.bonus_mc},
        {"bonus_sc", s.bonus_sc}, {"bonus_ac", s.bonus_ac},
        {"bonus_mac", s.bonus_mac}, {"bonus_hp", s.bonus_hp},
        {"bonus_mp", s.bonus_mp}, {"bonus_hit", s.bonus_hit},
        {"bonus_speed", s.bonus_speed}
    };
}

inline void from_json(const nlohmann::json& j, CharacterStats& s) {
    j.at("level").get_to(s.level);
    j.at("hp").get_to(s.hp);
    j.at("max_hp").get_to(s.max_hp);
    j.at("mp").get_to(s.mp);
    j.at("max_mp").get_to(s.max_mp);
    j.at("attack").get_to(s.attack);
    j.at("defense").get_to(s.defense);
    j.at("magic_attack").get_to(s.magic_attack);
    j.at("magic_defense").get_to(s.magic_defense);
    j.at("speed").get_to(s.speed);
    j.at("experience").get_to(s.experience);
    j.at("gold").get_to(s.gold);
    // New fields with backward-compatible defaults
    if (j.contains("body_luck")) j.at("body_luck").get_to(s.body_luck);
    if (j.contains("max_weight")) j.at("max_weight").get_to(s.max_weight);
    if (j.contains("max_wear_weight")) j.at("max_wear_weight").get_to(s.max_wear_weight);
    if (j.contains("max_hand_weight")) j.at("max_hand_weight").get_to(s.max_hand_weight);
    if (j.contains("bonus_remaining")) j.at("bonus_remaining").get_to(s.bonus_remaining);
    if (j.contains("bonus_dc")) j.at("bonus_dc").get_to(s.bonus_dc);
    if (j.contains("bonus_mc")) j.at("bonus_mc").get_to(s.bonus_mc);
    if (j.contains("bonus_sc")) j.at("bonus_sc").get_to(s.bonus_sc);
    if (j.contains("bonus_ac")) j.at("bonus_ac").get_to(s.bonus_ac);
    if (j.contains("bonus_mac")) j.at("bonus_mac").get_to(s.bonus_mac);
    if (j.contains("bonus_hp")) j.at("bonus_hp").get_to(s.bonus_hp);
    if (j.contains("bonus_mp")) j.at("bonus_mp").get_to(s.bonus_mp);
    if (j.contains("bonus_hit")) j.at("bonus_hit").get_to(s.bonus_hit);
    if (j.contains("bonus_speed")) j.at("bonus_speed").get_to(s.bonus_speed);
}

/// 获取指定职业1级时的基础属性
inline CharacterStats get_class_base_stats(CharacterClass char_class) {
    CharacterStats stats;
    stats.level = 1;
    stats.experience = 0;
    stats.gold = 0;

    switch (char_class) {
        case CharacterClass::WARRIOR:
            stats.max_hp = 150;
            stats.hp = 150;
            stats.max_mp = 30;
            stats.mp = 30;
            stats.attack = 15;
            stats.defense = 10;
            stats.magic_attack = 5;
            stats.magic_defense = 5;
            stats.speed = 4;
            break;

        case CharacterClass::MAGE:
            stats.max_hp = 80;
            stats.hp = 80;
            stats.max_mp = 100;
            stats.mp = 100;
            stats.attack = 5;
            stats.defense = 3;
            stats.magic_attack = 20;
            stats.magic_defense = 10;
            stats.speed = 5;
            break;

        case CharacterClass::TAOIST:
            stats.max_hp = 100;
            stats.hp = 100;
            stats.max_mp = 80;
            stats.mp = 80;
            stats.attack = 8;
            stats.defense = 5;
            stats.magic_attack = 12;
            stats.magic_defense = 12;
            stats.speed = 5;
            break;
    }

    return stats;
}

} // namespace mir2::common

#endif // LEGEND2_COMMON_TYPES_CHARACTER_STATS_H
