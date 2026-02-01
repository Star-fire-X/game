/**
 * @file character.h
 * @brief Legacy Character interface for old server systems.
 */

#ifndef LEGEND2_SERVER_LEGACY_CHARACTER_H
#define LEGEND2_SERVER_LEGACY_CHARACTER_H

#include <algorithm>
#include <cstdint>

#include "common/types.h"

namespace legend2 {

/**
 * @brief Minimal legacy Character implementation for legacy systems.
 *
 * This keeps the legacy inventory/skill/AI modules buildable without the
 * new ECS-only character model.
 */
class Character {
public:
    Character(uint32_t id = 0, mir2::common::CharacterClass char_class = mir2::common::CharacterClass::WARRIOR)
        : id_(id), char_class_(char_class) {}

    uint32_t get_id() const { return id_; }
    mir2::common::CharacterClass get_class() const { return char_class_; }
    int get_level() const { return stats_.level; }
    int get_mp() const { return stats_.mp; }
    int get_attack() const { return stats_.attack; }
    int get_magic_attack() const { return stats_.magic_attack; }
    const mir2::common::CharacterStats& get_stats() const { return stats_; }
    const mir2::common::Position& get_position() const { return position_; }
    uint32_t get_map_id() const { return map_id_; }

    bool is_dead() const { return stats_.hp <= 0; }
    bool is_alive() const { return stats_.hp > 0; }

    int take_damage(int amount) {
        int damage = std::max(0, amount);
        stats_.hp = std::max(0, stats_.hp - damage);
        return damage;
    }

    int heal(int amount) {
        int heal_amount = std::max(0, amount);
        int before = stats_.hp;
        stats_.hp = std::min(stats_.max_hp, stats_.hp + heal_amount);
        return stats_.hp - before;
    }

    void consume_mp(int amount) {
        int cost = std::max(0, amount);
        stats_.mp = std::max(0, stats_.mp - cost);
    }

    void add_stats(const mir2::common::CharacterStats& bonus) {
        stats_.max_hp += bonus.max_hp;
        stats_.max_mp += bonus.max_mp;
        stats_.attack += bonus.attack;
        stats_.defense += bonus.defense;
        stats_.magic_attack += bonus.magic_attack;
        stats_.magic_defense += bonus.magic_defense;
        stats_.speed += bonus.speed;
        stats_.hp = std::min(stats_.max_hp, stats_.hp + bonus.hp);
        stats_.mp = std::min(stats_.max_mp, stats_.mp + bonus.mp);
    }

    void remove_stats(const mir2::common::CharacterStats& bonus) {
        stats_.max_hp = std::max(0, stats_.max_hp - bonus.max_hp);
        stats_.max_mp = std::max(0, stats_.max_mp - bonus.max_mp);
        stats_.attack -= bonus.attack;
        stats_.defense -= bonus.defense;
        stats_.magic_attack -= bonus.magic_attack;
        stats_.magic_defense -= bonus.magic_defense;
        stats_.speed -= bonus.speed;
        stats_.hp = std::min(stats_.hp, stats_.max_hp);
        stats_.mp = std::min(stats_.mp, stats_.max_mp);
    }

    void set_position(const mir2::common::Position& position) { position_ = position; }
    void set_map_id(uint32_t map_id) { map_id_ = map_id; }
    void set_class(mir2::common::CharacterClass char_class) { char_class_ = char_class; }
    mir2::common::CharacterStats& mutable_stats() { return stats_; }

private:
    uint32_t id_ = 0;
    mir2::common::CharacterClass char_class_ = mir2::common::CharacterClass::WARRIOR;
    mir2::common::CharacterStats stats_;
    mir2::common::Position position_{0, 0};
    uint32_t map_id_ = 0;
};

}  // namespace legend2

#endif  // LEGEND2_SERVER_LEGACY_CHARACTER_H
