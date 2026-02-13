/**
 * @file config_manager.h
 * @brief 配置管理器
 */

#ifndef MIR2_CONFIG_CONFIG_MANAGER_H_
#define MIR2_CONFIG_CONFIG_MANAGER_H_

#include <cstddef>
#include <string>

#include <yaml-cpp/yaml.h>

#include "server/ecs/systems/combat_core.h"
#include "core/singleton.h"
#include "common/constants.h"

namespace mir2::config {

/**
 * @brief 服务器配置
 */
struct ServerConfig {
  int id = 1;
  std::string name = "Mir2-Server";
  std::string bind_ip = "0.0.0.0";
  uint16_t port = common::kDefaultServerPort;
  uint16_t udp_port = 0;
  int io_threads = common::kDefaultIoThreads;
  int max_connections = common::kMaxConnections;
  int tick_interval_ms = common::kDefaultTickIntervalMs;
  int heartbeat_timeout_ms = 30000;
  int hot_event_max_drain_per_tick = 2048;
  int hot_event_max_drain_ms_per_tick = 5;
  int mailbox_player_max_high_pending = 100;
  int mailbox_player_max_low_pending = 64;
  int mailbox_high_priority_burst = 4;
  int mailbox_overflow_kick_threshold = 3;
  int mailbox_global_pending_hard_limit = 20000;
  int mailbox_global_pending_soft_limit = -1;  // < 0 means 75% of hard limit.
  int backpressure_pause_ms = 100;
  int backpressure_signal_cooldown_ms = 100;
  int mailbox_soft_backpressure_pause_ms = 100;
  int mailbox_hard_backpressure_pause_ms = 300;
  int mailbox_soft_backpressure_cooldown_ms = 100;
  int mailbox_hard_backpressure_cooldown_ms = 25;
  int coroutine_hung_threshold_ms = 5000;
  int coroutine_hung_scan_interval_ms = 500;
  int coroutine_dump_max_entries = 64;
  int movement_speed_violation_severity = 10;
  int movement_teleport_violation_severity = 5;
  int login_ip_rate_limit_capacity = 5;
  int login_ip_rate_limit_refill_rate = 1;
  uint16_t metrics_port = 0;
};

/**
 * @brief 数据库配置
 */
struct DatabaseConfig {
  std::string host = "127.0.0.1";
  uint16_t port = 5432;
  std::string user = "mir2";
  std::string password;
  std::string database = "mir2_game";
  int pool_size = 10;
};

/**
 * @brief Redis配置
 */
struct RedisConfig {
  std::string host = "127.0.0.1";
  uint16_t port = 6379;
  std::string password;
  int db = 0;
};

/**
 * @brief 日志配置
 */
struct LogConfig {
  std::string level = "info";
  std::string path = "logs";
  int max_size_mb = 100;
  int max_files = 10;
};

/**
 * @brief 服务端点配置
 */
struct ServiceEndpoint {
  std::string host = "127.0.0.1";
  uint16_t port = 0;
};

/**
 * @brief 服务集群配置
 */
struct ServiceConfig {
  ServiceEndpoint logic{ "127.0.0.1", 8002 };
};

/**
 * @brief ECS配置
 */
struct EcsConfig {
  std::size_t world_registry_reserve = 1000;  ///< 单地图预估玩家数（用于预分配）
};

/**
 * @brief 配置管理器
 *
 * 负责加载/热重载配置，集中提供配置信息。
 */
class ConfigManager : public core::Singleton<ConfigManager> {
  friend class core::Singleton<ConfigManager>;

 public:
  /**
   * @brief 加载配置
   * @param config_path 配置文件路径
   * @return 是否成功
   */
  bool Load(const std::string& config_path);

  /**
   * @brief 重新加载配置
   */
  bool Reload();

  /**
   * @brief 检查配置是否已加载
   */
  bool IsLoaded() const { return loaded_; }

  const ServerConfig& GetServerConfig() const { return server_config_; }
  const DatabaseConfig& GetDatabaseConfig() const { return database_config_; }
  const RedisConfig& GetRedisConfig() const { return redis_config_; }
  const LogConfig& GetLogConfig() const { return log_config_; }
  const ServiceConfig& GetServiceConfig() const { return service_config_; }
  const EcsConfig& GetEcsConfig() const { return ecs_config_; }
  const legend2::CombatConfig& GetCombatConfig() const { return combat_config_; }

 private:
  ConfigManager() = default;

  bool LoadCombatConfig(const std::string& config_path);

  bool loaded_ = false;
  std::string config_path_;
  std::string combat_config_path_;
  ServerConfig server_config_;
  DatabaseConfig database_config_;
  RedisConfig redis_config_;
  LogConfig log_config_;
  ServiceConfig service_config_;
  EcsConfig ecs_config_;
  legend2::CombatConfig combat_config_;
};

}  // namespace mir2::config

#endif  // MIR2_CONFIG_CONFIG_MANAGER_H_
