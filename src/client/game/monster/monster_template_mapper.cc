/**
 * @file monster_template_mapper.cc
 * @brief Monster template id -> render configuration mapping.
 */

#include "client/game/monster/monster_template_mapper.h"

#include <filesystem>
#include <iostream>

#include <yaml-cpp/yaml.h>

namespace mir2::game::monster {

namespace {
constexpr ActorRace kDefaultRace = ActorRace::CHICKEN_DOG;
constexpr int kDefaultAppearance = 0;
} // namespace

MonsterTemplateMapper& MonsterTemplateMapper::instance() {
    static MonsterTemplateMapper mapper;
    return mapper;
}

bool MonsterTemplateMapper::load_from_file(const std::string& file_path) {
    if (file_path.empty()) {
        std::cerr << "Monster template path is empty." << std::endl;
        return false;
    }

    const std::filesystem::path path(file_path);
    if (!std::filesystem::exists(path)) {
        std::cerr << "Monster template file not found: " << path.string() << std::endl;
        return false;
    }

    try {
        const YAML::Node root = YAML::LoadFile(path.string());
        const YAML::Node monsters_node = root["monsters"] ? root["monsters"] : root;
        if (!monsters_node || !monsters_node.IsSequence()) {
            std::cerr << "Monster template file invalid: " << path.string() << std::endl;
            return false;
        }

        std::unordered_map<uint32_t, MonsterRenderInfo> loaded;
        loaded.reserve(monsters_node.size());

        for (const auto& node : monsters_node) {
            if (!node || !node.IsMap() || !node["id"]) {
                continue;
            }

            const uint32_t id = node["id"].as<uint32_t>();
            if (id == 0) {
                continue;
            }

            MonsterRenderInfo info;
            info.race = kDefaultRace;
            info.appearance = kDefaultAppearance;

            if (node["race"]) {
                info.race = static_cast<ActorRace>(node["race"].as<int>());
            }
            if (node["appearance"]) {
                info.appearance = node["appearance"].as<int>();
            }

            loaded[id] = info;
        }

        templates_ = std::move(loaded);
        return !templates_.empty();
    } catch (const std::exception& ex) {
        std::cerr << "Failed to load monster templates from " << path.string()
                  << ": " << ex.what() << std::endl;
        return false;
    }
}

MonsterRenderInfo MonsterTemplateMapper::get_render_info(uint32_t template_id) const {
    auto it = templates_.find(template_id);
    if (it != templates_.end()) {
        return it->second;
    }
    return {kDefaultRace, kDefaultAppearance};
}

bool MonsterTemplateMapper::has_template(uint32_t template_id) const {
    return templates_.find(template_id) != templates_.end();
}

} // namespace mir2::game::monster
