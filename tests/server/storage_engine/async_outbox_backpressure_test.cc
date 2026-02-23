#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <string>

#include "storage_engine/l2/rocksdb_cache.h"
#include "storage_engine/persistence/async_persistence_queue.h"
#include "storage_engine/storage_engine.h"
#include "storage_engine/test_backend_mocks.h"

namespace mir2::storage_engine {
namespace {

class OutboxBlockingBackend : public test::NoopStorageBackend {
 public:
  StorageResult Save(const std::string&,
                     uint64_t,
                     const std::vector<uint8_t>&) override {
    sync_save_calls.fetch_add(1, std::memory_order_relaxed);
    return StorageResult{true, "", 0};
  }

  StorageResult SaveBatch(const BatchItems&) override {
    const uint32_t call_index =
        batch_save_calls.fetch_add(1, std::memory_order_relaxed);
    if (call_index == 0) {
      return StorageResult{false, "forced first batch save failure", 0};
    }
    return StorageResult{true, "", 0};
  }

  std::atomic<uint32_t> sync_save_calls{0};
  std::atomic<uint32_t> batch_save_calls{0};
};

class AlwaysFailSaveBackend : public test::NoopStorageBackend {
 public:
  StorageResult Save(const std::string&,
                     uint64_t,
                     const std::vector<uint8_t>&) override {
    save_calls.fetch_add(1, std::memory_order_relaxed);
    return StorageResult{false, "forced failure", 0};
  }

  std::atomic<uint32_t> save_calls{0};
};

storage_engine::StorageEngine::Config BuildOutboxTestConfig(const std::string& l2_path) {
  storage_engine::StorageEngine::Config config;
  config.l2_path = l2_path;
  config.enable_outbox = true;
  config.outbox_max_items = 1;
  config.auto_sync_interval_ms = 10;
  config.queue_retry_count = 0;
  config.queue_retry_delay_ms = 0;
  config.enable_strict_write_guarantee = false;
  config.sync_write_key_prefixes = {"char:"};
  config.critical_key_prefixes = {"char:"};
  return config;
}

std::string BuildTempPath(const char* prefix) {
  return std::string("/tmp/") + prefix + "_" +
         std::to_string(
             std::chrono::steady_clock::now().time_since_epoch().count());
}

TEST(StorageEngineOutboxBackpressureTest, CriticalWriteFallsBackToSyncPersistWhenOutboxIsFull) {
  if (StorageEngine::IsInitialized()) {
    StorageEngine::Shutdown();
  }

  const std::string l2_path = BuildTempPath("mir2_storage_engine_outbox_critical");
  std::error_code ec;
  std::filesystem::remove_all(l2_path, ec);

  auto backend = std::make_unique<OutboxBlockingBackend>();
  auto* backend_ptr = backend.get();
  auto config = BuildOutboxTestConfig(l2_path);
  ASSERT_TRUE(StorageEngine::Initialize(std::move(backend), config));

  auto& engine = StorageEngine::Instance();
  ASSERT_TRUE(engine.Set("normal:key:1", std::vector<uint8_t>{1}, Priority::NORMAL));
  ASSERT_TRUE(engine.Flush(2000));

  EXPECT_TRUE(engine.Set("char:critical:1",
                         std::vector<uint8_t>{2},
                         Priority::NORMAL));
  EXPECT_EQ(backend_ptr->batch_save_calls.load(std::memory_order_relaxed), 1U);
  EXPECT_EQ(backend_ptr->sync_save_calls.load(std::memory_order_relaxed), 1U);

  StorageEngine::Shutdown();
  std::filesystem::remove_all(l2_path, ec);
}

TEST(StorageEngineOutboxBackpressureTest, NonCriticalWriteFailsWhenOutboxIsFull) {
  if (StorageEngine::IsInitialized()) {
    StorageEngine::Shutdown();
  }

  const std::string l2_path = BuildTempPath("mir2_storage_engine_outbox_noncritical");
  std::error_code ec;
  std::filesystem::remove_all(l2_path, ec);

  auto backend = std::make_unique<OutboxBlockingBackend>();
  auto* backend_ptr = backend.get();
  auto config = BuildOutboxTestConfig(l2_path);
  ASSERT_TRUE(StorageEngine::Initialize(std::move(backend), config));

  auto& engine = StorageEngine::Instance();
  ASSERT_TRUE(engine.Set("normal:key:1", std::vector<uint8_t>{1}, Priority::NORMAL));
  ASSERT_TRUE(engine.Flush(2000));

  EXPECT_FALSE(engine.Set("normal:key:2",
                          std::vector<uint8_t>{2},
                          Priority::NORMAL));
  EXPECT_EQ(backend_ptr->batch_save_calls.load(std::memory_order_relaxed), 1U);
  EXPECT_EQ(backend_ptr->sync_save_calls.load(std::memory_order_relaxed), 0U);

  StorageEngine::Shutdown();
  std::filesystem::remove_all(l2_path, ec);
}

TEST(AsyncPersistenceQueueOutboxMetricTest, RejectedCriticalCounterIncrementsWhenOutboxFull) {
  const std::string db_path = BuildTempPath("mir2_async_outbox_metrics");
  std::error_code ec;
  std::filesystem::remove_all(db_path, ec);

  l2::RocksDBCache::Config l2_config;
  l2_config.db_path = db_path;
  l2_config.ttl_seconds = 3600;
  l2::RocksDBCache cache(l2_config);
  ASSERT_TRUE(cache.Initialize());

  AlwaysFailSaveBackend backend;
  persistence::AsyncPersistenceQueue::Config queue_config;
  queue_config.enable_durable_outbox = true;
  queue_config.outbox_max_items = 1;
  queue_config.worker_threads = 2;
  queue_config.batch_interval_ms = 1000;
  queue_config.batch_size = 16;
  queue_config.retry_count = 0;
  queue_config.retry_delay_ms = 0;

  persistence::AsyncPersistenceQueue queue(&backend, &cache, queue_config);
  VersionedData first{
      .version = 1,
      .data = std::vector<uint8_t>{1},
      .timestamp_ms = 1};
  VersionedData second{
      .version = 2,
      .data = std::vector<uint8_t>{2},
      .timestamp_ms = 2};

  EXPECT_TRUE(queue.Enqueue("outbox:key:1", first, Priority::NORMAL));
  EXPECT_FALSE(queue.Enqueue("outbox:key:2", second, Priority::CRITICAL));

  const auto stats = queue.GetStats();
  EXPECT_EQ(stats.outbox_rejected, 1U);
  EXPECT_EQ(stats.outbox_rejected_critical, 1U);

  std::filesystem::remove_all(db_path, ec);
}

}  // namespace
}  // namespace mir2::storage_engine
