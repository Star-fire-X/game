/**
 * @file skill_manager.cc
 * @brief Client skill manager implementation.
 */

#include "skill_manager.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <string>
#include <utility>

#include <yaml-cpp/yaml.h>

namespace mir2::client::skill {

namespace {

std::string to_lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

template <typename T>
bool read_scalar(const YAML::Node& node, const char* key, T* out) {
    if (!node || !node[key]) {
        return false;
    }
    *out = node[key].as<T>();
    return true;
}

template <typename T>
bool read_scalar_or_sequence_first(const YAML::Node& node, const char* key, T* out) {
    if (!node || !node[key]) {
        return false;
    }
    const YAML::Node value = node[key];
    if (value.IsSequence()) {
        if (value.size() == 0) {
            return false;
        }
        *out = value[0].as<T>();
        return true;
    }
    *out = value.as<T>();
    return true;
}

mir2::common::SkillType parse_skill_type(const YAML::Node& node) {
    if (!node) {
        return mir2::common::SkillType::PHYSICAL;
    }

    const std::string raw = node.as<std::string>();
    if (raw.empty()) {
        return mir2::common::SkillType::PHYSICAL;
    }

    char* end = nullptr;
    const long value = std::strtol(raw.c_str(), &end, 10);
    if (end != nullptr && *end == '\0') {
        if (value >= 0 && value <= 4) {
            return static_cast<mir2::common::SkillType>(value);
        }
        if (value >= 1 && value <= 5) {
            return static_cast<mir2::common::SkillType>(value - 1);
        }
        return mir2::common::SkillType::PHYSICAL;
    }

    const std::string lowered = to_lower(raw);
    if (lowered == "physical" || lowered == "melee") {
        return mir2::common::SkillType::PHYSICAL;
    }
    if (lowered == "magical" || lowered == "magic") {
        return mir2::common::SkillType::MAGICAL;
    }
    if (lowered == "buff") {
        return mir2::common::SkillType::BUFF;
    }
    if (lowered == "debuff") {
        return mir2::common::SkillType::DEBUFF;
    }
    if (lowered == "heal" || lowered == "healing") {
        return mir2::common::SkillType::HEAL;
    }
    return mir2::common::SkillType::PHYSICAL;
}

mir2::common::SkillTarget parse_skill_target(const YAML::Node& node) {
    if (!node) {
        return mir2::common::SkillTarget::SELF;
    }

    const std::string raw = node.as<std::string>();
    if (raw.empty()) {
        return mir2::common::SkillTarget::SELF;
    }

    char* end = nullptr;
    const long value = std::strtol(raw.c_str(), &end, 10);
    if (end != nullptr && *end == '\0') {
        if (value >= 0 && value <= 5) {
            return static_cast<mir2::common::SkillTarget>(value);
        }
        if (value >= 1 && value <= 6) {
            return static_cast<mir2::common::SkillTarget>(value - 1);
        }
        return mir2::common::SkillTarget::SELF;
    }

    const std::string lowered = to_lower(raw);
    if (lowered == "self") {
        return mir2::common::SkillTarget::SELF;
    }
    if (lowered == "single_enemy" || lowered == "enemy" || lowered == "line_enemy") {
        return mir2::common::SkillTarget::SINGLE_ENEMY;
    }
    if (lowered == "single_ally" || lowered == "ally") {
        return mir2::common::SkillTarget::SINGLE_ALLY;
    }
    if (lowered == "aoe_enemy" || lowered == "enemy_aoe" || lowered == "aoe_ground" ||
        lowered == "ground" || lowered == "line" || lowered == "aoe_ground_enemy") {
        return mir2::common::SkillTarget::AOE_ENEMY;
    }
    if (lowered == "aoe_ally" || lowered == "ally_aoe") {
        return mir2::common::SkillTarget::AOE_ALLY;
    }
    if (lowered == "aoe_all" || lowered == "aoe") {
        return mir2::common::SkillTarget::AOE_ALL;
    }
    return mir2::common::SkillTarget::SELF;
}

bool is_valid_slot(uint8_t slot) {
    return slot >= 1 && slot <= SkillManager::kHotkeyCount;
}

} // namespace

bool SkillManager::load_templates_from_yaml(const std::string& path) {
    try {
        const YAML::Node root = YAML::LoadFile(path);
        const YAML::Node skills_node = root["skills"] ? root["skills"] : root;
        if (!skills_node || !skills_node.IsSequence()) {
            return false;
        }

        std::unordered_map<uint32_t, ClientSkillTemplate> loaded;
        loaded.reserve(skills_node.size());

        for (const auto& node : skills_node) {
            if (!node || !node.IsMap() || !node["id"]) {
                continue;
            }

            ClientSkillTemplate skill;
            skill.id = node["id"].as<uint32_t>();
            if (skill.id == 0) {
                continue;
            }

            if (node["name"]) {
                skill.name = node["name"].as<std::string>();
            }
            if (node["description"]) {
                skill.description = node["description"].as<std::string>();
            }

            if (node["skill_type"] || node["damage_type"]) {
                const YAML::Node type_node = node["skill_type"] ? node["skill_type"] : node["damage_type"];
                skill.skill_type = parse_skill_type(type_node);
            }
            if (node["target_type"]) {
                skill.target_type = parse_skill_target(node["target_type"]);
            }
            if (node["is_passive"]) {
                skill.is_passive = node["is_passive"].as<bool>();
            }

            read_scalar_or_sequence_first(node, "mp_cost", &skill.mp_cost);

            if (!read_scalar(node, "cooldown_ms", &skill.cooldown_ms)) {
                double cooldown_seconds = 0.0;
                if (read_scalar_or_sequence_first(node, "cooldown", &cooldown_seconds)) {
                    skill.cooldown_ms = static_cast<int>(cooldown_seconds * 1000.0);
                }
            }

            if (!read_scalar(node, "cast_time_ms", &skill.cast_time_ms)) {
                double cast_seconds = 0.0;
                if (read_scalar_or_sequence_first(node, "cast_time", &cast_seconds)) {
                    skill.cast_time_ms = static_cast<int>(cast_seconds * 1000.0);
                }
            }

            read_scalar(node, "range", &skill.range);
            read_scalar(node, "aoe_radius", &skill.aoe_radius);

            if (node["icon_id"]) {
                skill.icon_id = node["icon_id"].as<uint32_t>();
            }
            if (node["effect_id"]) {
                skill.effect_id = node["effect_id"].as<uint32_t>();
            }

            loaded[skill.id] = std::move(skill);
        }

        templates_ = std::move(loaded);
        return !templates_.empty();
    } catch (const std::exception&) {
        return false;
    }
}

bool SkillManager::add_learned_skill(const ClientLearnedSkill& skill) {
    if (skill.skill_id == 0) {
        return false;
    }

    for (const auto& slot : learned_skills_) {
        if (slot && slot->skill_id == skill.skill_id) {
            return false;
        }
    }

    auto empty_slot = std::find_if(learned_skills_.begin(), learned_skills_.end(),
                                   [](const auto& slot) { return !slot.has_value(); });
    if (empty_slot == learned_skills_.end()) {
        return false;
    }

    ClientLearnedSkill stored = skill;
    if (!is_valid_slot(stored.hotkey)) {
        stored.hotkey = 0;
    }

    *empty_slot = stored;

    if (stored.hotkey != 0) {
        if (!bind_hotkey(stored.hotkey, stored.skill_id)) {
            empty_slot->value().hotkey = 0;
        }
    }

    return true;
}

bool SkillManager::remove_learned_skill(uint32_t skill_id) {
    for (auto& slot : learned_skills_) {
        if (slot && slot->skill_id == skill_id) {
            const uint8_t hotkey = slot->hotkey;
            if (is_valid_slot(hotkey)) {
                const size_t index = static_cast<size_t>(hotkey - 1);
                if (hotkey_bindings_[index] == skill_id) {
                    hotkey_bindings_[index] = 0;
                }
            }
            slot.reset();
            return true;
        }
    }
    return false;
}

const ClientSkillTemplate* SkillManager::get_template(uint32_t skill_id) const {
    auto it = templates_.find(skill_id);
    if (it == templates_.end()) {
        return nullptr;
    }
    return &it->second;
}

const ClientLearnedSkill* SkillManager::get_learned_skill(uint32_t skill_id) const {
    for (const auto& slot : learned_skills_) {
        if (slot && slot->skill_id == skill_id) {
            return &(*slot);
        }
    }
    return nullptr;
}

bool SkillManager::has_skill(uint32_t skill_id) const {
    return get_learned_skill(skill_id) != nullptr;
}

bool SkillManager::bind_hotkey(uint8_t slot, uint32_t skill_id) {
    if (!is_valid_slot(slot) || skill_id == 0) {
        return false;
    }

    ClientLearnedSkill* learned = nullptr;
    for (auto& entry : learned_skills_) {
        if (entry && entry->skill_id == skill_id) {
            learned = &(*entry);
            break;
        }
    }
    if (!learned) {
        return false;
    }

    const size_t index = static_cast<size_t>(slot - 1);
    const uint32_t previous_skill = hotkey_bindings_[index];
    if (previous_skill != 0 && previous_skill != skill_id) {
        for (auto& entry : learned_skills_) {
            if (entry && entry->skill_id == previous_skill) {
                entry->hotkey = 0;
                break;
            }
        }
    }

    if (is_valid_slot(learned->hotkey)) {
        const size_t prev_index = static_cast<size_t>(learned->hotkey - 1);
        if (hotkey_bindings_[prev_index] == skill_id) {
            hotkey_bindings_[prev_index] = 0;
        }
    }

    hotkey_bindings_[index] = skill_id;
    learned->hotkey = slot;
    return true;
}

bool SkillManager::unbind_hotkey(uint8_t slot) {
    if (!is_valid_slot(slot)) {
        return false;
    }
    const size_t index = static_cast<size_t>(slot - 1);
    const uint32_t skill_id = hotkey_bindings_[index];
    if (skill_id == 0) {
        return false;
    }

    hotkey_bindings_[index] = 0;
    for (auto& entry : learned_skills_) {
        if (entry && entry->skill_id == skill_id) {
            entry->hotkey = 0;
            break;
        }
    }
    return true;
}

uint32_t SkillManager::get_skill_by_hotkey(uint8_t slot) const {
    if (!is_valid_slot(slot)) {
        return 0;
    }
    const size_t index = static_cast<size_t>(slot - 1);
    return hotkey_bindings_[index];
}

bool SkillManager::is_ready(uint32_t skill_id, int64_t now_ms) const {
    auto it = cooldowns_.find(skill_id);
    if (it == cooldowns_.end()) {
        return true;
    }
    return now_ms >= it->second.cooldown_end_ms;
}

void SkillManager::start_cooldown(uint32_t skill_id, int64_t duration_ms, int64_t now_ms) {
    if (skill_id == 0) {
        return;
    }
    if (duration_ms <= 0) {
        cooldowns_.erase(skill_id);
        return;
    }
    SkillCooldownState state;
    state.skill_id = skill_id;
    state.total_cooldown_ms = duration_ms;
    state.cooldown_end_ms = now_ms + duration_ms;
    cooldowns_[skill_id] = state;
}

int64_t SkillManager::get_remaining_cooldown_ms(uint32_t skill_id, int64_t now_ms) const {
    auto it = cooldowns_.find(skill_id);
    if (it == cooldowns_.end()) {
        return 0;
    }
    const int64_t remaining = it->second.cooldown_end_ms - now_ms;
    return remaining > 0 ? remaining : 0;
}

void SkillManager::start_casting(uint32_t skill_id, uint64_t target_id, int64_t cast_time_ms, int64_t now_ms) {
    casting_state_.is_casting = true;
    casting_state_.skill_id = skill_id;
    casting_state_.target_id = target_id;
    casting_state_.cast_start_ms = now_ms;
    casting_state_.cast_end_ms = now_ms + cast_time_ms;
}

void SkillManager::cancel_casting() {
    casting_state_.cancel();
}

void SkillManager::update(int64_t now_ms) {
    for (auto it = cooldowns_.begin(); it != cooldowns_.end();) {
        if (now_ms >= it->second.cooldown_end_ms) {
            it = cooldowns_.erase(it);
        } else {
            ++it;
        }
    }
}

} // namespace mir2::client::skill
