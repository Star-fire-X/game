/**
 * @file skill_registry.cc
 * @brief 技能模板注册表实现
 */

#include "ecs/skill_registry.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <utility>

#include <yaml-cpp/yaml.h>

namespace mir2::ecs {

namespace {

std::string ToLower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

template <typename T>
bool ReadScalar(const YAML::Node& node, const char* key, T* out) {
    if (!node || !node[key]) {
        return false;
    }
    *out = node[key].as<T>();
    return true;
}

template <typename T>
bool ReadScalarOrSequenceFirst(const YAML::Node& node, const char* key, T* out) {
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

template <std::size_t N>
bool ReadSequence(const YAML::Node& node, const char* key, std::array<uint8_t, N>* out) {
    if (!node || !node[key]) {
        return false;
    }
    const YAML::Node value = node[key];
    auto assign_value = [&](std::size_t index, const YAML::Node& item) {
        (*out)[index] = static_cast<uint8_t>(item.as<int>());
    };
    if (!value.IsSequence()) {
        assign_value(0, value);
        return true;
    }
    std::size_t index = 0;
    for (const auto& item : value) {
        if (index >= N) {
            break;
        }
        assign_value(index, item);
        ++index;
    }
    return true;
}

template <typename T, std::size_t N>
bool ReadSequence(const YAML::Node& node, const char* key, std::array<T, N>* out) {
    if (!node || !node[key]) {
        return false;
    }
    const YAML::Node value = node[key];
    if (!value.IsSequence()) {
        (*out)[0] = value.as<T>();
        return true;
    }
    std::size_t index = 0;
    for (const auto& item : value) {
        if (index >= N) {
            break;
        }
        (*out)[index] = item.as<T>();
        ++index;
    }
    return true;
}

mir2::common::CharacterClass ParseCharacterClass(const YAML::Node& node) {
    if (!node) {
        return mir2::common::CharacterClass::WARRIOR;
    }

    const std::string raw = node.as<std::string>();
    if (raw.empty()) {
        return mir2::common::CharacterClass::WARRIOR;
    }

    char* end = nullptr;
    const long value = std::strtol(raw.c_str(), &end, 10);
    if (end != nullptr && *end == '\0') {
        if (value >= 1 && value <= 3) {
            return static_cast<mir2::common::CharacterClass>(value - 1);
        }
        if (value >= 0 && value <= 2) {
            return static_cast<mir2::common::CharacterClass>(value);
        }
        return mir2::common::CharacterClass::WARRIOR;
    }

    const std::string lowered = ToLower(raw);
    if (lowered == "warrior" || lowered == "fighter") {
        return mir2::common::CharacterClass::WARRIOR;
    }
    if (lowered == "mage" || lowered == "wizard") {
        return mir2::common::CharacterClass::MAGE;
    }
    if (lowered == "taoist" || lowered == "priest") {
        return mir2::common::CharacterClass::TAOIST;
    }
    return mir2::common::CharacterClass::WARRIOR;
}

mir2::common::SkillType ParseSkillType(const YAML::Node& node) {
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

    const std::string lowered = ToLower(raw);
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

mir2::common::SkillTarget ParseSkillTarget(const YAML::Node& node) {
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

    const std::string lowered = ToLower(raw);
    if (lowered == "self") {
        return mir2::common::SkillTarget::SELF;
    }
    if (lowered == "single_enemy") {
        return mir2::common::SkillTarget::SINGLE_ENEMY;
    }
    if (lowered == "single_ally") {
        return mir2::common::SkillTarget::SINGLE_ALLY;
    }
    if (lowered == "aoe_enemy") {
        return mir2::common::SkillTarget::AOE_ENEMY;
    }
    if (lowered == "aoe_ally") {
        return mir2::common::SkillTarget::AOE_ALLY;
    }
    if (lowered == "aoe_all" || lowered == "aoe") {
        return mir2::common::SkillTarget::AOE_ALL;
    }
    return mir2::common::SkillTarget::SELF;
}

mir2::common::AmuletType ParseAmuletType(const YAML::Node& node) {
    if (!node) {
        return mir2::common::AmuletType::NONE;
    }

    const std::string raw = node.as<std::string>();
    if (raw.empty()) {
        return mir2::common::AmuletType::NONE;
    }

    char* end = nullptr;
    const long value = std::strtol(raw.c_str(), &end, 10);
    if (end != nullptr && *end == '\0') {
        if (value >= 0 && value <= 4) {
            return static_cast<mir2::common::AmuletType>(value);
        }
        if (value >= 1 && value <= 5) {
            return static_cast<mir2::common::AmuletType>(value - 1);
        }
        return mir2::common::AmuletType::NONE;
    }

    const std::string lowered = ToLower(raw);
    if (lowered == "none") {
        return mir2::common::AmuletType::NONE;
    }
    if (lowered == "holy" || lowered == "sacred") {
        return mir2::common::AmuletType::HOLY;
    }
    if (lowered == "poison" || lowered == "toxic") {
        return mir2::common::AmuletType::POISON;
    }
    if (lowered == "fire" || lowered == "flame") {
        return mir2::common::AmuletType::FIRE;
    }
    if (lowered == "ice" || lowered == "frost") {
        return mir2::common::AmuletType::ICE;
    }
    return mir2::common::AmuletType::NONE;
}

} // namespace

SkillRegistry& SkillRegistry::instance() {
    static SkillRegistry registry;
    return registry;
}

void SkillRegistry::register_skill(SkillTemplate skill) {
    std::unique_lock lock(mutex_);
    skills_[skill.id] = std::move(skill);
}

const SkillTemplate* SkillRegistry::get_skill(uint32_t id) const {
    std::shared_lock lock(mutex_);
    auto it = skills_.find(id);
    if (it == skills_.end()) {
        return nullptr;
    }
    return &it->second;
}

std::vector<const SkillTemplate*> SkillRegistry::get_skills_for_class(mir2::common::CharacterClass cls) const {
    std::vector<const SkillTemplate*> results;
    std::shared_lock lock(mutex_);
    results.reserve(skills_.size());
    for (const auto& entry : skills_) {
        const auto& skill = entry.second;
        if (skill.required_class == cls) {
            results.push_back(&skill);
        }
    }
    return results;
}

bool SkillRegistry::load_from_yaml(const std::string& path, std::string* error_out) {
    if (error_out) {
        error_out->clear();
    }
    auto set_error = [&](std::string message) {
        if (error_out) {
            *error_out = std::move(message);
        }
    };
    try {
        const YAML::Node root = YAML::LoadFile(path);
        const YAML::Node skills_node = root["skills"] ? root["skills"] : root;
        if (!skills_node || !skills_node.IsSequence()) {
            set_error("Skill registry load failed: missing or invalid skills node.");
            return false;
        }

        std::vector<SkillTemplate> loaded_skills;
        loaded_skills.reserve(skills_node.size());
        for (const auto& node : skills_node) {
            if (!node || !node.IsMap()) {
                continue;
            }

            SkillTemplate skill;
            if (node["id"]) {
                skill.id = node["id"].as<uint32_t>();
            }
            if (skill.id == 0) {
                continue;
            }

            if (node["name"]) {
                skill.name = node["name"].as<std::string>();
            }
            if (node["description"]) {
                skill.description = node["description"].as<std::string>();
            }

            if (node["required_class"] || node["profession"]) {
                const YAML::Node class_node = node["required_class"] ? node["required_class"] : node["profession"];
                skill.required_class = ParseCharacterClass(class_node);
            }
            if (node["required_level"]) {
                skill.required_level = static_cast<uint8_t>(node["required_level"].as<int>());
            }
            if (node["max_level"]) {
                skill.max_level = static_cast<uint8_t>(node["max_level"].as<int>());
            }

            ReadSequence(node, "train_level_req", &skill.train_level_req);
            ReadSequence(node, "train_points_req", &skill.train_points_req);

            if (node["skill_type"] || node["damage_type"]) {
                const YAML::Node type_node = node["skill_type"] ? node["skill_type"] : node["damage_type"];
                skill.skill_type = ParseSkillType(type_node);
            }
            if (node["target_type"]) {
                skill.target_type = ParseSkillTarget(node["target_type"]);
            }
            if (node["is_passive"]) {
                skill.is_passive = node["is_passive"].as<bool>();
            }

            ReadScalarOrSequenceFirst(node, "mp_cost", &skill.mp_cost);
            if (node["consumes_talisman"]) {
                skill.consumes_talisman = node["consumes_talisman"].as<bool>();
            }
            if (node["talisman_cost"]) {
                skill.talisman_cost = node["talisman_cost"].as<int>();
            }
            if (node["required_amulet"]) {
                skill.required_amulet = ParseAmuletType(node["required_amulet"]);
            }
            if (node["amulet_cost"]) {
                skill.amulet_cost = node["amulet_cost"].as<int>();
            }

            if (!ReadScalar(node, "cooldown_ms", &skill.cooldown_ms)) {
                double cooldown_seconds = 0.0;
                if (ReadScalarOrSequenceFirst(node, "cooldown", &cooldown_seconds)) {
                    skill.cooldown_ms = static_cast<int>(cooldown_seconds * 1000.0);
                }
            }

            if (!ReadScalar(node, "cast_time_ms", &skill.cast_time_ms)) {
                double cast_seconds = 0.0;
                if (ReadScalarOrSequenceFirst(node, "cast_time", &cast_seconds)) {
                    skill.cast_time_ms = static_cast<int>(cast_seconds * 1000.0);
                }
            }

            if (node["can_be_interrupted"]) {
                skill.can_be_interrupted = node["can_be_interrupted"].as<bool>();
            }

            ReadScalar(node, "range", &skill.range);
            ReadScalar(node, "aoe_radius", &skill.aoe_radius);
            ReadScalar(node, "min_power", &skill.min_power);
            ReadScalar(node, "max_power", &skill.max_power);
            ReadScalar(node, "def_power", &skill.def_power);
            ReadScalar(node, "def_max_power", &skill.def_max_power);
            int train_level = 0;
            if (ReadScalar(node, "train_lv", &train_level)) {
                skill.train_lv = static_cast<uint8_t>(train_level);
            }
            ReadScalar(node, "stat_modifier", &skill.stat_modifier);
            ReadScalar(node, "dot_damage", &skill.dot_damage);

            if (!ReadScalar(node, "dot_interval_ms", &skill.dot_interval_ms)) {
                double dot_seconds = 0.0;
                if (ReadScalarOrSequenceFirst(node, "dot_interval", &dot_seconds)) {
                    skill.dot_interval_ms = static_cast<int>(dot_seconds * 1000.0);
                }
            }

            if (node["duration_ms"]) {
                skill.duration_ms = node["duration_ms"].as<int>();
            } else {
                double duration_seconds = 0.0;
                if (ReadScalarOrSequenceFirst(node, "duration", &duration_seconds)) {
                    skill.duration_ms = static_cast<int>(duration_seconds * 1000.0);
                }
            }

            if (node["effect_type"]) {
                skill.effect_type = static_cast<uint8_t>(node["effect_type"].as<int>());
            }
            if (node["effect_id"]) {
                skill.effect_id = static_cast<uint8_t>(node["effect_id"].as<int>());
            }
            if (node["animation_id"]) {
                skill.animation_id = node["animation_id"].as<std::string>();
            }
            if (node["sound_id"]) {
                skill.sound_id = node["sound_id"].as<std::string>();
            }

            loaded_skills.push_back(std::move(skill));
        }

        if (!loaded_skills.empty()) {
            std::unique_lock lock(mutex_);
            for (auto& skill : loaded_skills) {
                skills_[skill.id] = std::move(skill);
            }
        }
    } catch (const std::exception& ex) {
        const std::string message = std::string("Skill registry load failed: ") + ex.what();
        set_error(message);
        std::cerr << message << std::endl;
        return false;
    }
    return true;
}

void SkillRegistry::clear() {
    std::unique_lock lock(mutex_);
    skills_.clear();
}

size_t SkillRegistry::size() const {
    std::shared_lock lock(mutex_);
    return skills_.size();
}

} // namespace mir2::ecs
