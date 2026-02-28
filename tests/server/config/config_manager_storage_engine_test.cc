#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

#include "config/config_manager.h"

namespace mir2::config {
namespace {

TEST(ConfigManagerStorageEngineTest, LoadsStorageEngineConfigFromYaml) {
  const std::filesystem::path path =
      std::filesystem::temp_directory_path() /
      ("mir2_config_storage_engine_" +
       std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) +
       ".yaml");

  std::ofstream out(path);
  ASSERT_TRUE(out.is_open());
  out << "server:\n"
      << "  id: 1\n"
      << "  name: \"test\"\n"
      << "database:\n"
      << "  host: \"127.0.0.1\"\n"
      << "log:\n"
      << "  level: \"info\"\n"
      << "services:\n"
      << "  logic:\n"
      << "    host: \"127.0.0.1\"\n"
      << "    port: 8002\n"
      << "storage_engine:\n"
      << "  l1_max_entries: 2048\n"
      << "  l1_ttl_seconds: 30\n"
      << "  l2_max_size_mb: 256\n"
      << "  l2_block_cache_mb: 192\n"
      << "  l2_data_write_buffer_mb: 48\n"
      << "  l2_meta_write_buffer_mb: 12\n"
      << "  l2_data_max_write_buffer_number: 6\n"
      << "  l2_meta_max_write_buffer_number: 3\n"
      << "  l2_max_background_jobs: 10\n"
      << "  l2_max_background_flushes: 4\n"
      << "  l2_block_size: 8192\n"
      << "  l2_bloom_filter_bits_per_key: 12.5\n"
      << "  l2_ttl_seconds: 120\n"
      << "  l2_ttl_periodic_compaction_seconds: 3600\n"
      << "  l2_strict_ttl_reads: false\n"
      << "  l2_scan_fill_cache: true\n"
      << "  l2_iter_pin_data: false\n"
      << "  queue_capacity: 4096\n"
      << "  queue_worker_threads: 3\n"
      << "  queue_retry_count: 7\n"
      << "  queue_retry_delay_ms: 250\n"
      << "  account_cache_ttl_seconds: 45\n"
      << "  account_cache_max_entries: 2222\n"
      << "  dead_letter_max_items: 123\n"
      << "  enable_outbox: true\n"
      << "  outbox_max_items: 777\n"
      << "  enable_access_control: true\n"
      << "  require_auth_for_reads: true\n"
      << "  enable_new_write_path: false\n"
      << "  access_control_token: \"token-xyz\"\n"
      << "  critical_key_prefixes: [\"char:\", \"account:username:\", \"trade:\"]\n"
      << "  sync_write_key_prefixes: [\"char:\", \"trade:\"]\n";
  out.close();

  ASSERT_TRUE(ConfigManager::Instance().Load(path.string()));
  const auto& cfg = ConfigManager::Instance().GetStorageEngineConfig();
  EXPECT_EQ(cfg.l1_max_entries, 2048u);
  EXPECT_EQ(cfg.l1_ttl_seconds, 30u);
  EXPECT_EQ(cfg.l2_max_size_mb, 256u);
  EXPECT_EQ(cfg.l2_block_cache_mb, 192u);
  EXPECT_EQ(cfg.l2_data_write_buffer_mb, 48u);
  EXPECT_EQ(cfg.l2_meta_write_buffer_mb, 12u);
  EXPECT_EQ(cfg.l2_data_max_write_buffer_number, 6u);
  EXPECT_EQ(cfg.l2_meta_max_write_buffer_number, 3u);
  EXPECT_EQ(cfg.l2_max_background_jobs, 10u);
  EXPECT_EQ(cfg.l2_max_background_flushes, 4u);
  EXPECT_EQ(cfg.l2_block_size, 8192u);
  EXPECT_DOUBLE_EQ(cfg.l2_bloom_filter_bits_per_key, 12.5);
  EXPECT_EQ(cfg.l2_ttl_seconds, 120u);
  EXPECT_EQ(cfg.l2_ttl_periodic_compaction_seconds, 3600u);
  EXPECT_FALSE(cfg.l2_strict_ttl_reads);
  EXPECT_TRUE(cfg.l2_scan_fill_cache);
  EXPECT_FALSE(cfg.l2_iter_pin_data);
  EXPECT_EQ(cfg.queue_capacity, 4096u);
  EXPECT_EQ(cfg.queue_worker_threads, 3u);
  EXPECT_EQ(cfg.queue_retry_count, 7u);
  EXPECT_EQ(cfg.queue_retry_delay_ms, 250u);
  EXPECT_EQ(cfg.account_cache_ttl_seconds, 45u);
  EXPECT_EQ(cfg.account_cache_max_entries, 2222u);
  EXPECT_EQ(cfg.dead_letter_max_items, 123u);
  EXPECT_TRUE(cfg.enable_outbox);
  EXPECT_EQ(cfg.outbox_max_items, 777u);
  EXPECT_TRUE(cfg.enable_access_control);
  EXPECT_TRUE(cfg.require_auth_for_reads);
  EXPECT_FALSE(cfg.enable_new_write_path);
  EXPECT_EQ(cfg.access_control_token, "token-xyz");
  ASSERT_EQ(cfg.critical_key_prefixes.size(), 3u);
  EXPECT_EQ(cfg.critical_key_prefixes[2], "trade:");
  ASSERT_EQ(cfg.sync_write_key_prefixes.size(), 2u);
  EXPECT_EQ(cfg.sync_write_key_prefixes[1], "trade:");

  std::error_code ec;
  std::filesystem::remove(path, ec);
}

TEST(ConfigManagerStorageEngineTest, UsesLegacyL2MaxSizeAsBlockCacheFallback) {
  const std::filesystem::path path =
      std::filesystem::temp_directory_path() /
      ("mir2_config_storage_engine_fallback_" +
       std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) +
       ".yaml");

  std::ofstream out(path);
  ASSERT_TRUE(out.is_open());
  out << "server:\n"
      << "  id: 1\n"
      << "  name: \"test\"\n"
      << "database:\n"
      << "  host: \"127.0.0.1\"\n"
      << "log:\n"
      << "  level: \"info\"\n"
      << "services:\n"
      << "  logic:\n"
      << "    host: \"127.0.0.1\"\n"
      << "    port: 8002\n"
      << "storage_engine:\n"
      << "  l2_max_size_mb: 321\n";
  out.close();

  ASSERT_TRUE(ConfigManager::Instance().Load(path.string()));
  const auto& cfg = ConfigManager::Instance().GetStorageEngineConfig();
  EXPECT_EQ(cfg.l2_max_size_mb, 321u);
  EXPECT_EQ(cfg.l2_block_cache_mb, 321u);

  std::error_code ec;
  std::filesystem::remove(path, ec);
}

}  // namespace
}  // namespace mir2::config
