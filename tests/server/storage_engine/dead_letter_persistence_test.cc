#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <string>
#include <vector>

#include "storage_engine/l2/rocksdb_cache.h"
#include "storage_engine/persistence/async_persistence_queue.h"
#include "storage_engine/storage_engine.h"
#include "storage_engine/test_backend_mocks.h"

namespace mir2::storage_engine {
namespace {

class AlwaysFailBackend : public test::NoopStorageBackend {
 public:
  StorageResult Save(const std::string&, uint64_t,
                     const std::vector<uint8_t>&) override {
    return StorageResult{false, "forced save failure", 0};
  }

  StorageResult SaveBatch(const BatchItems&) override {
    return StorageResult{false, "forced batch failure", 0};
  }

  bool IsHealthy() const override { return true; }
};

class CountingSuccessBackend : public test::NoopStorageBackend {
 public:
  StorageResult Save(const std::string&, uint64_t,
                     const std::vector<uint8_t>&) override {
    save_calls.fetch_add(1, std::memory_order_relaxed);
    return StorageResult{true, "", 0};
  }

  StorageResult SaveBatch(const BatchItems& items) override {
    batch_save_calls.fetch_add(1, std::memory_order_relaxed);
    persisted_items.fetch_add(items.size(), std::memory_order_relaxed);
    return StorageResult{true, "", 0};
  }

  bool IsHealthy() const override { return true; }

  std::atomic<uint32_t> save_calls{0};
  std::atomic<uint32_t> batch_save_calls{0};
  std::atomic<size_t> persisted_items{0};
};

std::string BuildTempPath(const char* prefix) {
  return std::string("/tmp/") + prefix + "_" +
         std::to_string(
             std::chrono::steady_clock::now().time_since_epoch().count());
}

VersionedData MakeVersionedData(uint64_t version, uint8_t value) {
  return VersionedData{
      .version = version,
      .data = std::vector<uint8_t>{value},
      .timestamp_ms = version};
}

persistence::AsyncPersistenceQueue::Config BuildQueueConfig() {
  persistence::AsyncPersistenceQueue::Config config;
  config.enable_durable_outbox = true;
  config.queue_capacity = 64;
  config.worker_threads = 2;
  config.batch_size = 1;
  config.batch_interval_ms = 10;
  config.retry_count = 0;
  config.retry_delay_ms = 0;
  config.dead_letter_max_items = 32;
  config.outbox_replay_limit = 0;
  return config;
}

l2::RocksDBCache::Config BuildCacheConfig(const std::string& path) {
  l2::RocksDBCache::Config config;
  config.db_path = path;
  config.ttl_seconds = 3600;
  config.block_cache_size = 16 * 1024 * 1024;
  return config;
}

TEST(DeadLetterPersistenceTest, DurableDeadLettersSurviveRestartAndRestoreSnapshot) {
  const std::string db_path = BuildTempPath("mir2_dead_letter_restore");
  std::error_code ec;
  std::filesystem::remove_all(db_path, ec);

  {
    auto cache_config = BuildCacheConfig(db_path);
    l2::RocksDBCache cache(cache_config);
    ASSERT_TRUE(cache.Initialize());

    AlwaysFailBackend backend;
    auto queue_config = BuildQueueConfig();
    persistence::AsyncPersistenceQueue queue(&backend, &cache, queue_config);

    ASSERT_TRUE(queue.Enqueue(
        "dead:persist:key", MakeVersionedData(101, 1), Priority::HIGH));
    ASSERT_TRUE(queue.FlushAll(2000));

    const auto stats = queue.GetStats();
    EXPECT_EQ(stats.dead_letter_depth, 1U);
    EXPECT_EQ(stats.dead_letter_enqueued, 1U);
    EXPECT_EQ(cache.DeadLetterDepth(), 1U);
  }

  {
    auto cache_config = BuildCacheConfig(db_path);
    l2::RocksDBCache cache(cache_config);
    ASSERT_TRUE(cache.Initialize());
    EXPECT_EQ(cache.DeadLetterDepth(), 1U);

    std::vector<l2::RocksDBCache::DeadLetterEntry> replayed;
    const size_t replayed_count = cache.ReplayDeadLetter(
        10, [&replayed](const l2::RocksDBCache::DeadLetterEntry& entry) {
          replayed.push_back(entry);
          return true;
        });
    ASSERT_EQ(replayed_count, 1U);
    ASSERT_EQ(replayed.size(), 1U);
    EXPECT_EQ(replayed[0].key, "dead:persist:key");
    EXPECT_EQ(replayed[0].data.version, 101U);
    EXPECT_EQ(replayed[0].priority, Priority::HIGH);
    EXPECT_GT(replayed[0].dead_letter_id, 0U);

    CountingSuccessBackend success_backend;
    auto queue_config = BuildQueueConfig();
    persistence::AsyncPersistenceQueue queue(&success_backend, &cache,
                                             queue_config);

    const auto restored_stats = queue.GetStats();
    EXPECT_EQ(restored_stats.dead_letter_depth, 1U);

    const auto snapshot = queue.GetDeadLetterSnapshot(8);
    ASSERT_EQ(snapshot.size(), 1U);
    EXPECT_EQ(snapshot[0].key, "dead:persist:key");
    EXPECT_EQ(snapshot[0].version, 101U);
    EXPECT_GT(snapshot[0].dead_letter_id, 0U);
  }

  std::filesystem::remove_all(db_path, ec);
}

TEST(DeadLetterPersistenceTest, ReplayAndAckRemovesDurableDeadLetters) {
  const std::string db_path = BuildTempPath("mir2_dead_letter_replay");
  std::error_code ec;
  std::filesystem::remove_all(db_path, ec);

  {
    auto cache_config = BuildCacheConfig(db_path);
    l2::RocksDBCache cache(cache_config);
    ASSERT_TRUE(cache.Initialize());

    AlwaysFailBackend backend;
    auto queue_config = BuildQueueConfig();
    persistence::AsyncPersistenceQueue queue(&backend, &cache, queue_config);

    ASSERT_TRUE(queue.Enqueue(
        "dead:replay:key", MakeVersionedData(202, 2), Priority::NORMAL));
    ASSERT_TRUE(queue.FlushAll(2000));
    EXPECT_EQ(cache.DeadLetterDepth(), 1U);
    EXPECT_EQ(cache.OutboxDepth(), 1U);

    size_t replayed = 0;
    cache.ReplayDeadLetter(
        0, [&cache, &replayed](const l2::RocksDBCache::DeadLetterEntry& entry) {
          if (entry.durable_outbox_id == 0) {
            uint64_t outbox_id = 0;
            if (!cache.AppendOutbox(entry.key, entry.data, entry.priority,
                                    &outbox_id)) {
              return false;
            }
          }
          if (!cache.AckDeadLetter(entry.dead_letter_id)) {
            return false;
          }
          ++replayed;
          return true;
        });

    EXPECT_EQ(replayed, 1U);
    EXPECT_EQ(cache.DeadLetterDepth(), 0U);
    EXPECT_EQ(cache.OutboxDepth(), 1U);
  }

  {
    auto cache_config = BuildCacheConfig(db_path);
    l2::RocksDBCache cache(cache_config);
    ASSERT_TRUE(cache.Initialize());

    CountingSuccessBackend backend;
    auto queue_config = BuildQueueConfig();
    persistence::AsyncPersistenceQueue queue(&backend, &cache, queue_config);

    ASSERT_TRUE(queue.FlushAll(3000));
    EXPECT_GT(backend.persisted_items.load(std::memory_order_relaxed), 0U);
    EXPECT_EQ(cache.OutboxDepth(), 0U);
    EXPECT_EQ(cache.DeadLetterDepth(), 0U);
  }

  std::filesystem::remove_all(db_path, ec);
}

}  // namespace
}  // namespace mir2::storage_engine
