/**
 * @file map_config_loader.cc
 * @brief 地图配置加载器实现
 */

#include "config/map_config_loader.h"

#include <filesystem>

#include <yaml-cpp/yaml.h>

namespace mir2::config {

namespace {

template <typename T>
T ReadOrDefault(const YAML::Node& node, const char* key, const T& default_value) {
    if (node && node[key]) {
        try {
            return node[key].as<T>();
        } catch (const std::exception&) {
            return default_value;
        }
    }
    return default_value;
}

std::string ReadString(const YAML::Node& node,
                       const char* key,
                       const std::string& default_value) {
    if (!node || !node[key]) {
        return default_value;
    }

    const YAML::Node value = node[key];
    if (!value.IsScalar()) {
        return default_value;
    }

    try {
        return value.as<std::string>();
    } catch (const std::exception&) {
        try {
            return std::to_string(value.as<int64_t>());
        } catch (const std::exception&) {
            return default_value;
        }
    }
}

void LoadQuestRequirements(const YAML::Node& node,
                           std::vector<mir2::game::map::QuestRequirement>& out) {
    out.clear();
    if (!node || !node.IsSequence()) {
        return;
    }
    for (const auto& item : node) {
        if (!item || !item.IsMap()) {
            continue;
        }
        mir2::game::map::QuestRequirement req;
        req.quest_id = ReadOrDefault(item, "quest_id", req.quest_id);
        if (req.quest_id == 0 && item["id"]) {
            req.quest_id = item["id"].as<int32_t>();
        }
        req.quest_value = ReadOrDefault(item, "quest_value", req.quest_value);
        if (req.quest_value == 0 && item["value"]) {
            req.quest_value = item["value"].as<int32_t>();
        }
        if (req.quest_id <= 0) {
            continue;
        }
        out.push_back(req);
    }
}

void LoadSafeZones(const YAML::Node& node,
                   std::vector<mir2::game::map::SafeZone>& out) {
    out.clear();
    if (!node || !node.IsSequence()) {
        return;
    }
    for (const auto& item : node) {
        if (!item) {
            continue;
        }
        mir2::game::map::SafeZone zone;
        if (item.IsSequence()) {
            if (item.size() > 0) {
                zone.x = item[0].as<int32_t>();
            }
            if (item.size() > 1) {
                zone.y = item[1].as<int32_t>();
            }
            if (item.size() > 2) {
                zone.radius = item[2].as<int32_t>();
            }
            out.push_back(zone);
            continue;
        }

        if (!item.IsMap()) {
            continue;
        }
        zone.x = ReadOrDefault(item, "x", zone.x);
        zone.y = ReadOrDefault(item, "y", zone.y);
        zone.radius = ReadOrDefault(item, "radius", zone.radius);
        out.push_back(zone);
    }
}

void LoadGates(const YAML::Node& node,
               std::vector<mir2::game::map::GateInfo>& out,
               const std::string& map_id_string) {
    out.clear();
    if (!node || !node.IsSequence()) {
        return;
    }
    for (const auto& item : node) {
        if (!item || !item.IsMap()) {
            continue;
        }
        mir2::game::map::GateInfo gate{};
        gate.gate_id = ReadOrDefault(item, "id", gate.gate_id);
        if (gate.gate_id == 0 && item["gate_id"]) {
            gate.gate_id = item["gate_id"].as<uint32_t>();
        }
        gate.source_map = ReadString(item, "source_map", map_id_string);
        gate.source_x = ReadOrDefault(item, "source_x", gate.source_x);
        gate.source_y = ReadOrDefault(item, "source_y", gate.source_y);
        gate.target_map = ReadString(item, "target_map", gate.target_map);
        gate.target_x = ReadOrDefault(item, "target_x", gate.target_x);
        gate.target_y = ReadOrDefault(item, "target_y", gate.target_y);
        gate.require_item = ReadOrDefault(item, "require_item", gate.require_item);
        gate.required_item_id = ReadOrDefault(item, "required_item_id", gate.required_item_id);

        if (gate.source_map.empty()) {
            gate.source_map = map_id_string;
        }
        if (gate.source_map.empty() || gate.target_map.empty()) {
            continue;
        }
        out.push_back(gate);
    }
}

void LoadFixes(const YAML::Node& node,
               std::vector<std::pair<int32_t, int32_t>>& out) {
    out.clear();
    if (!node || !node.IsSequence()) {
        return;
    }
    for (const auto& item : node) {
        if (!item) {
            continue;
        }
        if (item.IsSequence()) {
            int32_t x = 0;
            int32_t y = 0;
            if (item.size() > 0) {
                x = item[0].as<int32_t>();
            }
            if (item.size() > 1) {
                y = item[1].as<int32_t>();
            }
            out.emplace_back(x, y);
            continue;
        }
        if (!item.IsMap()) {
            continue;
        }
        const int32_t x = ReadOrDefault(item, "x", 0);
        const int32_t y = ReadOrDefault(item, "y", 0);
        out.emplace_back(x, y);
    }
}

mir2::game::map::MapAttributes LoadAttributes(const YAML::Node& node) {
    mir2::game::map::MapAttributes attrs;
    if (!node || !node.IsMap()) {
        return attrs;
    }

    attrs.is_safe_zone = ReadOrDefault(node, "is_safe_zone", attrs.is_safe_zone);
    attrs.is_pk_zone = ReadOrDefault(node, "is_pk_zone", attrs.is_pk_zone);
    attrs.no_teleport = ReadOrDefault(node, "no_teleport", attrs.no_teleport);
    attrs.no_drug = ReadOrDefault(node, "no_drug", attrs.no_drug);
    attrs.is_dark_map = ReadOrDefault(node, "is_dark_map", attrs.is_dark_map);
    attrs.no_recall = ReadOrDefault(node, "no_recall", attrs.no_recall);
    attrs.no_random_move = ReadOrDefault(node, "no_random_move", attrs.no_random_move);
    attrs.fight_zone = ReadOrDefault(node, "fight_zone", attrs.fight_zone);
    attrs.fight3_zone = ReadOrDefault(node, "fight3_zone", attrs.fight3_zone);
    attrs.min_level = ReadOrDefault(node, "min_level", attrs.min_level);
    attrs.max_level = ReadOrDefault(node, "max_level", attrs.max_level);
    attrs.mine_map = ReadOrDefault(node, "mine_map", attrs.mine_map);
    attrs.dark_level = ReadOrDefault(node, "dark_level", attrs.dark_level);
    attrs.exp_rate = ReadOrDefault(node, "exp_rate", attrs.exp_rate);
    attrs.drop_rate = ReadOrDefault(node, "drop_rate", attrs.drop_rate);
    attrs.home_map = ReadString(node, "home_map", attrs.home_map);
    attrs.home_x = ReadOrDefault(node, "home_x", attrs.home_x);
    attrs.home_y = ReadOrDefault(node, "home_y", attrs.home_y);
    attrs.pk_village_map = ReadString(node, "pk_village_map", attrs.pk_village_map);
    attrs.pk_village_x = ReadOrDefault(node, "pk_village_x", attrs.pk_village_x);
    attrs.pk_village_y = ReadOrDefault(node, "pk_village_y", attrs.pk_village_y);

    LoadQuestRequirements(node["quest_requirements"], attrs.quest_requirements);
    LoadSafeZones(node["safe_zones"], attrs.safe_zones);

    return attrs;
}

MapConfigLoader::MapConfig ParseMapNode(const YAML::Node& node) {
    MapConfigLoader::MapConfig config;
    config.map_id = ReadOrDefault(node, "id", config.map_id);
    if (config.map_id == 0 && node["map_id"]) {
        config.map_id = node["map_id"].as<int32_t>();
    }
    const std::string map_id_string = std::to_string(config.map_id);

    config.attributes = LoadAttributes(node["attributes"] ? node["attributes"] : node);
    LoadFixes(node["fixes"], config.fixes);
    LoadGates(node["gates"], config.gates, map_id_string);
    return config;
}

}  // namespace

std::optional<MapConfigLoader::MapConfig> MapConfigLoader::LoadMapConfig(
    const std::string& config_path) {
    if (config_path.empty() || !std::filesystem::exists(config_path)) {
        return std::nullopt;
    }

    try {
        YAML::Node root = YAML::LoadFile(config_path);
        YAML::Node map_node = root["map"] ? root["map"] : root;
        if (!map_node || !map_node.IsMap()) {
            return std::nullopt;
        }
        if (!map_node["id"] && !map_node["map_id"]) {
            return std::nullopt;
        }

        MapConfig config = ParseMapNode(map_node);
        if (config.map_id < 0) {
            return std::nullopt;
        }
        return config;
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

std::vector<MapConfigLoader::MapConfig> MapConfigLoader::LoadAllMapConfigs(
    const std::string& config_dir) {
    std::vector<MapConfig> results;
    if (config_dir.empty() || !std::filesystem::exists(config_dir)) {
        return results;
    }

    std::filesystem::path maps_path = std::filesystem::path(config_dir) / "maps.yaml";
    if (std::filesystem::exists(maps_path)) {
        try {
            YAML::Node root = YAML::LoadFile(maps_path.string());
            YAML::Node maps_node = root["maps"] ? root["maps"] : root;
            if (maps_node && maps_node.IsSequence()) {
                for (const auto& map_node : maps_node) {
                    if (!map_node || !map_node.IsMap()) {
                        continue;
                    }
                    if (!map_node["id"] && !map_node["map_id"]) {
                        continue;
                    }
                    MapConfig config = ParseMapNode(map_node);
                    if (config.map_id < 0) {
                        continue;
                    }
                    results.push_back(std::move(config));
                }
            }
        } catch (const std::exception&) {
            // ignore parse errors for maps.yaml
        }
        return results;
    }

    for (const auto& entry : std::filesystem::directory_iterator(config_dir)) {
        if (!entry.is_regular_file()) {
            continue;
        }
        const auto& path = entry.path();
        if (path.extension() != ".yaml" && path.extension() != ".yml") {
            continue;
        }
        auto loaded = LoadMapConfig(path.string());
        if (loaded) {
            results.push_back(std::move(*loaded));
        }
    }
    return results;
}

}  // namespace mir2::config
