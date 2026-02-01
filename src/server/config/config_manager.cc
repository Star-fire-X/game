#include "config/config_manager.h"

#include <filesystem>
#include <iostream>

namespace mir2::config {

namespace {

template <typename T>
T ReadOrDefault(const YAML::Node& node, const char* key, const T& default_value) {
  if (node && node[key]) {
    return node[key].as<T>();
  }
  return default_value;
}

}  // namespace

bool ConfigManager::Load(const std::string& config_path) {
  try {
    YAML::Node root = YAML::LoadFile(config_path);
    config_path_ = config_path;

    const YAML::Node server = root["server"];
    server_config_.id = ReadOrDefault(server, "id", server_config_.id);
    server_config_.name = ReadOrDefault(server, "name", server_config_.name);
    server_config_.bind_ip = ReadOrDefault(server, "bind_ip", server_config_.bind_ip);
    server_config_.port = ReadOrDefault(server, "port", server_config_.port);
    server_config_.io_threads = ReadOrDefault(server, "io_threads", server_config_.io_threads);
    server_config_.max_connections = ReadOrDefault(server, "max_connections", server_config_.max_connections);
    server_config_.tick_interval_ms = ReadOrDefault(server, "tick_interval_ms", server_config_.tick_interval_ms);
    server_config_.heartbeat_timeout_ms =
        ReadOrDefault(server, "heartbeat_timeout_ms", server_config_.heartbeat_timeout_ms);
    server_config_.metrics_port = ReadOrDefault(server, "metrics_port", server_config_.metrics_port);

    const YAML::Node database = root["database"];
    database_config_.host = ReadOrDefault(database, "host", database_config_.host);
    database_config_.port = ReadOrDefault(database, "port", database_config_.port);
    database_config_.user = ReadOrDefault(database, "user", database_config_.user);
    database_config_.password = ReadOrDefault(database, "password", database_config_.password);
    database_config_.database = ReadOrDefault(database, "database", database_config_.database);
    database_config_.pool_size = ReadOrDefault(database, "pool_size", database_config_.pool_size);

    const YAML::Node redis = root["redis"];
    redis_config_.host = ReadOrDefault(redis, "host", redis_config_.host);
    redis_config_.port = ReadOrDefault(redis, "port", redis_config_.port);
    redis_config_.password = ReadOrDefault(redis, "password", redis_config_.password);
    redis_config_.db = ReadOrDefault(redis, "db", redis_config_.db);

    const YAML::Node log = root["log"];
    log_config_.level = ReadOrDefault(log, "level", log_config_.level);
    log_config_.path = ReadOrDefault(log, "path", log_config_.path);
    log_config_.max_size_mb = ReadOrDefault(log, "max_size_mb", log_config_.max_size_mb);
    log_config_.max_files = ReadOrDefault(log, "max_files", log_config_.max_files);

    const YAML::Node services = root["services"];
    const YAML::Node world = services["world"];
    service_config_.world.host = ReadOrDefault(world, "host", service_config_.world.host);
    service_config_.world.port = ReadOrDefault(world, "port", service_config_.world.port);

    const YAML::Node game = services["game"];
    service_config_.game.host = ReadOrDefault(game, "host", service_config_.game.host);
    service_config_.game.port = ReadOrDefault(game, "port", service_config_.game.port);

    const YAML::Node db = services["db"];
    service_config_.db.host = ReadOrDefault(db, "host", service_config_.db.host);
    service_config_.db.port = ReadOrDefault(db, "port", service_config_.db.port);

    const YAML::Node ecs = root["ecs"];
    ecs_config_.world_registry_reserve =
        ReadOrDefault(ecs, "world_registry_reserve", ecs_config_.world_registry_reserve);

    const auto config_dir = std::filesystem::path(config_path).parent_path();
    if (!config_dir.empty()) {
      combat_config_path_ = (config_dir / "combat_config.yaml").string();
      if (!LoadCombatConfig(combat_config_path_)) {
        std::cerr << "Combat config load failed: " << combat_config_path_ << std::endl;
      }
    }

    return true;
  } catch (const std::exception& ex) {
    std::cerr << "Config load failed: " << ex.what() << std::endl;
    return false;
  }
}

bool ConfigManager::Reload() {
  if (config_path_.empty()) {
    return false;
  }
  return Load(config_path_);
}

bool ConfigManager::LoadCombatConfig(const std::string& config_path) {
  try {
    if (config_path.empty() || !std::filesystem::exists(config_path)) {
      return false;
    }

    YAML::Node root = YAML::LoadFile(config_path);
    const YAML::Node combat = root["combat"] ? root["combat"] : root;

    combat_config_.min_variance_percent =
        ReadOrDefault(combat, "min_variance_percent", combat_config_.min_variance_percent);
    combat_config_.max_variance_percent =
        ReadOrDefault(combat, "max_variance_percent", combat_config_.max_variance_percent);
    combat_config_.minimum_damage =
        ReadOrDefault(combat, "minimum_damage", combat_config_.minimum_damage);
    combat_config_.base_critical_chance =
        ReadOrDefault(combat, "base_critical_chance", combat_config_.base_critical_chance);
    combat_config_.critical_multiplier =
        ReadOrDefault(combat, "critical_multiplier", combat_config_.critical_multiplier);
    combat_config_.base_miss_chance =
        ReadOrDefault(combat, "base_miss_chance", combat_config_.base_miss_chance);
    combat_config_.default_melee_range =
        ReadOrDefault(combat, "default_melee_range", combat_config_.default_melee_range);

    const YAML::Node respawn = combat["respawn"];
    combat_config_.default_respawn_hp_percent =
        ReadOrDefault(respawn, "hp_percent", combat_config_.default_respawn_hp_percent);
    combat_config_.default_respawn_mp_percent =
        ReadOrDefault(respawn, "mp_percent", combat_config_.default_respawn_mp_percent);
    combat_config_.default_respawn_map_id =
        ReadOrDefault(respawn, "map_id", combat_config_.default_respawn_map_id);

    const YAML::Node position = respawn["position"];
    if (position) {
      combat_config_.default_respawn_position.x =
          ReadOrDefault(position, "x", combat_config_.default_respawn_position.x);
      combat_config_.default_respawn_position.y =
          ReadOrDefault(position, "y", combat_config_.default_respawn_position.y);
    }

    return true;
  } catch (const std::exception& ex) {
    std::cerr << "Combat config load failed: " << ex.what() << std::endl;
    return false;
  }
}

}  // namespace mir2::config
