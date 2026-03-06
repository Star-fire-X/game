#include "config/config_manager.h"

#include <array>
#include <algorithm>
#include <cctype>
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

std::string NormalizeTransport(const std::string& value) {
  std::string normalized = value;
  std::transform(normalized.begin(), normalized.end(), normalized.begin(),
                 [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
  return normalized;
}

bool IsSupportedTransport(const std::string& value) {
  return value == "auto" || value == "tcp" || value == "uds";
}

bool ValidateNoRemovedKeys(const YAML::Node& root, const YAML::Node& server) {
  if (root && root["message_routes"]) {
    std::cerr << "Config load failed: key 'message_routes' is removed; "
              << "gateway now uses universal forwarding." << std::endl;
    return false;
  }

  static constexpr std::array<const char*, 5> kRemovedServerKeys = {
      "legacy_fallback_enabled",
      "legacy_fallback_allow_auth_whitelist",
      "legacy_fallback_allow_critical_msgs",
      "legacy_fallback_allow_normal_msgs",
      "queue_full_fallback_non_best_effort_enabled"};
  for (const char* key : kRemovedServerKeys) {
    if (server && server[key]) {
      std::cerr << "Config load failed: key 'server." << key
                << "' is removed and must not be configured." << std::endl;
      return false;
    }
  }
  return true;
}

bool ValidateGatewayThresholdConfig(const ServerConfig& config) {
  if (config.gateway_stale_route_cleanup_interval_ms <= 0) {
    std::cerr << "Config load failed: server.gateway_stale_route_cleanup_interval_ms "
              << "must be > 0." << std::endl;
    return false;
  }
  if (config.gateway_backpressure_default_pause_ms <= 0) {
    std::cerr << "Config load failed: server.gateway_backpressure_default_pause_ms "
              << "must be > 0." << std::endl;
    return false;
  }
  if (config.gateway_backpressure_max_pause_ms <= 0) {
    std::cerr << "Config load failed: server.gateway_backpressure_max_pause_ms "
              << "must be > 0." << std::endl;
    return false;
  }
  if (config.gateway_backpressure_max_pause_ms <
      config.gateway_backpressure_default_pause_ms) {
    std::cerr << "Config load failed: server.gateway_backpressure_max_pause_ms "
              << "must be >= server.gateway_backpressure_default_pause_ms."
              << std::endl;
    return false;
  }
  if (config.gateway_max_forward_payload_bytes <= 0) {
    std::cerr << "Config load failed: server.gateway_max_forward_payload_bytes "
              << "must be > 0." << std::endl;
    return false;
  }
  if (config.gateway_disconnect_retry_initial_backoff_ms <= 0) {
    std::cerr << "Config load failed: "
              << "server.gateway_disconnect_retry_initial_backoff_ms must be > 0."
              << std::endl;
    return false;
  }
  if (config.gateway_disconnect_retry_max_backoff_ms <= 0) {
    std::cerr << "Config load failed: "
              << "server.gateway_disconnect_retry_max_backoff_ms must be > 0."
              << std::endl;
    return false;
  }
  if (config.gateway_disconnect_retry_max_backoff_ms <
      config.gateway_disconnect_retry_initial_backoff_ms) {
    std::cerr << "Config load failed: "
              << "server.gateway_disconnect_retry_max_backoff_ms "
              << "must be >= server.gateway_disconnect_retry_initial_backoff_ms."
              << std::endl;
    return false;
  }
  if (config.gateway_disconnect_retry_ttl_ms <= 0) {
    std::cerr << "Config load failed: server.gateway_disconnect_retry_ttl_ms "
              << "must be > 0." << std::endl;
    return false;
  }
  if (config.gateway_disconnect_retry_ttl_ms <
      config.gateway_disconnect_retry_initial_backoff_ms) {
    std::cerr << "Config load failed: server.gateway_disconnect_retry_ttl_ms "
              << "must be >= server.gateway_disconnect_retry_initial_backoff_ms."
              << std::endl;
    return false;
  }
  if (config.gateway_disconnect_retry_max_queue_size <= 0) {
    std::cerr << "Config load failed: server.gateway_disconnect_retry_max_queue_size "
              << "must be > 0." << std::endl;
    return false;
  }
  if (config.gateway_holder_buffer_capacity_bytes <= 0) {
    std::cerr << "Config load failed: server.gateway_holder_buffer_capacity_bytes "
              << "must be > 0." << std::endl;
    return false;
  }
  if (config.gateway_holder_disconnect_threshold_bytes <= 0) {
    std::cerr << "Config load failed: "
              << "server.gateway_holder_disconnect_threshold_bytes must be > 0."
              << std::endl;
    return false;
  }
  if (config.gateway_holder_disconnect_threshold_bytes <
      config.gateway_holder_buffer_capacity_bytes) {
    std::cerr << "Config load failed: "
              << "server.gateway_holder_disconnect_threshold_bytes must be >= "
              << "server.gateway_holder_buffer_capacity_bytes." << std::endl;
    return false;
  }
  if (config.gateway_kcp_cleanup_interval_ms <= 0) {
    std::cerr << "Config load failed: server.gateway_kcp_cleanup_interval_ms "
              << "must be > 0." << std::endl;
    return false;
  }
  if (config.gateway_tcp_write_batch_max_bytes <= 0) {
    std::cerr << "Config load failed: server.gateway_tcp_write_batch_max_bytes "
              << "must be > 0." << std::endl;
    return false;
  }
  if (config.gateway_tcp_write_batch_flush_us < 0) {
    std::cerr << "Config load failed: server.gateway_tcp_write_batch_flush_us "
              << "must be >= 0." << std::endl;
    return false;
  }
  return true;
}

}  // namespace

bool ConfigManager::Load(const std::string& config_path) {
  try {
    YAML::Node root = YAML::LoadFile(config_path);
    config_path_ = config_path;
    server_config_ = ServerConfig{};
    database_config_ = DatabaseConfig{};
    log_config_ = LogConfig{};
    service_config_ = ServiceConfig{};
    ecs_config_ = EcsConfig{};
    storage_engine_config_ = StorageEngineConfig{};

    const YAML::Node server = root["server"];
    if (!ValidateNoRemovedKeys(root, server)) {
      return false;
    }

    server_config_.id = ReadOrDefault(server, "id", server_config_.id);
    server_config_.name = ReadOrDefault(server, "name", server_config_.name);
    server_config_.bind_ip = ReadOrDefault(server, "bind_ip", server_config_.bind_ip);
    server_config_.port = ReadOrDefault(server, "port", server_config_.port);
    server_config_.udp_port = ReadOrDefault(server, "udp_port", server_config_.udp_port);
    server_config_.io_threads = ReadOrDefault(server, "io_threads", server_config_.io_threads);
    server_config_.max_connections = ReadOrDefault(server, "max_connections", server_config_.max_connections);
    server_config_.tick_interval_ms = ReadOrDefault(server, "tick_interval_ms", server_config_.tick_interval_ms);
    server_config_.heartbeat_timeout_ms =
        ReadOrDefault(server, "heartbeat_timeout_ms", server_config_.heartbeat_timeout_ms);
    server_config_.service_link_write_queue_size = ReadOrDefault(
        server,
        "service_link_write_queue_size",
        server_config_.service_link_write_queue_size);
    server_config_.network_low_copy_send_enabled = ReadOrDefault(
        server,
        "network_low_copy_send_enabled",
        server_config_.network_low_copy_send_enabled);
    server_config_.network_session_idle_check_interval_ms = ReadOrDefault(
        server,
        "network_session_idle_check_interval_ms",
        server_config_.network_session_idle_check_interval_ms);
    server_config_.network_session_idle_timeout_ms = ReadOrDefault(
        server,
        "network_session_idle_timeout_ms",
        server_config_.network_session_idle_timeout_ms);
    server_config_.hot_event_max_drain_per_tick = ReadOrDefault(
        server,
        "hot_event_max_drain_per_tick",
        server_config_.hot_event_max_drain_per_tick);
    server_config_.hot_event_max_drain_ms_per_tick = ReadOrDefault(
        server,
        "hot_event_max_drain_ms_per_tick",
        server_config_.hot_event_max_drain_ms_per_tick);
    server_config_.mailbox_player_max_high_pending = ReadOrDefault(
        server,
        "mailbox_player_max_high_pending",
        server_config_.mailbox_player_max_high_pending);
    server_config_.mailbox_player_max_low_pending = ReadOrDefault(
        server,
        "mailbox_player_max_low_pending",
        server_config_.mailbox_player_max_low_pending);
    server_config_.mailbox_high_priority_burst = ReadOrDefault(
        server,
        "mailbox_high_priority_burst",
        server_config_.mailbox_high_priority_burst);
    server_config_.mailbox_overflow_kick_threshold = ReadOrDefault(
        server,
        "mailbox_overflow_kick_threshold",
        server_config_.mailbox_overflow_kick_threshold);
    server_config_.mailbox_global_pending_hard_limit = ReadOrDefault(
        server,
        "mailbox_global_pending_hard_limit",
        server_config_.mailbox_global_pending_hard_limit);
    server_config_.mailbox_global_pending_soft_limit = ReadOrDefault(
        server,
        "mailbox_global_pending_soft_limit",
        server_config_.mailbox_global_pending_soft_limit);
    server_config_.backpressure_pause_ms = ReadOrDefault(
        server,
        "backpressure_pause_ms",
        server_config_.backpressure_pause_ms);
    server_config_.backpressure_signal_cooldown_ms = ReadOrDefault(
        server,
        "backpressure_signal_cooldown_ms",
        server_config_.backpressure_signal_cooldown_ms);
    server_config_.mailbox_soft_backpressure_pause_ms = ReadOrDefault(
        server,
        "mailbox_soft_backpressure_pause_ms",
        server_config_.mailbox_soft_backpressure_pause_ms);
    server_config_.mailbox_hard_backpressure_pause_ms = ReadOrDefault(
        server,
        "mailbox_hard_backpressure_pause_ms",
        server_config_.mailbox_hard_backpressure_pause_ms);
    server_config_.mailbox_soft_backpressure_cooldown_ms = ReadOrDefault(
        server,
        "mailbox_soft_backpressure_cooldown_ms",
        server_config_.mailbox_soft_backpressure_cooldown_ms);
    server_config_.mailbox_hard_backpressure_cooldown_ms = ReadOrDefault(
        server,
        "mailbox_hard_backpressure_cooldown_ms",
        server_config_.mailbox_hard_backpressure_cooldown_ms);
    server_config_.coroutine_hung_threshold_ms = ReadOrDefault(
        server,
        "coroutine_hung_threshold_ms",
        server_config_.coroutine_hung_threshold_ms);
    server_config_.coroutine_hung_scan_interval_ms = ReadOrDefault(
        server,
        "coroutine_hung_scan_interval_ms",
        server_config_.coroutine_hung_scan_interval_ms);
    server_config_.coroutine_dump_max_entries = ReadOrDefault(
        server,
        "coroutine_dump_max_entries",
        server_config_.coroutine_dump_max_entries);
    server_config_.session_cleanup_on_gateway_disconnect = ReadOrDefault(
        server,
        "session_cleanup_on_gateway_disconnect",
        server_config_.session_cleanup_on_gateway_disconnect);
    server_config_.reconcile_cleanup_enabled = ReadOrDefault(
        server,
        "reconcile_cleanup_enabled",
        server_config_.reconcile_cleanup_enabled);
    server_config_.zombie_detection_enabled = ReadOrDefault(
        server,
        "zombie_detection_enabled",
        server_config_.zombie_detection_enabled);
    server_config_.zombie_detection_scan_interval_ms = ReadOrDefault(
        server,
        "zombie_detection_scan_interval_ms",
        server_config_.zombie_detection_scan_interval_ms);
    server_config_.zombie_detection_idle_timeout_ms = ReadOrDefault(
        server,
        "zombie_detection_idle_timeout_ms",
        server_config_.zombie_detection_idle_timeout_ms);
    if (const YAML::Node zombie_detection = server["zombie_detection"]) {
      server_config_.zombie_detection_enabled = ReadOrDefault(
          zombie_detection,
          "enabled",
          server_config_.zombie_detection_enabled);
      server_config_.zombie_detection_scan_interval_ms = ReadOrDefault(
          zombie_detection,
          "scan_interval_ms",
          server_config_.zombie_detection_scan_interval_ms);
      server_config_.zombie_detection_idle_timeout_ms = ReadOrDefault(
          zombie_detection,
          "idle_timeout_ms",
          server_config_.zombie_detection_idle_timeout_ms);
    }
    server_config_.chat_batch_send_enabled = ReadOrDefault(
        server,
        "chat_batch_send_enabled",
        server_config_.chat_batch_send_enabled);
    server_config_.gateway_stale_route_cleanup_interval_ms = ReadOrDefault(
        server,
        "gateway_stale_route_cleanup_interval_ms",
        server_config_.gateway_stale_route_cleanup_interval_ms);
    server_config_.gateway_backpressure_default_pause_ms = ReadOrDefault(
        server,
        "gateway_backpressure_default_pause_ms",
        server_config_.gateway_backpressure_default_pause_ms);
    server_config_.gateway_backpressure_max_pause_ms = ReadOrDefault(
        server,
        "gateway_backpressure_max_pause_ms",
        server_config_.gateway_backpressure_max_pause_ms);
    server_config_.gateway_max_forward_payload_bytes = ReadOrDefault(
        server,
        "gateway_max_forward_payload_bytes",
        server_config_.gateway_max_forward_payload_bytes);
    server_config_.gateway_disconnect_retry_initial_backoff_ms = ReadOrDefault(
        server,
        "gateway_disconnect_retry_initial_backoff_ms",
        server_config_.gateway_disconnect_retry_initial_backoff_ms);
    server_config_.gateway_disconnect_retry_max_backoff_ms = ReadOrDefault(
        server,
        "gateway_disconnect_retry_max_backoff_ms",
        server_config_.gateway_disconnect_retry_max_backoff_ms);
    server_config_.gateway_disconnect_retry_ttl_ms = ReadOrDefault(
        server,
        "gateway_disconnect_retry_ttl_ms",
        server_config_.gateway_disconnect_retry_ttl_ms);
    server_config_.gateway_disconnect_retry_max_queue_size = ReadOrDefault(
        server,
        "gateway_disconnect_retry_max_queue_size",
        server_config_.gateway_disconnect_retry_max_queue_size);
    server_config_.gateway_holder_buffer_capacity_bytes = ReadOrDefault(
        server,
        "gateway_holder_buffer_capacity_bytes",
        server_config_.gateway_holder_buffer_capacity_bytes);
    server_config_.gateway_holder_disconnect_threshold_bytes = ReadOrDefault(
        server,
        "gateway_holder_disconnect_threshold_bytes",
        server_config_.gateway_holder_disconnect_threshold_bytes);
    server_config_.gateway_kcp_cleanup_interval_ms = ReadOrDefault(
        server,
        "gateway_kcp_cleanup_interval_ms",
        server_config_.gateway_kcp_cleanup_interval_ms);
    server_config_.gateway_tcp_write_batch_max_bytes = ReadOrDefault(
        server,
        "gateway_tcp_write_batch_max_bytes",
        server_config_.gateway_tcp_write_batch_max_bytes);
    server_config_.gateway_tcp_write_batch_flush_us = ReadOrDefault(
        server,
        "gateway_tcp_write_batch_flush_us",
        server_config_.gateway_tcp_write_batch_flush_us);
    server_config_.movement_speed_violation_severity = ReadOrDefault(
        server,
        "movement_speed_violation_severity",
        server_config_.movement_speed_violation_severity);
    server_config_.movement_teleport_violation_severity = ReadOrDefault(
        server,
        "movement_teleport_violation_severity",
        server_config_.movement_teleport_violation_severity);
    server_config_.login_ip_rate_limit_capacity = ReadOrDefault(
        server,
        "login_ip_rate_limit_capacity",
        server_config_.login_ip_rate_limit_capacity);
    server_config_.login_ip_rate_limit_refill_rate = ReadOrDefault(
        server,
        "login_ip_rate_limit_refill_rate",
        server_config_.login_ip_rate_limit_refill_rate);
    server_config_.login_username_rate_limit_capacity = ReadOrDefault(
        server,
        "login_username_rate_limit_capacity",
        server_config_.login_username_rate_limit_capacity);
    server_config_.login_username_rate_limit_refill_rate = ReadOrDefault(
        server,
        "login_username_rate_limit_refill_rate",
        server_config_.login_username_rate_limit_refill_rate);
    server_config_.login_username_rate_limit_refill_interval_seconds = ReadOrDefault(
        server,
        "login_username_rate_limit_refill_interval_seconds",
        server_config_.login_username_rate_limit_refill_interval_seconds);
    server_config_.udp_send_fault_inject_every_n = ReadOrDefault(
        server,
        "udp_send_fault_inject_every_n",
        server_config_.udp_send_fault_inject_every_n);
    server_config_.enable_network_listener = ReadOrDefault(
        server,
        "enable_network_listener",
        server_config_.enable_network_listener);
    server_config_.metrics_port = ReadOrDefault(server, "metrics_port", server_config_.metrics_port);
    if (!ValidateGatewayThresholdConfig(server_config_)) {
      return false;
    }

    const YAML::Node database = root["database"];
    database_config_.host = ReadOrDefault(database, "host", database_config_.host);
    database_config_.port = ReadOrDefault(database, "port", database_config_.port);
    database_config_.user = ReadOrDefault(database, "user", database_config_.user);
    database_config_.password = ReadOrDefault(database, "password", database_config_.password);
    database_config_.database = ReadOrDefault(database, "database", database_config_.database);
    database_config_.pool_size = ReadOrDefault(database, "pool_size", database_config_.pool_size);

    const YAML::Node log = root["log"];
    log_config_.level = ReadOrDefault(log, "level", log_config_.level);
    log_config_.path = ReadOrDefault(log, "path", log_config_.path);
    log_config_.max_size_mb = ReadOrDefault(log, "max_size_mb", log_config_.max_size_mb);
    log_config_.max_files = ReadOrDefault(log, "max_files", log_config_.max_files);

    // 只解析 logic 服务配置（向后兼容：忽略旧的 world/game/db 字段）
    service_config_.logic = ServiceConfig{}.logic;
    const YAML::Node services = root["services"];
    const YAML::Node logic = services["logic"];
    service_config_.logic.host = ReadOrDefault(logic, "host", service_config_.logic.host);
    service_config_.logic.port = ReadOrDefault(logic, "port", service_config_.logic.port);
    service_config_.logic.transport = NormalizeTransport(
        ReadOrDefault(logic, "transport", service_config_.logic.transport));
    service_config_.logic.uds_path =
        ReadOrDefault(logic, "uds_path", service_config_.logic.uds_path);
    if (!IsSupportedTransport(service_config_.logic.transport)) {
      std::cerr << "Unsupported services.logic.transport='"
                << service_config_.logic.transport
                << "', fallback to 'auto'" << std::endl;
      service_config_.logic.transport = "auto";
    }

    const YAML::Node ecs = root["ecs"];
    ecs_config_.world_registry_reserve =
        ReadOrDefault(ecs, "world_registry_reserve", ecs_config_.world_registry_reserve);

    const YAML::Node storage_engine = root["storage_engine"];
    storage_engine_config_.l1_max_entries = ReadOrDefault(
        storage_engine, "l1_max_entries", storage_engine_config_.l1_max_entries);
    storage_engine_config_.l1_ttl_seconds = ReadOrDefault(
        storage_engine, "l1_ttl_seconds", storage_engine_config_.l1_ttl_seconds);
    storage_engine_config_.l2_max_size_mb = ReadOrDefault(
        storage_engine, "l2_max_size_mb", storage_engine_config_.l2_max_size_mb);
    storage_engine_config_.l2_block_cache_mb = ReadOrDefault(
        storage_engine, "l2_block_cache_mb",
        storage_engine_config_.l2_block_cache_mb);
    if (!(storage_engine && storage_engine["l2_block_cache_mb"])) {
      // Backward compatibility: keep the legacy behavior when the new key
      // is omitted.
      storage_engine_config_.l2_block_cache_mb =
          storage_engine_config_.l2_max_size_mb;
    }
    storage_engine_config_.l2_data_write_buffer_mb = ReadOrDefault(
        storage_engine, "l2_data_write_buffer_mb",
        storage_engine_config_.l2_data_write_buffer_mb);
    storage_engine_config_.l2_meta_write_buffer_mb = ReadOrDefault(
        storage_engine, "l2_meta_write_buffer_mb",
        storage_engine_config_.l2_meta_write_buffer_mb);
    storage_engine_config_.l2_data_max_write_buffer_number = ReadOrDefault(
        storage_engine, "l2_data_max_write_buffer_number",
        storage_engine_config_.l2_data_max_write_buffer_number);
    storage_engine_config_.l2_meta_max_write_buffer_number = ReadOrDefault(
        storage_engine, "l2_meta_max_write_buffer_number",
        storage_engine_config_.l2_meta_max_write_buffer_number);
    storage_engine_config_.l2_max_background_jobs = ReadOrDefault(
        storage_engine, "l2_max_background_jobs",
        storage_engine_config_.l2_max_background_jobs);
    storage_engine_config_.l2_max_background_flushes = ReadOrDefault(
        storage_engine, "l2_max_background_flushes",
        storage_engine_config_.l2_max_background_flushes);
    storage_engine_config_.l2_block_size = ReadOrDefault(
        storage_engine, "l2_block_size", storage_engine_config_.l2_block_size);
    storage_engine_config_.l2_bloom_filter_bits_per_key = ReadOrDefault(
        storage_engine, "l2_bloom_filter_bits_per_key",
        storage_engine_config_.l2_bloom_filter_bits_per_key);
    storage_engine_config_.l2_usage_soft_limit_ratio = ReadOrDefault(
        storage_engine, "l2_usage_soft_limit_ratio",
        storage_engine_config_.l2_usage_soft_limit_ratio);
    storage_engine_config_.l2_usage_hard_limit_ratio = ReadOrDefault(
        storage_engine, "l2_usage_hard_limit_ratio",
        storage_engine_config_.l2_usage_hard_limit_ratio);
    storage_engine_config_.l2_path = ReadOrDefault(
        storage_engine, "l2_path", storage_engine_config_.l2_path);
    storage_engine_config_.l2_ttl_seconds = ReadOrDefault(
        storage_engine, "l2_ttl_seconds", storage_engine_config_.l2_ttl_seconds);
    storage_engine_config_.l2_ttl_periodic_compaction_seconds = ReadOrDefault(
        storage_engine, "l2_ttl_periodic_compaction_seconds",
        storage_engine_config_.l2_ttl_periodic_compaction_seconds);
    storage_engine_config_.l2_strict_ttl_reads = ReadOrDefault(
        storage_engine, "l2_strict_ttl_reads",
        storage_engine_config_.l2_strict_ttl_reads);
    storage_engine_config_.l2_scan_fill_cache = ReadOrDefault(
        storage_engine, "l2_scan_fill_cache",
        storage_engine_config_.l2_scan_fill_cache);
    storage_engine_config_.l2_iter_pin_data = ReadOrDefault(
        storage_engine, "l2_iter_pin_data",
        storage_engine_config_.l2_iter_pin_data);
    storage_engine_config_.auto_sync_interval_ms = ReadOrDefault(
        storage_engine, "auto_sync_interval_ms",
        storage_engine_config_.auto_sync_interval_ms);
    storage_engine_config_.batch_size = ReadOrDefault(
        storage_engine, "batch_size", storage_engine_config_.batch_size);
    storage_engine_config_.sync_timeout_ms = ReadOrDefault(
        storage_engine, "sync_timeout_ms", storage_engine_config_.sync_timeout_ms);
    storage_engine_config_.queue_capacity = ReadOrDefault(
        storage_engine, "queue_capacity", storage_engine_config_.queue_capacity);
    storage_engine_config_.queue_worker_threads = ReadOrDefault(
        storage_engine, "queue_worker_threads",
        storage_engine_config_.queue_worker_threads);
    storage_engine_config_.queue_retry_count = ReadOrDefault(
        storage_engine, "queue_retry_count",
        storage_engine_config_.queue_retry_count);
    storage_engine_config_.queue_retry_delay_ms = ReadOrDefault(
        storage_engine, "queue_retry_delay_ms",
        storage_engine_config_.queue_retry_delay_ms);
    storage_engine_config_.account_cache_ttl_seconds = ReadOrDefault(
        storage_engine,
        "account_cache_ttl_seconds",
        storage_engine_config_.account_cache_ttl_seconds);
    storage_engine_config_.account_cache_max_entries = ReadOrDefault(
        storage_engine,
        "account_cache_max_entries",
        storage_engine_config_.account_cache_max_entries);
    storage_engine_config_.dead_letter_max_items = ReadOrDefault(
        storage_engine, "dead_letter_max_items",
        storage_engine_config_.dead_letter_max_items);
    storage_engine_config_.enable_strict_write_guarantee = ReadOrDefault(
        storage_engine, "enable_strict_write_guarantee",
        storage_engine_config_.enable_strict_write_guarantee);
    storage_engine_config_.critical_data_no_ttl = ReadOrDefault(
        storage_engine, "critical_data_no_ttl",
        storage_engine_config_.critical_data_no_ttl);
    storage_engine_config_.enable_outbox = ReadOrDefault(
        storage_engine, "enable_outbox", storage_engine_config_.enable_outbox);
    storage_engine_config_.outbox_replay_limit = ReadOrDefault(
        storage_engine, "outbox_replay_limit",
        storage_engine_config_.outbox_replay_limit);
    storage_engine_config_.outbox_max_items = ReadOrDefault(
        storage_engine, "outbox_max_items", storage_engine_config_.outbox_max_items);
    storage_engine_config_.circuit_breaker_threshold = ReadOrDefault(
        storage_engine, "circuit_breaker_threshold",
        storage_engine_config_.circuit_breaker_threshold);
    storage_engine_config_.circuit_breaker_timeout_ms = ReadOrDefault(
        storage_engine, "circuit_breaker_timeout_ms",
        storage_engine_config_.circuit_breaker_timeout_ms);
    storage_engine_config_.enable_metrics = ReadOrDefault(
        storage_engine, "enable_metrics", storage_engine_config_.enable_metrics);
    storage_engine_config_.enable_audit_log = ReadOrDefault(
        storage_engine, "enable_audit_log", storage_engine_config_.enable_audit_log);
    storage_engine_config_.audit_log_max_entries = ReadOrDefault(
        storage_engine, "audit_log_max_entries",
        storage_engine_config_.audit_log_max_entries);
    storage_engine_config_.enable_new_write_path = ReadOrDefault(
        storage_engine, "enable_new_write_path",
        storage_engine_config_.enable_new_write_path);
    storage_engine_config_.enable_v2_encode = ReadOrDefault(
        storage_engine, "enable_v2_encode",
        storage_engine_config_.enable_v2_encode);
    storage_engine_config_.enable_v2_read_fallback = ReadOrDefault(
        storage_engine, "enable_v2_read_fallback",
        storage_engine_config_.enable_v2_read_fallback);
    storage_engine_config_.enable_data_encryption = ReadOrDefault(
        storage_engine, "enable_data_encryption",
        storage_engine_config_.enable_data_encryption);
    storage_engine_config_.encryption_active_key_id = ReadOrDefault(
        storage_engine, "encryption_active_key_id",
        storage_engine_config_.encryption_active_key_id);
    storage_engine_config_.encryption_key_env = ReadOrDefault(
        storage_engine, "encryption_key_env",
        storage_engine_config_.encryption_key_env);
    storage_engine_config_.startup_fail_on_validation_error = ReadOrDefault(
        storage_engine, "startup_fail_on_validation_error",
        storage_engine_config_.startup_fail_on_validation_error);
    storage_engine_config_.checkpoint_enabled = ReadOrDefault(
        storage_engine, "checkpoint_enabled",
        storage_engine_config_.checkpoint_enabled);
    storage_engine_config_.checkpoint_interval_minutes = ReadOrDefault(
        storage_engine, "checkpoint_interval_minutes",
        storage_engine_config_.checkpoint_interval_minutes);
    storage_engine_config_.checkpoint_retention = ReadOrDefault(
        storage_engine, "checkpoint_retention",
        storage_engine_config_.checkpoint_retention);
    storage_engine_config_.tombstone_retention_seconds = ReadOrDefault(
        storage_engine, "tombstone_retention_seconds",
        storage_engine_config_.tombstone_retention_seconds);
    storage_engine_config_.tombstone_gc_interval_seconds = ReadOrDefault(
        storage_engine, "tombstone_gc_interval_seconds",
        storage_engine_config_.tombstone_gc_interval_seconds);
    storage_engine_config_.enable_access_control = ReadOrDefault(
        storage_engine, "enable_access_control",
        storage_engine_config_.enable_access_control);
    storage_engine_config_.require_auth_for_reads = ReadOrDefault(
        storage_engine, "require_auth_for_reads",
        storage_engine_config_.require_auth_for_reads);
    storage_engine_config_.access_control_token = ReadOrDefault(
        storage_engine, "access_control_token",
        storage_engine_config_.access_control_token);
    if (storage_engine && storage_engine["critical_key_prefixes"]) {
      storage_engine_config_.critical_key_prefixes.clear();
      for (const auto& prefix_node : storage_engine["critical_key_prefixes"]) {
        storage_engine_config_.critical_key_prefixes.push_back(prefix_node.as<std::string>());
      }
      if (storage_engine_config_.critical_key_prefixes.empty()) {
        storage_engine_config_.critical_key_prefixes = {"char:", "account:username:"};
      }
    }
    if (storage_engine && storage_engine["sync_write_key_prefixes"]) {
      storage_engine_config_.sync_write_key_prefixes.clear();
      for (const auto& prefix_node : storage_engine["sync_write_key_prefixes"]) {
        storage_engine_config_.sync_write_key_prefixes.push_back(
            prefix_node.as<std::string>());
      }
      if (storage_engine_config_.sync_write_key_prefixes.empty()) {
        storage_engine_config_.sync_write_key_prefixes = {"char:"};
      }
    }

    const auto config_dir = std::filesystem::path(config_path).parent_path();
    if (!config_dir.empty()) {
      combat_config_path_ = (config_dir / "combat_config.yaml").string();
      if (!LoadCombatConfig(combat_config_path_)) {
        std::cerr << "Combat config load failed: " << combat_config_path_ << std::endl;
      }
    }

    loaded_ = true;
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
