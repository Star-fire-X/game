#include "apps/dead_letter_replay_compat.h"

#include <chrono>
#include <cstdio>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "storage_engine/l2/rocksdb_cache.h"

namespace mir2::apps {
namespace {

std::string MakeTempPath(const std::string& suffix) {
  return "/tmp/mir2_dead_letter_replay_compat_test_" + suffix + "_" +
         std::to_string(
             std::chrono::steady_clock::now().time_since_epoch().count());
}

TEST(DeadLetterReplayCompatTest, HelpPrintsUsageAndReturnsZero) {
  char arg0[] = "mir2_dead_letter_replay";
  char arg1[] = "--help";
  char* argv[] = {arg0, arg1};
  std::ostringstream out;
  std::ostringstream err;
  const int rc = RunDeadLetterReplayCompat(2, argv, &out, &err);
  EXPECT_EQ(rc, 0);
  EXPECT_NE(out.str().find("Usage: mir2_dead_letter_replay"),
            std::string::npos);
  EXPECT_TRUE(err.str().empty());
}

TEST(DeadLetterReplayCompatTest, MissingDbPathReturnsError) {
  char arg0[] = "mir2_dead_letter_replay";
  char* argv[] = {arg0};
  std::ostringstream out;
  std::ostringstream err;
  const int rc = RunDeadLetterReplayCompat(1, argv, &out, &err);
  EXPECT_EQ(rc, 1);
  EXPECT_NE(err.str().find("--db-path is required"), std::string::npos);
}

TEST(DeadLetterReplayCompatTest, DryRunKeepsDeadLetterRows) {
  const std::string db_path = MakeTempPath("dry_run_db");
  const std::string config_path = MakeTempPath("dry_run_yaml") + ".yaml";
  std::error_code ec;
  std::filesystem::remove_all(db_path, ec);
  std::filesystem::remove(config_path, ec);

  {
    std::ofstream out(config_path);
    out << "storage_engine:\n";
    out << "  l2_ttl_seconds: 7200\n";
  }

  {
    storage_engine::l2::RocksDBCache::Config cfg;
    cfg.db_path = db_path;
    cfg.enable_v2_encode = true;
    cfg.enable_v2_read_fallback = true;
    storage_engine::l2::RocksDBCache cache(cfg);
    ASSERT_TRUE(cache.Initialize());

    storage_engine::l2::RocksDBCache::DeadLetterEntry entry;
    entry.key = "compat:dead:dry";
    entry.priority = storage_engine::Priority::NORMAL;
    entry.attempts = 1;
    entry.recorded_at_ms = 1000;
    entry.error_message = "dry";
    entry.data.version = 1;
    entry.data.timestamp_ms = 1000;
    entry.data.data = {1, 2, 3};
    ASSERT_TRUE(cache.AppendDeadLetter(entry));
  }

  char arg0[] = "mir2_dead_letter_replay";
  char arg1[] = "--db-path";
  char arg2[512];
  std::snprintf(arg2, sizeof(arg2), "%s", db_path.c_str());
  char arg3[] = "--config";
  char arg4[512];
  std::snprintf(arg4, sizeof(arg4), "%s", config_path.c_str());
  char arg5[] = "--dry-run";
  char* argv[] = {arg0, arg1, arg2, arg3, arg4, arg5};

  std::ostringstream out;
  std::ostringstream err;
  const int rc = RunDeadLetterReplayCompat(6, argv, &out, &err);
  EXPECT_EQ(rc, 0);
  EXPECT_NE(out.str().find("storage_admin_dead_letter_replay_summary"),
            std::string::npos);
  EXPECT_NE(out.str().find("matched=1"), std::string::npos);
  EXPECT_NE(out.str().find("replayed=0"), std::string::npos);
  EXPECT_NE(out.str().find("acked=0"), std::string::npos);
  EXPECT_TRUE(err.str().empty());

  storage_engine::l2::RocksDBCache::Config verify_cfg;
  verify_cfg.db_path = db_path;
  verify_cfg.enable_v2_encode = true;
  verify_cfg.enable_v2_read_fallback = true;
  storage_engine::l2::RocksDBCache verify_cache(verify_cfg);
  ASSERT_TRUE(verify_cache.Initialize());
  EXPECT_EQ(verify_cache.DeadLetterDepth(), 1u);
  EXPECT_EQ(verify_cache.OutboxDepth(), 0u);

  std::filesystem::remove_all(db_path, ec);
  std::filesystem::remove(config_path, ec);
}

TEST(DeadLetterReplayCompatTest, ReplayAppendsOutboxAndAcksDeadLetter) {
  const std::string db_path = MakeTempPath("replay_db");
  std::error_code ec;
  std::filesystem::remove_all(db_path, ec);

  {
    storage_engine::l2::RocksDBCache::Config cfg;
    cfg.db_path = db_path;
    cfg.enable_v2_encode = true;
    cfg.enable_v2_read_fallback = true;
    storage_engine::l2::RocksDBCache cache(cfg);
    ASSERT_TRUE(cache.Initialize());

    storage_engine::l2::RocksDBCache::DeadLetterEntry entry;
    entry.key = "compat:dead:replay";
    entry.priority = storage_engine::Priority::HIGH;
    entry.attempts = 2;
    entry.recorded_at_ms = 2000;
    entry.error_message = "replay";
    entry.data.version = 2;
    entry.data.timestamp_ms = 2000;
    entry.data.data = {4, 5, 6};
    ASSERT_TRUE(cache.AppendDeadLetter(entry));
  }

  char arg0[] = "mir2_dead_letter_replay";
  char arg1[] = "--db-path";
  char arg2[512];
  std::snprintf(arg2, sizeof(arg2), "%s", db_path.c_str());
  char arg3[] = "--ttl-seconds";
  char arg4[] = "3600";
  char* argv[] = {arg0, arg1, arg2, arg3, arg4};

  std::ostringstream out;
  std::ostringstream err;
  const int rc = RunDeadLetterReplayCompat(5, argv, &out, &err);
  EXPECT_EQ(rc, 0);
  EXPECT_NE(out.str().find("storage_admin_dead_letter_replay_summary"),
            std::string::npos);
  EXPECT_NE(out.str().find("matched=1"), std::string::npos);
  EXPECT_NE(out.str().find("replayed=1"), std::string::npos);
  EXPECT_NE(out.str().find("acked=1"), std::string::npos);
  EXPECT_NE(out.str().find("failed=0"), std::string::npos);
  EXPECT_TRUE(err.str().empty());

  storage_engine::l2::RocksDBCache::Config verify_cfg;
  verify_cfg.db_path = db_path;
  verify_cfg.enable_v2_encode = true;
  verify_cfg.enable_v2_read_fallback = true;
  storage_engine::l2::RocksDBCache verify_cache(verify_cfg);
  ASSERT_TRUE(verify_cache.Initialize());
  EXPECT_EQ(verify_cache.DeadLetterDepth(), 0u);
  EXPECT_EQ(verify_cache.OutboxDepth(), 1u);

  std::filesystem::remove_all(db_path, ec);
}

}  // namespace
}  // namespace mir2::apps
