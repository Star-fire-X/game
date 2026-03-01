/**
 * @file async_test.cc
 * @brief StorageEngine coroutine async tests.
 */

#include <gtest/gtest.h>

#include <asio/executor_work_guard.hpp>
#include <asio/io_context.hpp>
#include <asio/post.hpp>

#include <chrono>
#include <cstring>
#include <filesystem>
#include <future>
#include <thread>
#include <vector>

#include "logic/coroutine_executor.h"
#include "storage_engine/l2/rocksdb_cache.h"
#include "storage_engine/persistence/async_persistence_queue.h"
#include "storage_engine/storage_engine.h"
#include "storage_engine/test_backend_mocks.h"

namespace mir2::storage_engine {
namespace {

class BlockingBackend : public test::NoopStorageBackend {
 public:
  BlockingBackend(std::shared_future<void> gate,
                  std::promise<std::thread::id>* load_thread_promise,
                  std::vector<uint8_t> payload,
                  uint64_t version)
      : gate_(std::move(gate)),
        load_thread_promise_(load_thread_promise),
        payload_(std::move(payload)),
        version_(version) {}

  std::optional<std::pair<uint64_t, std::vector<uint8_t>>> Load(const std::string&) override {
    if (load_thread_promise_) {
      load_thread_promise_->set_value(std::this_thread::get_id());
      load_thread_promise_ = nullptr;
    }
    gate_.wait();
    return std::make_pair(version_, payload_);
  }

 private:
  std::shared_future<void> gate_;
  std::promise<std::thread::id>* load_thread_promise_;
  std::vector<uint8_t> payload_;
  uint64_t version_;
};

class StorageEngineAsyncTest : public ::testing::Test {
 protected:
  void SetUp() override {
    if (StorageEngine::IsInitialized()) {
      StorageEngine::Shutdown();
    }

    auto backend = std::make_unique<test::NoopStorageBackend>();
    ASSERT_TRUE(StorageEngine::Initialize(std::move(backend)));
  }

  void TearDown() override {
    StorageEngine::Shutdown();
  }
};

class AlwaysUnhealthyBackend : public test::NoopStorageBackend {
 public:
  bool IsHealthy() const override {
    return false;
  }
};

class AlwaysFailBackend : public test::NoopStorageBackend {
 public:
  StorageResult Save(const std::string&,
                     uint64_t,
                     const std::vector<uint8_t>&) override {
    return StorageResult{false, "forced save failure", 0};
  }

  StorageResult SaveBatch(
      const std::vector<std::tuple<std::string, uint64_t, std::vector<uint8_t>>>&)
      override {
    return StorageResult{false, "forced batch failure", 0};
  }

  bool IsHealthy() const override {
    return true;
  }
};

class BatchCountingBackend : public test::NoopStorageBackend {
 public:
  StorageResult SaveBatch(
      const std::vector<std::tuple<std::string, uint64_t, std::vector<uint8_t>>>&)
      override {
    save_batch_calls.fetch_add(1, std::memory_order_relaxed);
    return StorageResult{true, "", 0};
  }

  bool IsHealthy() const override {
    return true;
  }

  std::atomic<uint32_t> save_batch_calls{0};
};

class AtomicBatchCountingBackend : public test::NoopStorageBackend,
                                   public IAtomicBatchStorageBackend {
 public:
  StorageResult SaveBatch(
      const std::vector<std::tuple<std::string, uint64_t, std::vector<uint8_t>>>&)
      override {
    save_batch_calls.fetch_add(1, std::memory_order_relaxed);
    return StorageResult{true, "", 0};
  }

  StorageResult SaveBatchAtomic(
      const std::vector<std::tuple<std::string, uint64_t, std::vector<uint8_t>>>&)
      override {
    save_batch_atomic_calls.fetch_add(1, std::memory_order_relaxed);
    return StorageResult{true, "", 0};
  }

  bool IsHealthy() const override {
    return true;
  }

  std::atomic<uint32_t> save_batch_calls{0};
  std::atomic<uint32_t> save_batch_atomic_calls{0};
};

class QueueBlockingBackend : public test::NoopStorageBackend {
 public:
  void SetBlockingKey(std::string key) {
    std::lock_guard<std::mutex> lock(block_mutex_);
    blocking_key_ = std::move(key);
    allow_blocked_save_ = false;
    block_entered_ = false;
  }

  bool WaitUntilBlocked(std::chrono::milliseconds timeout) {
    std::unique_lock<std::mutex> lock(block_mutex_);
    return block_cv_.wait_for(lock, timeout, [this] { return block_entered_; });
  }

  void Unblock() {
    std::lock_guard<std::mutex> lock(block_mutex_);
    allow_blocked_save_ = true;
    unblock_cv_.notify_all();
  }

  StorageResult SaveBatch(
      const std::vector<std::tuple<std::string, uint64_t, std::vector<uint8_t>>>& items)
      override {
    for (const auto& [key, version, data] : items) {
      (void)version;
      MaybeBlockOnKey(key);
      std::lock_guard<std::mutex> lock(store_mutex_);
      saved_[key] = data;
    }
    return StorageResult{true, "", 0};
  }

  StorageResult Save(const std::string& key,
                     uint64_t version,
                     const std::vector<uint8_t>& data) override {
    return SaveBatch({std::make_tuple(key, version, data)});
  }

  bool HasSavedKey(const std::string& key) const {
    std::lock_guard<std::mutex> lock(store_mutex_);
    return saved_.find(key) != saved_.end();
  }

 private:
  void MaybeBlockOnKey(const std::string& key) {
    std::unique_lock<std::mutex> lock(block_mutex_);
    if (blocking_key_.empty() || key != blocking_key_) {
      return;
    }
    block_entered_ = true;
    block_cv_.notify_all();
    unblock_cv_.wait(lock, [this] { return allow_blocked_save_; });
  }

  mutable std::mutex store_mutex_;
  std::map<std::string, std::vector<uint8_t>> saved_;

  std::mutex block_mutex_;
  std::condition_variable block_cv_;
  std::condition_variable unblock_cv_;
  std::string blocking_key_;
  bool allow_blocked_save_ = false;
  bool block_entered_ = false;
};

mir2::logic::Task<void> RunLoadFromDBAsyncTask(
    StorageEngine* engine,
    mir2::logic::CoroutineExecutor* executor,
    std::promise<std::optional<VersionedData>>* result_promise) {
  auto result = co_await engine->LoadFromDBAsync("async:db", *executor);
  result_promise->set_value(std::move(result));
  co_return;
}

}  // namespace

TEST_F(StorageEngineAsyncTest, GetAsyncL1HitDoesNotSuspend) {
  auto& engine = StorageEngine::Instance();
  engine.Set("async:l1", std::vector<uint8_t>{1, 2, 3});

  asio::io_context io_context;
  mir2::logic::CoroutineExecutor executor(io_context, 1);

  auto task = engine.GetAsync("async:l1", executor);
  auto handle = task.Release();
  ASSERT_TRUE(handle);
  handle.resume();

  EXPECT_EQ(executor.SuspendedCount(), 0);
}

TEST_F(StorageEngineAsyncTest, GetAsyncL1MissSuspends) {
  auto& engine = StorageEngine::Instance();

  asio::io_context io_context;
  mir2::logic::CoroutineExecutor executor(io_context, 1);

  auto task = engine.GetAsync("async:miss", executor);
  auto handle = task.Release();
  ASSERT_TRUE(handle);
  handle.resume();

  EXPECT_GT(executor.SuspendedCount(), 0);

  auto guard = asio::make_work_guard(io_context);
  std::thread io_thread([&]() { io_context.run(); });

  auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
  while (executor.SuspendedCount() > 0 && std::chrono::steady_clock::now() < deadline) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }

  guard.reset();
  io_context.stop();
  io_thread.join();

  EXPECT_EQ(executor.SuspendedCount(), 0);
}

TEST(StorageEngineAsyncLoadTest, LoadFromDBAsyncRunsOnBackgroundThread) {
  if (StorageEngine::IsInitialized()) {
    StorageEngine::Shutdown();
  }

  std::promise<void> gate_promise;
  auto gate_future = gate_promise.get_future().share();
  std::promise<std::thread::id> load_thread_promise;
  auto load_thread_future = load_thread_promise.get_future();

  auto backend = std::make_unique<BlockingBackend>(
      gate_future, &load_thread_promise, std::vector<uint8_t>{9, 9}, 7);
  ASSERT_TRUE(StorageEngine::Initialize(std::move(backend)));

  asio::io_context io_context;
  mir2::logic::CoroutineExecutor executor(io_context, 1);
  auto guard = asio::make_work_guard(io_context);

  std::promise<std::thread::id> io_thread_promise;
  auto io_thread_future = io_thread_promise.get_future();
  std::thread io_thread([&]() {
    io_thread_promise.set_value(std::this_thread::get_id());
    io_context.run();
  });

  std::promise<std::optional<VersionedData>> result_promise;
  auto result_future = result_promise.get_future();

  executor.Spawn(
      RunLoadFromDBAsyncTask(&StorageEngine::Instance(), &executor, &result_promise));

  ASSERT_EQ(load_thread_future.wait_for(std::chrono::seconds(1)), std::future_status::ready);
  auto load_thread_id = load_thread_future.get();

  std::promise<void> io_tick_promise;
  auto io_tick_future = io_tick_promise.get_future();
  asio::post(io_context, [&]() { io_tick_promise.set_value(); });

  EXPECT_EQ(io_tick_future.wait_for(std::chrono::milliseconds(200)),
            std::future_status::ready);

  auto io_thread_id = io_thread_future.get();
  EXPECT_NE(load_thread_id, io_thread_id);

  gate_promise.set_value();

  ASSERT_EQ(result_future.wait_for(std::chrono::seconds(2)), std::future_status::ready);
  auto result = result_future.get();
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->version, 7u);
  ASSERT_EQ(result->data.size(), 2u);
  EXPECT_EQ(result->data[0], 9);
  EXPECT_EQ(result->data[1], 9);

  guard.reset();
  io_context.stop();
  io_thread.join();
  StorageEngine::Shutdown();
}

TEST(AsyncPersistenceQueueP1TailTest, DurableOutboxRejectsWhenCapacityReached) {
  const std::string db_path =
      "/tmp/mir2_storage_engine_p1_outbox_limit_" +
      std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
  std::error_code ec;
  std::filesystem::remove_all(db_path, ec);

  {
    l2::RocksDBCache::Config l2_config;
    l2_config.db_path = db_path;
    l2_config.ttl_seconds = 3600;
    l2::RocksDBCache cache(l2_config);
    ASSERT_TRUE(cache.Initialize());

    AlwaysUnhealthyBackend backend;
    persistence::AsyncPersistenceQueue::Config queue_config;
    queue_config.enable_durable_outbox = true;
    queue_config.outbox_max_items = 1;
    queue_config.worker_threads = 2;
    queue_config.batch_interval_ms = 1000;
    queue_config.batch_size = 16;

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
    EXPECT_FALSE(queue.Enqueue("outbox:key:2", second, Priority::NORMAL));

    auto stats = queue.GetStats();
    EXPECT_EQ(stats.outbox_depth, 1U);
    EXPECT_EQ(stats.outbox_rejected, 1U);
  }
  std::filesystem::remove_all(db_path, ec);
}

TEST(AsyncPersistenceQueueP1TailTest, FailedItemsEnterDeadLetterQueue) {
  AlwaysFailBackend backend;
  persistence::AsyncPersistenceQueue::Config queue_config;
  queue_config.worker_threads = 2;
  queue_config.batch_size = 1;
  queue_config.batch_interval_ms = 20;
  queue_config.retry_count = 1;
  queue_config.retry_delay_ms = 10;
  queue_config.dead_letter_max_items = 16;

  persistence::AsyncPersistenceQueue queue(&backend, queue_config);
  VersionedData data{
      .version = 42,
      .data = std::vector<uint8_t>{4, 2},
      .timestamp_ms = 42};
  ASSERT_TRUE(queue.Enqueue("dead:letter:key", data, Priority::NORMAL));
  ASSERT_TRUE(queue.FlushAll(2000));

  const auto stats = queue.GetStats();
  EXPECT_EQ(stats.dead_letter_depth, 1U);
  EXPECT_EQ(stats.dead_letter_enqueued, 1U);
  EXPECT_EQ(stats.dead_letter_dropped, 0U);

  const auto snapshot = queue.GetDeadLetterSnapshot(8);
  ASSERT_EQ(snapshot.size(), 1U);
  EXPECT_EQ(snapshot[0].key, "dead:letter:key");
  EXPECT_EQ(snapshot[0].version, 42U);
  EXPECT_GE(snapshot[0].attempts, 1U);
}

TEST(AsyncPersistenceQueueP1TailTest, BatchWorkerFallsBackToSaveBatchWithoutAtomicSupport) {
  BatchCountingBackend backend;
  persistence::AsyncPersistenceQueue::Config queue_config;
  queue_config.worker_threads = 2;
  queue_config.batch_size = 1;
  queue_config.batch_interval_ms = 10;
  queue_config.retry_count = 0;
  queue_config.retry_delay_ms = 0;

  persistence::AsyncPersistenceQueue queue(&backend, queue_config);
  VersionedData data{
      .version = 6,
      .data = std::vector<uint8_t>{6},
      .timestamp_ms = 6};
  ASSERT_TRUE(queue.Enqueue("batch:non_atomic:key", data, Priority::NORMAL));
  ASSERT_TRUE(queue.FlushAll(2000));

  EXPECT_GT(backend.save_batch_calls.load(std::memory_order_relaxed), 0U);
}

TEST(AsyncPersistenceQueueP1TailTest, BatchWorkerUsesAtomicBatchSaveWhenSupported) {
  AtomicBatchCountingBackend backend;
  persistence::AsyncPersistenceQueue::Config queue_config;
  queue_config.worker_threads = 2;
  queue_config.batch_size = 1;
  queue_config.batch_interval_ms = 10;
  queue_config.retry_count = 0;
  queue_config.retry_delay_ms = 0;

  persistence::AsyncPersistenceQueue queue(&backend, queue_config);
  VersionedData data{
      .version = 7,
      .data = std::vector<uint8_t>{7},
      .timestamp_ms = 7};
  ASSERT_TRUE(queue.Enqueue("batch:atomic:key", data, Priority::NORMAL));
  ASSERT_TRUE(queue.FlushAll(2000));

  EXPECT_GT(backend.save_batch_atomic_calls.load(std::memory_order_relaxed), 0U);
  EXPECT_EQ(backend.save_batch_calls.load(std::memory_order_relaxed), 0U);
}

TEST(AsyncPersistenceQueueP1TailTest, CancelPendingForKeyDropsQueuedWrite) {
  QueueBlockingBackend backend;
  backend.SetBlockingKey("cancel:blocker");

  persistence::AsyncPersistenceQueue::Config queue_config;
  queue_config.worker_threads = 2;
  queue_config.batch_size = 1;
  queue_config.batch_interval_ms = 1;
  queue_config.retry_count = 0;
  queue_config.retry_delay_ms = 0;

  persistence::AsyncPersistenceQueue queue(&backend, queue_config);

  ASSERT_TRUE(queue.Enqueue(
      "cancel:blocker",
      VersionedData{1, std::vector<uint8_t>{1}, 1},
      Priority::NORMAL));
  ASSERT_TRUE(backend.WaitUntilBlocked(std::chrono::milliseconds(2000)));

  ASSERT_TRUE(queue.Enqueue(
      "cancel:target",
      VersionedData{2, std::vector<uint8_t>{2}, 2},
      Priority::NORMAL));

  const size_t canceled =
      queue.CancelPendingForKey("cancel:target", /*delete_version=*/10);
  EXPECT_EQ(canceled, 1U);

  backend.Unblock();
  ASSERT_TRUE(queue.FlushAll(5000));
  EXPECT_FALSE(backend.HasSavedKey("cancel:target"));
}

}  // namespace mir2::storage_engine
