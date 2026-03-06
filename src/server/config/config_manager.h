/**
 * @file config_manager.h
 * @brief 配置管理器
 */

#ifndef MIR2_CONFIG_CONFIG_MANAGER_H_
#define MIR2_CONFIG_CONFIG_MANAGER_H_

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

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
  int service_link_write_queue_size = 8192;
  bool network_low_copy_send_enabled = true;
  int network_session_idle_check_interval_ms = 30000;
  int network_session_idle_timeout_ms = 90000;
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
  bool session_cleanup_on_gateway_disconnect = true;
  bool reconcile_cleanup_enabled = true;
  bool zombie_detection_enabled = true;
  int zombie_detection_scan_interval_ms = 5000;
  int zombie_detection_idle_timeout_ms = 120000;
  bool legacy_fallback_enabled = false;
  bool legacy_fallback_allow_auth_whitelist = false;
  bool legacy_fallback_allow_critical_msgs = false;
  bool legacy_fallback_allow_normal_msgs = false;
  bool queue_full_fallback_non_best_effort_enabled = false;
  bool chat_batch_send_enabled = true;
  int gateway_stale_route_cleanup_interval_ms = 30000;
  int gateway_backpressure_default_pause_ms = 100;
  int gateway_backpressure_max_pause_ms = 2000;
  int gateway_max_forward_payload_bytes = 64 * 1024;
  int gateway_disconnect_retry_initial_backoff_ms = 500;
  int gateway_disconnect_retry_max_backoff_ms = 30000;
  int gateway_disconnect_retry_ttl_ms = 300000;
  int gateway_disconnect_retry_max_queue_size = 100000;
  int gateway_holder_buffer_capacity_bytes = 2 * 1024 * 1024;
  int gateway_holder_disconnect_threshold_bytes = 5 * 1024 * 1024;
  int gateway_kcp_cleanup_interval_ms = 1000;
  int gateway_tcp_write_batch_max_bytes = 32768;
  int gateway_tcp_write_batch_flush_us = 1000;
  int movement_speed_violation_severity = 10;
  int movement_teleport_violation_severity = 5;
  int login_ip_rate_limit_capacity = 5;
  int login_ip_rate_limit_refill_rate = 1;
  int login_username_rate_limit_capacity = 5;
  int login_username_rate_limit_refill_rate = 1;
  int login_username_rate_limit_refill_interval_seconds = 12;
  int udp_send_fault_inject_every_n = 0;
  bool enable_network_listener = true;
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
  std::string transport = "auto";
  std::string uds_path;
};

/**
 * @brief 服务集群配置
 */
struct ServiceConfig {
  ServiceEndpoint logic{ "127.0.0.1", 8002, "auto", "" };
};

/**
 * @brief ECS配置
 */
struct EcsConfig {
  std::size_t world_registry_reserve = 1000;  ///< 单地图预估玩家数（用于预分配）
};

/**
 * @brief StorageEngine 配置
 */
struct StorageEngineConfig {
  uint32_t l1_max_entries = 10000;
  uint32_t l1_ttl_seconds = 300;
  uint32_t l2_max_size_mb = 512;
  uint32_t l2_block_cache_mb = 512;
  uint32_t l2_data_write_buffer_mb = 64;
  uint32_t l2_meta_write_buffer_mb = 8;
  uint32_t l2_data_max_write_buffer_number = 4;
  uint32_t l2_meta_max_write_buffer_number = 2;
  uint32_t l2_max_background_jobs = 8;
  uint32_t l2_max_background_flushes = 2;
  uint32_t l2_block_size = 4096;
  double l2_bloom_filter_bits_per_key = 10.0;
  double l2_usage_soft_limit_ratio = 0.85;
  double l2_usage_hard_limit_ratio = 0.95;
  std::string l2_path = "/var/lib/mir2/cache";
  uint32_t l2_ttl_seconds = 604800;
  uint32_t l2_ttl_periodic_compaction_seconds = 21600;
  bool l2_strict_ttl_reads = true;
  bool l2_scan_fill_cache = false;
  bool l2_iter_pin_data = true;
  uint32_t auto_sync_interval_ms = 5000;
  uint32_t batch_size = 100;
  uint32_t sync_timeout_ms = 30000;
  size_t queue_capacity = 10000;
  size_t queue_worker_threads = 2;
  uint32_t queue_retry_count = 3;
  uint32_t queue_retry_delay_ms = 100;
  uint32_t account_cache_ttl_seconds = 300;
  size_t account_cache_max_entries = 10000;
  size_t dead_letter_max_items = 10000;
  bool enable_strict_write_guarantee = true;
  bool critical_data_no_ttl = true;
  bool enable_outbox = true;
  size_t outbox_replay_limit = 0;
  size_t outbox_max_items = 200000;
  uint32_t circuit_breaker_threshold = 5;
  uint32_t circuit_breaker_timeout_ms = 60000;
  bool enable_metrics = true;
  bool enable_audit_log = true;
  size_t audit_log_max_entries = 5000;
  bool enable_new_write_path = true;
  bool enable_v2_encode = false;
  bool enable_v2_read_fallback = true;
  bool enable_data_encryption = false;
  std::string encryption_active_key_id;
  std::string encryption_key_env;
  bool startup_fail_on_validation_error = true;
  bool checkpoint_enabled = true;
  uint32_t checkpoint_interval_minutes = 30;
  uint32_t checkpoint_retention = 48;
  uint32_t tombstone_retention_seconds = 604800;
  uint32_t tombstone_gc_interval_seconds = 3600;
  bool enable_access_control = false;
  bool require_auth_for_reads = false;
  std::string access_control_token;
  std::vector<std::string> critical_key_prefixes = {"char:", "account:username:"};
  std::vector<std::string> sync_write_key_prefixes = {"char:"};
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
  const LogConfig& GetLogConfig() const { return log_config_; }
  const ServiceConfig& GetServiceConfig() const { return service_config_; }
  const EcsConfig& GetEcsConfig() const { return ecs_config_; }
  const StorageEngineConfig& GetStorageEngineConfig() const { return storage_engine_config_; }
  const legend2::CombatConfig& GetCombatConfig() const { return combat_config_; }

 private:
  ConfigManager() = default;

  bool LoadCombatConfig(const std::string& config_path);

  bool loaded_ = false;
  std::string config_path_;
  std::string combat_config_path_;
  ServerConfig server_config_;
  DatabaseConfig database_config_;
  LogConfig log_config_;
  ServiceConfig service_config_;
  EcsConfig ecs_config_;
  StorageEngineConfig storage_engine_config_;
  legend2::CombatConfig combat_config_;
};

}  // namespace mir2::config

#endif  // MIR2_CONFIG_CONFIG_MANAGER_H_
