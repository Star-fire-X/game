/**
 * @file map_config_loader_test.cpp
 * @brief MapConfigLoader 单元测试
 */

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

#include "config/map_config_loader.h"

namespace {

class MapConfigLoaderTest : public ::testing::Test {
protected:
    void SetUp() override {
        auto base = std::filesystem::temp_directory_path();
        auto unique = std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count());
        temp_dir_ = base / ("mir2_map_config_test_" + unique);
        std::filesystem::create_directories(temp_dir_);
    }

    void TearDown() override {
        std::error_code ec;
        if (!temp_dir_.empty()) {
            std::filesystem::remove_all(temp_dir_, ec);
        }
    }

    std::filesystem::path WriteFile(const std::string& name, const std::string& contents) {
        auto path = temp_dir_ / name;
        std::ofstream out(path);
        out << contents;
        return path;
    }

    std::filesystem::path temp_dir_;
};

}  // namespace

TEST_F(MapConfigLoaderTest, LoadMapConfigParsesAttributesFixesAndGates) {
    const auto path = WriteFile(
        "map_5.yaml",
        R"(map:
  id: 5
  attributes:
    is_safe_zone: true
    no_teleport: true
    min_level: 10
    max_level: 20
    dark_level: 2
    exp_rate: 1.5
    safe_zones:
      - [10, 20, 5]
  fixes:
    - [100, 101]
  gates:
    - id: 7
      source_x: 10
      source_y: 11
      target_map: "2"
      target_x: 1
      target_y: 2
)");

    auto config = mir2::config::MapConfigLoader::LoadMapConfig(path.string());
    ASSERT_TRUE(config.has_value());
    EXPECT_EQ(config->map_id, 5);

    const auto& attrs = config->attributes;
    EXPECT_TRUE(attrs.is_safe_zone);
    EXPECT_TRUE(attrs.no_teleport);
    EXPECT_EQ(attrs.min_level, 10);
    EXPECT_EQ(attrs.max_level, 20);
    EXPECT_EQ(attrs.dark_level, 2);
    EXPECT_FLOAT_EQ(attrs.exp_rate, 1.5f);
    ASSERT_EQ(attrs.safe_zones.size(), 1u);
    EXPECT_EQ(attrs.safe_zones[0].x, 10);
    EXPECT_EQ(attrs.safe_zones[0].y, 20);
    EXPECT_EQ(attrs.safe_zones[0].radius, 5);

    ASSERT_EQ(config->fixes.size(), 1u);
    EXPECT_EQ(config->fixes[0].first, 100);
    EXPECT_EQ(config->fixes[0].second, 101);

    ASSERT_EQ(config->gates.size(), 1u);
    EXPECT_EQ(config->gates[0].gate_id, 7u);
    EXPECT_EQ(config->gates[0].source_map, "5");
    EXPECT_EQ(config->gates[0].source_x, 10);
    EXPECT_EQ(config->gates[0].source_y, 11);
    EXPECT_EQ(config->gates[0].target_map, "2");
    EXPECT_EQ(config->gates[0].target_x, 1);
    EXPECT_EQ(config->gates[0].target_y, 2);
}

TEST_F(MapConfigLoaderTest, LoadAllMapConfigsFromTablesFile) {
    WriteFile(
        "maps.yaml",
        R"(maps:
  - id: 1
    attributes:
      is_safe_zone: true
  - id: 3
    attributes:
      fight_zone: true
      dark_level: 2
    fixes:
      - [329, 332]
)"
    );

    auto configs = mir2::config::MapConfigLoader::LoadAllMapConfigs(temp_dir_.string());
    ASSERT_EQ(configs.size(), 2u);

    auto find_by_id = [&](int32_t map_id) {
        return std::find_if(configs.begin(), configs.end(), [&](const auto& cfg) {
            return cfg.map_id == map_id;
        });
    };

    auto map1 = find_by_id(1);
    ASSERT_NE(map1, configs.end());
    EXPECT_TRUE(map1->attributes.is_safe_zone);

    auto map3 = find_by_id(3);
    ASSERT_NE(map3, configs.end());
    EXPECT_TRUE(map3->attributes.fight_zone);
    EXPECT_EQ(map3->attributes.dark_level, 2);
    ASSERT_EQ(map3->fixes.size(), 1u);
    EXPECT_EQ(map3->fixes[0].first, 329);
    EXPECT_EQ(map3->fixes[0].second, 332);
}
