/**
 * @file client_config.h
 * @brief Client configuration loading and saving.
 *
 * Header-only module that defines the ClientConfig struct and provides
 * YAML-based load/save helpers.  When the config file does not exist the
 * loader silently returns a default-constructed ClientConfig so the client
 * can always start up with sensible values.
 */

#ifndef LEGEND2_CLIENT_CONFIG_CLIENT_CONFIG_H
#define LEGEND2_CLIENT_CONFIG_CLIENT_CONFIG_H

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

#include <yaml-cpp/yaml.h>

namespace mir2::client::config {

// ---------------------------------------------------------------------------
// Configuration struct
// ---------------------------------------------------------------------------

/**
 * @brief Aggregated client settings with sensible defaults.
 *
 * Every field carries a default value so that the client can operate even
 * when no configuration file is present.
 */
struct ClientConfig {
    // -- Network --------------------------------------------------------------
    std::string server_host  = "127.0.0.1";
    uint16_t    server_port  = 7000;
    bool        auto_connect = false;
    bool        use_v2_protocol = true;

    // -- Window ---------------------------------------------------------------
    int         window_width  = 800;
    int         window_height = 600;
    bool        fullscreen    = false;
    bool        vsync         = true;
    std::string window_title  = "Legend2";

    // -- Audio ----------------------------------------------------------------
    float music_volume  = 0.8f;
    float sfx_volume    = 1.0f;
    bool  music_enabled = true;
    bool  sfx_enabled   = true;

    // -- Resource paths -------------------------------------------------------
    std::string data_path  = "Data";
    std::string map_path   = "Map";
    std::string sound_path = "Wav";
    std::string music_path = "MUSIC";
    std::string font_path  = "Data/fonts";

    // -- Logging --------------------------------------------------------------
    std::string log_path  = "logs/client";
    std::string log_level = "info";

    // -- Gameplay -------------------------------------------------------------
    bool  show_fps     = false;
    bool  show_minimap = true;
    float ui_scale     = 1.0f;
};

// ---------------------------------------------------------------------------
// Internal helpers (inline, detail namespace to avoid ODR collisions)
// ---------------------------------------------------------------------------

namespace detail {

/**
 * @brief Read a typed value from a YAML node, falling back to @p default_value
 *        when the key is absent or the node is invalid.
 */
template <typename T>
inline T read_or_default(const YAML::Node& node,
                         const char* key,
                         const T& default_value) {
    if (node && node[key]) {
        try {
            return node[key].as<T>();
        } catch (const YAML::Exception&) {
            return default_value;
        }
    }
    return default_value;
}

}  // namespace detail

// ---------------------------------------------------------------------------
// Load
// ---------------------------------------------------------------------------

/**
 * @brief Load a ClientConfig from a YAML file.
 *
 * If the file at @p path does not exist or cannot be parsed the function
 * returns a default-constructed ClientConfig -- the caller is never forced
 * to handle an error.
 *
 * Expected YAML layout:
 * @code
 * network:
 *   host: "127.0.0.1"
 *   port: 7000
 *   auto_connect: false
 *   use_v2_protocol: true
 * window:
 *   width: 800
 *   height: 600
 *   fullscreen: false
 *   vsync: true
 *   title: "Legend2"
 * audio:
 *   music_volume: 0.8
 *   sfx_volume: 1.0
 *   music_enabled: true
 *   sfx_enabled: true
 * resources:
 *   data: "Data"
 *   map: "Map"
 *   sound: "Wav"
 *   music: "MUSIC"
 *   font: "Data/fonts"
 * logging:
 *   path: "logs/client"
 *   level: "info"
 * gameplay:
 *   show_fps: false
 *   show_minimap: true
 *   ui_scale: 1.0
 * @endcode
 */
inline ClientConfig load_client_config(const std::string& path) {
    ClientConfig cfg;

    if (!std::filesystem::exists(path)) {
        return cfg;
    }

    try {
        const YAML::Node root = YAML::LoadFile(path);

        // -- Network ----------------------------------------------------------
        const YAML::Node network = root["network"];
        cfg.server_host  = detail::read_or_default(network, "host",         cfg.server_host);
        cfg.server_port  = detail::read_or_default(network, "port",         cfg.server_port);
        cfg.auto_connect = detail::read_or_default(network, "auto_connect", cfg.auto_connect);
        cfg.use_v2_protocol =
            detail::read_or_default(network, "use_v2_protocol", cfg.use_v2_protocol);

        // -- Window -----------------------------------------------------------
        const YAML::Node window = root["window"];
        cfg.window_width  = detail::read_or_default(window, "width",      cfg.window_width);
        cfg.window_height = detail::read_or_default(window, "height",     cfg.window_height);
        cfg.fullscreen    = detail::read_or_default(window, "fullscreen", cfg.fullscreen);
        cfg.vsync         = detail::read_or_default(window, "vsync",      cfg.vsync);
        cfg.window_title  = detail::read_or_default(window, "title",      cfg.window_title);

        // -- Audio ------------------------------------------------------------
        const YAML::Node audio = root["audio"];
        cfg.music_volume  = detail::read_or_default(audio, "music_volume",  cfg.music_volume);
        cfg.sfx_volume    = detail::read_or_default(audio, "sfx_volume",    cfg.sfx_volume);
        cfg.music_enabled = detail::read_or_default(audio, "music_enabled", cfg.music_enabled);
        cfg.sfx_enabled   = detail::read_or_default(audio, "sfx_enabled",   cfg.sfx_enabled);

        // -- Resources --------------------------------------------------------
        const YAML::Node resources = root["resources"];
        cfg.data_path  = detail::read_or_default(resources, "data",  cfg.data_path);
        cfg.map_path   = detail::read_or_default(resources, "map",   cfg.map_path);
        cfg.sound_path = detail::read_or_default(resources, "sound", cfg.sound_path);
        cfg.music_path = detail::read_or_default(resources, "music", cfg.music_path);
        cfg.font_path  = detail::read_or_default(resources, "font",  cfg.font_path);

        // -- Logging ----------------------------------------------------------
        const YAML::Node logging = root["logging"];
        cfg.log_path  = detail::read_or_default(logging, "path",  cfg.log_path);
        cfg.log_level = detail::read_or_default(logging, "level", cfg.log_level);

        // -- Gameplay ---------------------------------------------------------
        const YAML::Node gameplay = root["gameplay"];
        cfg.show_fps     = detail::read_or_default(gameplay, "show_fps",     cfg.show_fps);
        cfg.show_minimap = detail::read_or_default(gameplay, "show_minimap", cfg.show_minimap);
        cfg.ui_scale     = detail::read_or_default(gameplay, "ui_scale",     cfg.ui_scale);

    } catch (const std::exception& ex) {
        std::cerr << "Failed to load client config (" << path << "): "
                  << ex.what() << std::endl;
    }

    return cfg;
}

// ---------------------------------------------------------------------------
// Save
// ---------------------------------------------------------------------------

/**
 * @brief Serialise a ClientConfig to a YAML file.
 *
 * Parent directories are created automatically when they do not yet exist.
 * Errors are reported to stderr but do not throw.
 */
inline void save_client_config(const ClientConfig& config,
                               const std::string& path) {
    try {
        // Ensure the parent directory tree exists.
        const auto parent = std::filesystem::path(path).parent_path();
        if (!parent.empty()) {
            std::filesystem::create_directories(parent);
        }

        YAML::Emitter out;
        out << YAML::BeginMap;

        // -- Network ----------------------------------------------------------
        out << YAML::Key << "network" << YAML::Value << YAML::BeginMap;
        out << YAML::Key << "host"         << YAML::Value << config.server_host;
        out << YAML::Key << "port"         << YAML::Value << config.server_port;
        out << YAML::Key << "auto_connect" << YAML::Value << config.auto_connect;
        out << YAML::Key << "use_v2_protocol" << YAML::Value << config.use_v2_protocol;
        out << YAML::EndMap;

        // -- Window -----------------------------------------------------------
        out << YAML::Key << "window" << YAML::Value << YAML::BeginMap;
        out << YAML::Key << "width"      << YAML::Value << config.window_width;
        out << YAML::Key << "height"     << YAML::Value << config.window_height;
        out << YAML::Key << "fullscreen" << YAML::Value << config.fullscreen;
        out << YAML::Key << "vsync"      << YAML::Value << config.vsync;
        out << YAML::Key << "title"      << YAML::Value << config.window_title;
        out << YAML::EndMap;

        // -- Audio ------------------------------------------------------------
        out << YAML::Key << "audio" << YAML::Value << YAML::BeginMap;
        out << YAML::Key << "music_volume"  << YAML::Value << config.music_volume;
        out << YAML::Key << "sfx_volume"    << YAML::Value << config.sfx_volume;
        out << YAML::Key << "music_enabled" << YAML::Value << config.music_enabled;
        out << YAML::Key << "sfx_enabled"   << YAML::Value << config.sfx_enabled;
        out << YAML::EndMap;

        // -- Resources --------------------------------------------------------
        out << YAML::Key << "resources" << YAML::Value << YAML::BeginMap;
        out << YAML::Key << "data"  << YAML::Value << config.data_path;
        out << YAML::Key << "map"   << YAML::Value << config.map_path;
        out << YAML::Key << "sound" << YAML::Value << config.sound_path;
        out << YAML::Key << "music" << YAML::Value << config.music_path;
        out << YAML::Key << "font"  << YAML::Value << config.font_path;
        out << YAML::EndMap;

        // -- Logging ----------------------------------------------------------
        out << YAML::Key << "logging" << YAML::Value << YAML::BeginMap;
        out << YAML::Key << "path"  << YAML::Value << config.log_path;
        out << YAML::Key << "level" << YAML::Value << config.log_level;
        out << YAML::EndMap;

        // -- Gameplay ---------------------------------------------------------
        out << YAML::Key << "gameplay" << YAML::Value << YAML::BeginMap;
        out << YAML::Key << "show_fps"     << YAML::Value << config.show_fps;
        out << YAML::Key << "show_minimap" << YAML::Value << config.show_minimap;
        out << YAML::Key << "ui_scale"     << YAML::Value << config.ui_scale;
        out << YAML::EndMap;

        out << YAML::EndMap;

        std::ofstream fout(path);
        if (!fout.is_open()) {
            std::cerr << "Failed to open client config for writing: "
                      << path << std::endl;
            return;
        }
        fout << out.c_str() << "\n";

    } catch (const std::exception& ex) {
        std::cerr << "Failed to save client config (" << path << "): "
                  << ex.what() << std::endl;
    }
}

}  // namespace mir2::client::config

#endif  // LEGEND2_CLIENT_CONFIG_CLIENT_CONFIG_H
