/**
 * @file skill_registry.cc
 * @brief 技能模板注册表实现
 */

#include "ecs/skill_registry.h"

#include "config/runtime_config.h"

#include <mutex>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <utility>

namespace mir2::ecs {

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

std::vector<const SkillTemplate*> SkillRegistry::get_skills_for_class(
    mir2::common::CharacterClass cls) const {
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

SkillRegistry::ReplaceResult SkillRegistry::ReplaceAllFromConfigData(
    const mir2::config::ConfigData& config_data) {
    ReplaceResult result;
    result.generated_at = config_data.manifest.generated_at;

    const auto* artifact = config_data.manifest.FindArtifact("skills");
    result.artifact_present = artifact != nullptr;
    if (artifact != nullptr) {
        result.artifact_file = artifact->file;
        result.artifact_hash = artifact->hash;
    }

    std::unordered_map<uint32_t, SkillTemplate> next_skills;
    if (result.artifact_present) {
        next_skills = config_data.skills;
        for (const auto& [id, skill] : next_skills) {
            if (skill.id != id) {
                result.error_message =
                    "skill registry replace failed: snapshot key/id mismatch for " +
                    std::to_string(id);
                return result;
            }
        }
    }

    {
        std::unique_lock lock(mutex_);
        skills_.swap(next_skills);
        result.loaded_skill_count = skills_.size();
    }

    result.success = true;
    return result;
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
