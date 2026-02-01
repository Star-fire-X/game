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
    int experience = 0;
    int gold = 0;

    bool operator==(const CharacterStats& other) const {
        return level == other.level &&
               hp == other.hp && max_hp == other.max_hp &&
               mp == other.mp && max_mp == other.max_mp &&
               attack == other.attack && defense == other.defense &&
               magic_attack == other.magic_attack && magic_defense == other.magic_defense &&
               speed == other.speed && experience == other.experience &&
               gold == other.gold;
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
        {"gold", s.gold}
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
