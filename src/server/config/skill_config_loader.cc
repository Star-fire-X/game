/**
 * @file skill_config_loader.cc
 * @brief 技能配置加载器实现
 */

#include "config/skill_config_loader.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <iostream>
#include <string>
#include <unordered_set>
#include <utility>

#include <yaml-cpp/yaml.h>

namespace mir2::config {

namespace {

std::string ToLower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
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
        if (value >= 0 && value <= 2) {
            return static_cast<mir2::common::CharacterClass>(value);
        }
        if (value >= 1 && value <= 3) {
            return static_cast<mir2::common::CharacterClass>(value - 1);
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
    if (lowered == "single_enemy" || lowered == "enemy") {
        return mir2::common::SkillTarget::SINGLE_ENEMY;
    }
    if (lowered == "single_ally" || lowered == "ally") {
        return mir2::common::SkillTarget::SINGLE_ALLY;
    }
    if (lowered == "aoe_enemy" || lowered == "enemy_aoe") {
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

bool HasRequiredFields(const YAML::Node& node, std::vector<std::string>* missing) {
    const char* required_keys[] = {
        "id",
        "name",
        "description",
        "skill_type",
        "target_type",
        "required_class",
        "required_level",
        "is_passive",
        "mp_cost",
        "cooldown_ms",
        "range",
        "min_power",
        "max_power",
        "train_level_req",
        "train_points_req",
    };

    bool ok = true;
    for (const char* key : required_keys) {
        if (!node[key]) {
            ok = false;
            if (missing != nullptr) {
                missing->emplace_back(key);
            }
        }
    }
    return ok;
}

} // namespace

std::vector<mir2::ecs::SkillTemplate> SkillConfigLoader::load_from_yaml(const std::string& path) {
    std::vector<mir2::ecs::SkillTemplate> skills;

    try {
        const YAML::Node root = YAML::LoadFile(path);
        const YAML::Node skills_node = root["skills"];
        if (!skills_node || !skills_node.IsSequence()) {
            return skills;
        }

        skills.reserve(skills_node.size());

        for (const auto& node : skills_node) {
            if (!node || !node.IsMap()) {
                continue;
            }

            std::vector<std::string> missing;
            if (!HasRequiredFields(node, &missing)) {
                std::cerr << "Skill config missing fields:";
                for (const auto& key : missing) {
                    std::cerr << ' ' << key;
                }
                std::cerr << std::endl;
                continue;
            }

            mir2::ecs::SkillTemplate skill;
            skill.id = node["id"].as<uint32_t>();
            skill.name = node["name"].as<std::string>();
            skill.description = node["description"].as<std::string>();
            skill.skill_type = ParseSkillType(node["skill_type"]);
            skill.target_type = ParseSkillTarget(node["target_type"]);
            skill.required_class = ParseCharacterClass(node["required_class"]);
            skill.required_level = static_cast<uint8_t>(node["required_level"].as<int>());
            skill.is_passive = node["is_passive"].as<bool>();
            skill.mp_cost = node["mp_cost"].as<int>();
            skill.cooldown_ms = node["cooldown_ms"].as<int>();
            skill.range = node["range"].as<float>();
            skill.min_power = node["min_power"].as<int>();
            skill.max_power = node["max_power"].as<int>();
            if (node["required_amulet"]) {
                skill.required_amulet = ParseAmuletType(node["required_amulet"]);
            }
            if (node["amulet_cost"]) {
                skill.amulet_cost = node["amulet_cost"].as<int>();
            }

            ReadSequence(node, "train_level_req", &skill.train_level_req);
            ReadSequence(node, "train_points_req", &skill.train_points_req);

            skills.push_back(std::move(skill));
        }
    } catch (const std::exception& ex) {
        std::cerr << "Skill config load failed: " << ex.what() << std::endl;
    }

    return skills;
}

bool SkillConfigLoader::validate(const std::vector<mir2::ecs::SkillTemplate>& skills) {
    bool ok = true;
    std::unordered_set<uint32_t> ids;

    for (const auto& skill : skills) {
        if (skill.id == 0) {
            std::cerr << "Skill config validation failed: invalid id 0." << std::endl;
            ok = false;
        }
        if (skill.name.empty()) {
            std::cerr << "Skill config validation failed: missing name for id " << skill.id << "." << std::endl;
            ok = false;
        }
        if (skill.description.empty()) {
            std::cerr << "Skill config validation failed: missing description for id " << skill.id << "."
                      << std::endl;
            ok = false;
        }
        if (skill.required_level == 0) {
            std::cerr << "Skill config validation failed: invalid required_level for id " << skill.id << "."
                      << std::endl;
            ok = false;
        }
        const int class_value = static_cast<int>(skill.required_class);
        if (class_value < 0 || class_value > static_cast<int>(mir2::common::CharacterClass::TAOIST)) {
            std::cerr << "Skill config validation failed: invalid required_class for id " << skill.id << "."
                      << std::endl;
            ok = false;
        }
        const int type_value = static_cast<int>(skill.skill_type);
        if (type_value < 0 || type_value > static_cast<int>(mir2::common::SkillType::HEAL)) {
            std::cerr << "Skill config validation failed: invalid skill_type for id " << skill.id << "."
                      << std::endl;
            ok = false;
        }
        const int target_value = static_cast<int>(skill.target_type);
        if (target_value < 0 || target_value > static_cast<int>(mir2::common::SkillTarget::AOE_ALL)) {
            std::cerr << "Skill config validation failed: invalid target_type for id " << skill.id << "."
                      << std::endl;
            ok = false;
        }
        if (skill.mp_cost < 0 || skill.cooldown_ms < 0) {
            std::cerr << "Skill config validation failed: negative cost/cooldown for id " << skill.id << "."
                      << std::endl;
            ok = false;
        }
        if (skill.amulet_cost < 0) {
            std::cerr << "Skill config validation failed: negative amulet_cost for id " << skill.id << "."
                      << std::endl;
            ok = false;
        }
        if (skill.range < 0.0f) {
            std::cerr << "Skill config validation failed: negative range for id " << skill.id << "."
                      << std::endl;
            ok = false;
        }
        if (skill.min_power > skill.max_power) {
            std::cerr << "Skill config validation failed: min_power > max_power for id " << skill.id << "."
                      << std::endl;
            ok = false;
        }
        if (!ids.insert(skill.id).second) {
            std::cerr << "Skill config validation failed: duplicate id " << skill.id << "." << std::endl;
            ok = false;
        }
    }

    return ok;
}

} // namespace mir2::config
