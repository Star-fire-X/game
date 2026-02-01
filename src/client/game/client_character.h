// =============================================================================
// Legend2 Client Character State
//
// Purpose:
//   - Maintain client-side character state (position, class, gender, etc.)
//   - Reuse CharacterData for network/UI workflows
// =============================================================================

#ifndef LEGEND2_CLIENT_CHARACTER_H
#define LEGEND2_CLIENT_CHARACTER_H

#include "common/character_data.h"

namespace mir2::game {

using mir2::common::CharacterClass;
using mir2::common::CharacterData;
using mir2::common::Gender;
using mir2::common::Position;
using mir2::common::get_class_base_stats;

/// Lightweight client-side character state.
class ClientCharacter {
public:
    ClientCharacter(uint32_t id, const std::string& name, CharacterClass char_class, Gender gender = Gender::MALE)
        : data_{} {
        data_.id = id;
        data_.name = name;
        data_.char_class = char_class;
        data_.gender = gender;
        data_.stats = get_class_base_stats(char_class);
        data_.map_id = 1;
        data_.position = {100, 100};
    }

    explicit ClientCharacter(const CharacterData& data)
        : data_(data) {}

    /// Get character ID.
    uint32_t get_id() const { return data_.id; }

    /// Get character name.
    const std::string& get_name() const { return data_.name; }

    /// Get character class.
    CharacterClass get_class() const { return data_.char_class; }

    /// Get gender.
    Gender get_gender() const { return data_.gender; }

    /// Get map ID.
    uint32_t get_map_id() const { return data_.map_id; }

    /// Get position.
    const Position& get_position() const { return data_.position; }

    /// Set position.
    void set_position(const Position& pos) { data_.position = pos; }

    /// Set position by coordinates.
    void set_position(int x, int y) { data_.position = {x, y}; }

    /// Set map ID.
    void set_map_id(uint32_t map_id) { data_.map_id = map_id; }

    /// Access underlying data.
    const CharacterData& get_data() const { return data_; }

    /// Access underlying data (mutable).
    CharacterData& get_data_mut() { return data_; }

private:
    CharacterData data_;
};

} // namespace mir2::game

#endif // LEGEND2_CLIENT_CHARACTER_H
