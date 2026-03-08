/**
 * @file client_config_test.cc
 * @brief Unit tests for the client configuration loader and saver.
 *
 * Validates default construction, YAML round-trip serialization, partial
 * and malformed input handling, directory creation on save, and correct
 * parsing of all six configuration sections.
 */

#include "client/config/client_config.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>

namespace {

namespace fs = std::filesystem;

using mir2::client::config::ClientConfig;
using mir2::client::config::load_client_config;
using mir2::client::config::save_client_config;

// ---------------------------------------------------------------------------
// Test fixture -- manages a per-test temporary directory for file I/O.
// ---------------------------------------------------------------------------
class ClientConfigTest : public ::testing::Test {
protected:
    void SetUp() override {
        const auto* info = ::testing::UnitTest::GetInstance()->current_test_info();
        std::string dir_name = std::string("client_config_test_") +
                               info->test_case_name() + "_" + info->name();
        tmp_dir_ = fs::temp_directory_path() / dir_name;

        if (fs::exists(tmp_dir_)) {
            fs::remove_all(tmp_dir_);
        }
        fs::create_directories(tmp_dir_);
    }

    void TearDown() override {
        std::error_code ec;
        fs::remove_all(tmp_dir_, ec);
    }

    std::string TmpFile(const std::string& name) const {
        return (tmp_dir_ / name).string();
    }

    void WriteRawYaml(const std::string& name, const std::string& content) {
        const auto path = tmp_dir_ / name;
        fs::create_directories(path.parent_path());
        std::ofstream out(path);
        out << content;
    }

    fs::path tmp_dir_;
};

// ---------------------------------------------------------------------------
// 1. DefaultValues
// ---------------------------------------------------------------------------
TEST_F(ClientConfigTest, DefaultValues) {
    const ClientConfig cfg;

    EXPECT_EQ(cfg.server_host, "127.0.0.1");
    EXPECT_EQ(cfg.server_port, 7000);
    EXPECT_EQ(cfg.auto_connect, false);
    EXPECT_EQ(cfg.use_v2_protocol, true);

    EXPECT_EQ(cfg.window_width, 800);
    EXPECT_EQ(cfg.window_height, 600);
    EXPECT_EQ(cfg.fullscreen, false);
    EXPECT_EQ(cfg.vsync, true);
    EXPECT_EQ(cfg.window_title, "Legend2");

    EXPECT_FLOAT_EQ(cfg.music_volume, 0.8f);
    EXPECT_FLOAT_EQ(cfg.sfx_volume, 1.0f);
    EXPECT_EQ(cfg.music_enabled, true);
    EXPECT_EQ(cfg.sfx_enabled, true);

    EXPECT_EQ(cfg.data_path, "Data");
    EXPECT_EQ(cfg.map_path, "Map");
    EXPECT_EQ(cfg.sound_path, "Wav");
    EXPECT_EQ(cfg.music_path, "MUSIC");
    EXPECT_EQ(cfg.font_path, "Data/fonts");

    EXPECT_EQ(cfg.log_path, "logs/client");
    EXPECT_EQ(cfg.log_level, "info");

    EXPECT_EQ(cfg.show_fps, false);
    EXPECT_EQ(cfg.show_minimap, true);
    EXPECT_FLOAT_EQ(cfg.ui_scale, 1.0f);
}

// ---------------------------------------------------------------------------
// 2. LoadMissingFile
// ---------------------------------------------------------------------------
TEST_F(ClientConfigTest, LoadMissingFile) {
    const ClientConfig cfg = load_client_config(TmpFile("does_not_exist.yaml"));

    EXPECT_EQ(cfg.server_host, "127.0.0.1");
    EXPECT_EQ(cfg.server_port, 7000);
    EXPECT_EQ(cfg.use_v2_protocol, true);
    EXPECT_EQ(cfg.window_width, 800);
    EXPECT_EQ(cfg.window_height, 600);
    EXPECT_FLOAT_EQ(cfg.music_volume, 0.8f);
    EXPECT_EQ(cfg.data_path, "Data");
    EXPECT_EQ(cfg.log_level, "info");
    EXPECT_FLOAT_EQ(cfg.ui_scale, 1.0f);
}

// ---------------------------------------------------------------------------
// 3. RoundTrip
// ---------------------------------------------------------------------------
TEST_F(ClientConfigTest, RoundTrip) {
    ClientConfig original;
    original.server_host  = "192.168.1.100";
    original.server_port  = 9999;
    original.auto_connect = true;
    original.use_v2_protocol = false;
    original.window_width  = 1920;
    original.window_height = 1080;
    original.fullscreen    = true;
    original.vsync         = false;
    original.window_title  = "Mir2 Test";
    original.music_volume  = 0.5f;
    original.sfx_volume    = 0.75f;
    original.music_enabled = false;
    original.sfx_enabled   = false;
    original.data_path  = "/opt/game/data";
    original.map_path   = "/opt/game/maps";
    original.sound_path = "/opt/game/sfx";
    original.music_path = "/opt/game/music";
    original.font_path  = "/opt/game/fonts";
    original.log_path  = "/var/log/mir2";
    original.log_level = "debug";
    original.show_fps     = true;
    original.show_minimap = false;
    original.ui_scale     = 2.0f;

    const std::string path = TmpFile("roundtrip.yaml");
    save_client_config(original, path);
    ASSERT_TRUE(fs::exists(path));

    const ClientConfig loaded = load_client_config(path);

    EXPECT_EQ(loaded.server_host, "192.168.1.100");
    EXPECT_EQ(loaded.server_port, 9999);
    EXPECT_EQ(loaded.auto_connect, true);
    EXPECT_EQ(loaded.use_v2_protocol, false);
    EXPECT_EQ(loaded.window_width, 1920);
    EXPECT_EQ(loaded.window_height, 1080);
    EXPECT_EQ(loaded.fullscreen, true);
    EXPECT_EQ(loaded.vsync, false);
    EXPECT_EQ(loaded.window_title, "Mir2 Test");
    EXPECT_FLOAT_EQ(loaded.music_volume, 0.5f);
    EXPECT_FLOAT_EQ(loaded.sfx_volume, 0.75f);
    EXPECT_EQ(loaded.music_enabled, false);
    EXPECT_EQ(loaded.sfx_enabled, false);
    EXPECT_EQ(loaded.data_path, "/opt/game/data");
    EXPECT_EQ(loaded.map_path, "/opt/game/maps");
    EXPECT_EQ(loaded.sound_path, "/opt/game/sfx");
    EXPECT_EQ(loaded.music_path, "/opt/game/music");
    EXPECT_EQ(loaded.font_path, "/opt/game/fonts");
    EXPECT_EQ(loaded.log_path, "/var/log/mir2");
    EXPECT_EQ(loaded.log_level, "debug");
    EXPECT_EQ(loaded.show_fps, true);
    EXPECT_EQ(loaded.show_minimap, false);
    EXPECT_FLOAT_EQ(loaded.ui_scale, 2.0f);
}

// ---------------------------------------------------------------------------
// 4. PartialYAML
// ---------------------------------------------------------------------------
TEST_F(ClientConfigTest, PartialYAML) {
    WriteRawYaml("partial.yaml",
        "network:\n"
        "  host: \"10.0.0.1\"\n"
        "  port: 8080\n"
        "  auto_connect: true\n"
        "  use_v2_protocol: false\n");

    const ClientConfig cfg = load_client_config(TmpFile("partial.yaml"));

    EXPECT_EQ(cfg.server_host, "10.0.0.1");
    EXPECT_EQ(cfg.server_port, 8080);
    EXPECT_EQ(cfg.auto_connect, true);
    EXPECT_EQ(cfg.use_v2_protocol, false);

    EXPECT_EQ(cfg.window_width, 800);
    EXPECT_EQ(cfg.window_height, 600);
    EXPECT_EQ(cfg.fullscreen, false);
    EXPECT_EQ(cfg.vsync, true);
    EXPECT_EQ(cfg.window_title, "Legend2");
    EXPECT_FLOAT_EQ(cfg.music_volume, 0.8f);
    EXPECT_FLOAT_EQ(cfg.sfx_volume, 1.0f);
    EXPECT_EQ(cfg.data_path, "Data");
    EXPECT_EQ(cfg.log_level, "info");
    EXPECT_FLOAT_EQ(cfg.ui_scale, 1.0f);
}

// ---------------------------------------------------------------------------
// 5. MalformedValues
// ---------------------------------------------------------------------------
TEST_F(ClientConfigTest, MalformedValues) {
    WriteRawYaml("malformed.yaml",
        "network:\n"
        "  host: \"good_host\"\n"
        "  port: not_a_number\n"
        "  auto_connect: maybe\n"
        "window:\n"
        "  width: wide\n"
        "  height: tall\n"
        "audio:\n"
        "  music_volume: loud\n"
        "gameplay:\n"
        "  ui_scale: big\n");

    const ClientConfig cfg = load_client_config(TmpFile("malformed.yaml"));

    EXPECT_EQ(cfg.server_host, "good_host");
    EXPECT_EQ(cfg.server_port, 7000);
    EXPECT_EQ(cfg.auto_connect, false);
    EXPECT_EQ(cfg.use_v2_protocol, true);
    EXPECT_EQ(cfg.window_width, 800);
    EXPECT_EQ(cfg.window_height, 600);
    EXPECT_FLOAT_EQ(cfg.music_volume, 0.8f);
    EXPECT_FLOAT_EQ(cfg.ui_scale, 1.0f);
}

// ---------------------------------------------------------------------------
// 6. EmptyFile
// ---------------------------------------------------------------------------
TEST_F(ClientConfigTest, EmptyFile) {
    WriteRawYaml("empty.yaml", "");

    const ClientConfig cfg = load_client_config(TmpFile("empty.yaml"));

    EXPECT_EQ(cfg.server_host, "127.0.0.1");
    EXPECT_EQ(cfg.server_port, 7000);
    EXPECT_EQ(cfg.use_v2_protocol, true);
    EXPECT_EQ(cfg.window_width, 800);
    EXPECT_EQ(cfg.window_title, "Legend2");
    EXPECT_FLOAT_EQ(cfg.music_volume, 0.8f);
    EXPECT_EQ(cfg.data_path, "Data");
    EXPECT_EQ(cfg.log_level, "info");
    EXPECT_FLOAT_EQ(cfg.ui_scale, 1.0f);
}

// ---------------------------------------------------------------------------
// 7. SaveCreatesDirectories
// ---------------------------------------------------------------------------
TEST_F(ClientConfigTest, SaveCreatesDirectories) {
    const std::string nested_path =
        (tmp_dir_ / "a" / "b" / "c" / "config.yaml").string();

    ASSERT_FALSE(fs::exists(tmp_dir_ / "a"));

    const ClientConfig cfg;
    save_client_config(cfg, nested_path);

    EXPECT_TRUE(fs::exists(nested_path));

    const ClientConfig loaded = load_client_config(nested_path);
    EXPECT_EQ(loaded.server_host, "127.0.0.1");
    EXPECT_EQ(loaded.server_port, 7000);
}

// ---------------------------------------------------------------------------
// 8. AllSections
// ---------------------------------------------------------------------------
TEST_F(ClientConfigTest, AllSections) {
    WriteRawYaml("all_sections.yaml",
        "network:\n"
        "  host: \"203.0.113.50\"\n"
        "  port: 12345\n"
        "  auto_connect: true\n"
        "  use_v2_protocol: false\n"
        "window:\n"
        "  width: 1280\n"
        "  height: 720\n"
        "  fullscreen: true\n"
        "  vsync: false\n"
        "  title: \"All Sections Test\"\n"
        "audio:\n"
        "  music_volume: 0.3\n"
        "  sfx_volume: 0.6\n"
        "  music_enabled: false\n"
        "  sfx_enabled: false\n"
        "resources:\n"
        "  data: \"custom/data\"\n"
        "  map: \"custom/maps\"\n"
        "  sound: \"custom/sounds\"\n"
        "  music: \"custom/bgm\"\n"
        "  font: \"custom/fonts\"\n"
        "logging:\n"
        "  path: \"/tmp/mir2_test_log\"\n"
        "  level: \"trace\"\n"
        "gameplay:\n"
        "  show_fps: true\n"
        "  show_minimap: false\n"
        "  ui_scale: 1.5\n");

    const ClientConfig cfg = load_client_config(TmpFile("all_sections.yaml"));

    EXPECT_EQ(cfg.server_host, "203.0.113.50");
    EXPECT_EQ(cfg.server_port, 12345);
    EXPECT_EQ(cfg.auto_connect, true);
    EXPECT_EQ(cfg.use_v2_protocol, false);
    EXPECT_EQ(cfg.window_width, 1280);
    EXPECT_EQ(cfg.window_height, 720);
    EXPECT_EQ(cfg.fullscreen, true);
    EXPECT_EQ(cfg.vsync, false);
    EXPECT_EQ(cfg.window_title, "All Sections Test");
    EXPECT_NEAR(cfg.music_volume, 0.3f, 1e-5f);
    EXPECT_NEAR(cfg.sfx_volume, 0.6f, 1e-5f);
    EXPECT_EQ(cfg.music_enabled, false);
    EXPECT_EQ(cfg.sfx_enabled, false);
    EXPECT_EQ(cfg.data_path, "custom/data");
    EXPECT_EQ(cfg.map_path, "custom/maps");
    EXPECT_EQ(cfg.sound_path, "custom/sounds");
    EXPECT_EQ(cfg.music_path, "custom/bgm");
    EXPECT_EQ(cfg.font_path, "custom/fonts");
    EXPECT_EQ(cfg.log_path, "/tmp/mir2_test_log");
    EXPECT_EQ(cfg.log_level, "trace");
    EXPECT_EQ(cfg.show_fps, true);
    EXPECT_EQ(cfg.show_minimap, false);
    EXPECT_FLOAT_EQ(cfg.ui_scale, 1.5f);
}

}  // namespace
