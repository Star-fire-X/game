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
      << "  l2_ttl_seconds: 120\n"
      << "  queue_capacity: 4096\n"
      << "  queue_worker_threads: 3\n"
      << "  queue_retry_count: 7\n"
      << "  queue_retry_delay_ms: 250\n"
      << "  dead_letter_max_items: 123\n"
      << "  enable_outbox: true\n"
      << "  outbox_max_items: 777\n"
      << "  enable_access_control: true\n"
      << "  require_auth_for_reads: true\n"
      << "  access_control_token: \"token-xyz\"\n"
      << "  critical_key_prefixes: [\"char:\", \"account:username:\", \"trade:\"]\n";
  out.close();

  ASSERT_TRUE(ConfigManager::Instance().Load(path.string()));
  const auto& cfg = ConfigManager::Instance().GetStorageEngineConfig();
  EXPECT_EQ(cfg.l1_max_entries, 2048u);
  EXPECT_EQ(cfg.l1_ttl_seconds, 30u);
  EXPECT_EQ(cfg.l2_ttl_seconds, 120u);
  EXPECT_EQ(cfg.queue_capacity, 4096u);
  EXPECT_EQ(cfg.queue_worker_threads, 3u);
  EXPECT_EQ(cfg.queue_retry_count, 7u);
  EXPECT_EQ(cfg.queue_retry_delay_ms, 250u);
  EXPECT_EQ(cfg.dead_letter_max_items, 123u);
  EXPECT_TRUE(cfg.enable_outbox);
  EXPECT_EQ(cfg.outbox_max_items, 777u);
  EXPECT_TRUE(cfg.enable_access_control);
  EXPECT_TRUE(cfg.require_auth_for_reads);
  EXPECT_EQ(cfg.access_control_token, "token-xyz");
  ASSERT_EQ(cfg.critical_key_prefixes.size(), 3u);
  EXPECT_EQ(cfg.critical_key_prefixes[2], "trade:");

  std::error_code ec;
  std::filesystem::remove(path, ec);
}

}  // namespace
}  // namespace mir2::config
