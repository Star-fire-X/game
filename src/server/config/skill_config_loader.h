#ifndef LEGEND2_SERVER_SKILL_CONFIG_LOADER_H
#define LEGEND2_SERVER_SKILL_CONFIG_LOADER_H

#include "ecs/components/skill_template_component.h"
#include <string>
#include <vector>

namespace mir2::config {

class SkillConfigLoader {
public:
    std::vector<mir2::ecs::SkillTemplate> load_from_yaml(const std::string& path);
    bool validate(const std::vector<mir2::ecs::SkillTemplate>& skills);
};

} // namespace mir2::config
#endif
