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
#include <future>
#include <thread>
#include <vector>

#include "logic/coroutine_executor.h"
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

}  // namespace mir2::storage_engine
