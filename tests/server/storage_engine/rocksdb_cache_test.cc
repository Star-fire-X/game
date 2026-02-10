#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "storage_engine/l2/rocksdb_cache.h"

namespace mir2::storage_engine::l2 {
namespace {

class RocksDBCacheP1Test : public ::testing::Test {
 protected:
  void SetUp() override {
    db_path_ =
        "/tmp/mir2_storage_engine_l2_p1_" +
        std::to_string(std::chrono::steady_clock::now()
                           .time_since_epoch()
                           .count());

    config_.db_path = db_path_;
    config_.block_cache_size = 16 * 1024 * 1024;
    config_.ttl_seconds = 3600;
    config_.max_version_persist_step = 64;

    cache_ = std::make_unique<RocksDBCache>(config_);
    ASSERT_TRUE(cache_->Initialize());
  }

  void TearDown() override {
    cache_.reset();
    std::error_code ec;
    std::filesystem::remove_all(db_path_, ec);
  }

  static VersionedData MakeVersionedData(uint64_t version, uint8_t value) {
    return VersionedData{
        .version = version,
        .data = std::vector<uint8_t>{value},
        .timestamp_ms = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch())
                .count())};
  }

  RocksDBCache::Config config_;
  std::unique_ptr<RocksDBCache> cache_;
  std::string db_path_;
};

TEST_F(RocksDBCacheP1Test, MaxVersionTracksHighestWrite) {
  ASSERT_TRUE(cache_->Set("v100", MakeVersionedData(100, 1)));
  ASSERT_TRUE(cache_->Set("v200", MakeVersionedData(200, 2)));
  ASSERT_TRUE(cache_->Set("v150", MakeVersionedData(150, 3)));
  EXPECT_EQ(cache_->GetMaxVersion(), 200U);
}

TEST_F(RocksDBCacheP1Test, BatchSetTracksHighestVersion) {
  std::vector<std::pair<std::string, VersionedData>> batch{
      {"k1", MakeVersionedData(10, 1)},
      {"k2", MakeVersionedData(80, 2)},
      {"k3", MakeVersionedData(30, 3)}};
  ASSERT_TRUE(cache_->BatchSet(batch));
  EXPECT_EQ(cache_->GetMaxVersion(), 80U);
}

TEST_F(RocksDBCacheP1Test, DestructorFlushesMaxVersionEvenWhenPersistStepNotReached) {
  cache_.reset();

  config_.max_version_persist_step = 1000;
  cache_ = std::make_unique<RocksDBCache>(config_);
  ASSERT_TRUE(cache_->Initialize());

  ASSERT_TRUE(cache_->Set("key", MakeVersionedData(10, 7)));
  ASSERT_EQ(cache_->GetMaxVersion(), 10U);

  cache_.reset();

  auto reopened = std::make_unique<RocksDBCache>(config_);
  ASSERT_TRUE(reopened->Initialize());
  EXPECT_EQ(reopened->GetMaxVersion(), 10U);
}

}  // namespace
}  // namespace mir2::storage_engine::l2
