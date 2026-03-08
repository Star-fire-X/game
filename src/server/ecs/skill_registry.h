/**
 * @file skill_registry.h
 * @brief 技能模板注册表
 */

#ifndef MIR2_SERVER_ECS_SKILL_REGISTRY_H_
#define MIR2_SERVER_ECS_SKILL_REGISTRY_H_

#include "ecs/components/skill_template_component.h"

#include <cstddef>
#include <cstdint>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace mir2::config {
struct ConfigData;
}

namespace mir2::ecs {

/**
 * @brief 技能模板注册表
 *
 * 负责加载、注册与查询技能模板数据。
 */
class SkillRegistry {
public:
    struct ReplaceResult {
        bool success = false;
        size_t loaded_skill_count = 0;
        bool artifact_present = false;
        std::string artifact_file;
        std::string artifact_hash;
        std::string generated_at;
        std::string error_message;
    };

    /// 获取全局单例
    static SkillRegistry& instance();

    /// 注册技能模板（同 ID 会覆盖）
    void register_skill(SkillTemplate skill);

    /// 根据 ID 获取技能模板
    const SkillTemplate* get_skill(uint32_t id) const;

    /// 获取指定职业可用技能模板
    std::vector<const SkillTemplate*> get_skills_for_class(mir2::common::CharacterClass cls) const;

    /// 用 runtime config 快照全量替换 registry 内容
    ReplaceResult ReplaceAllFromConfigData(const mir2::config::ConfigData& config_data);

    /// 清空所有技能模板
    void clear();

    /// 获取技能模板数量
    size_t size() const;

private:
    SkillRegistry() = default;
    SkillRegistry(const SkillRegistry&) = delete;
    SkillRegistry& operator=(const SkillRegistry&) = delete;

    mutable std::shared_mutex mutex_;
    std::unordered_map<uint32_t, SkillTemplate> skills_;
};

} // namespace mir2::ecs

#endif // MIR2_SERVER_ECS_SKILL_REGISTRY_H_
