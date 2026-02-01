/**
 * @file map_config_loader.h
 * @brief 地图配置加载器
 */

#ifndef LEGEND2_SERVER_MAP_CONFIG_LOADER_H
#define LEGEND2_SERVER_MAP_CONFIG_LOADER_H

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "game/map/gate_manager.h"
#include "game/map/map_attributes.h"

namespace mir2::config {

class MapConfigLoader {
public:
    struct MapConfig {
        int32_t map_id = 0;
        game::map::MapAttributes attributes;
        std::vector<game::map::GateInfo> gates;  // 该地图相关的传送门
        std::vector<std::pair<int32_t, int32_t>> fixes;  // 地图修正点坐标
    };

    static std::optional<MapConfig> LoadMapConfig(const std::string& config_path);
    static std::vector<MapConfig> LoadAllMapConfigs(const std::string& config_dir);
};

}  // namespace mir2::config

#endif  // LEGEND2_SERVER_MAP_CONFIG_LOADER_H
