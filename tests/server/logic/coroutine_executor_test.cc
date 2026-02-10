/**
 * @file coroutine_executor_test.cc
 * @brief Tests for coroutine executor.
 */

#include <gtest/gtest.h>

#include <asio/executor_work_guard.hpp>
#include <asio/io_context.hpp>

#include <chrono>
#include <cstring>
#include <future>
#include <string>
#include <thread>
#include <vector>

#include "logic/coroutine_executor.h"
#include "storage_engine/interfaces/storage_backend.h"
#include "storage_engine/storage_engine.h"

namespace mir2::logic {
namespace {

namespace storage_engine = mir2::storage_engine;

class MockBackend : public storage_engine::IStorageBackend {
 public:
  storage_engine::IStorageBackend::StorageResult Save(
      const std::string&, uint64_t, const std::vector<uint8_t>&) override {
    return storage_engine::IStorageBackend::StorageResult{true, "", 0};
  }

  storage_engine::IStorageBackend::StorageResult SaveBatch(
      const std::vector<std::tuple<std::string, uint64_t, std::vector<uint8_t>>>&) override {
    return storage_engine::IStorageBackend::StorageResult{true, "", 0};
  }

  std::optional<std::pair<uint64_t, std::vector<uint8_t>>> Load(const std::string&) override {
    return std::nullopt;
  }

  std::optional<std::map<std::string, std::pair<uint64_t, std::vector<uint8_t>>>> LoadAll()
      override {
    return std::nullopt;
  }

  storage_engine::IStorageBackend::StorageResult Validate() override {
    return storage_engine::IStorageBackend::StorageResult{true, "", 0};
  }

  bool IsHealthy() const override { return true; }
};

class CoroutineExecutorTest : public ::testing::Test {
 protected:
  void SetUp() override {
    if (storage_engine::StorageEngine::IsInitialized()) {
      storage_engine::StorageEngine::Shutdown();
    }

    auto backend = std::make_unique<MockBackend>();
    ASSERT_TRUE(storage_engine::StorageEngine::Initialize(std::move(backend)));
  }

  void TearDown() override { storage_engine::StorageEngine::Shutdown(); }
};

Task<void> RunAsyncStorageTask(CoroutineExecutor* executor,
                               std::promise<std::thread::id>* worker_thread_promise,
                               std::promise<void>* done_promise) {
  auto worker_id = co_await executor->Async([]() {
    auto& engine = storage_engine::StorageEngine::Instance();
    engine.Set("coroutine:test", std::vector<uint8_t>{1, 2, 3});
    return std::this_thread::get_id();
  });

  worker_thread_promise->set_value(worker_id);
  done_promise->set_value();
  co_return;
}

Task<void> RunAsyncExceptionTask(CoroutineExecutor* executor,
                                 std::promise<std::string>* result_promise) {
  try {
    co_await executor->Async([]() -> int {
      throw std::runtime_error("boom");
    });
    result_promise->set_value("no_error");
  } catch (const std::exception& ex) {
    result_promise->set_value(ex.what());
  }
  co_return;
}

Task<void> RunAsyncTimeoutTask(CoroutineExecutor* executor,
                               std::promise<std::string>* result_promise) {
  using namespace std::chrono_literals;

  try {
    co_await executor->AsyncWithTimeout([]() -> int {
      std::this_thread::sleep_for(50ms);
      return 1;
    }, 5ms);
    result_promise->set_value("no_timeout");
  } catch (const TimeoutError&) {
    result_promise->set_value("timeout");
  } catch (const std::exception&) {
    result_promise->set_value("other");
  }
  co_return;
}

Task<void> RunTimeoutThenSignalTask(CoroutineExecutor* executor,
                                    std::promise<void>* done_promise) {
  using namespace std::chrono_literals;
  try {
    co_await executor->AsyncWithTimeout([]() -> int {
      std::this_thread::sleep_for(50ms);
      return 7;
    }, 5ms);
  } catch (const TimeoutError&) {
    // Expected in this test.
  }

  done_promise->set_value();
  co_return;
}

Task<void> SignalTask(std::promise<void>* done_promise) {
  done_promise->set_value();
  co_return;
}

}  // namespace

TEST_F(CoroutineExecutorTest, AsyncStorageDoesNotBlockIoContext) {
  asio::io_context io_context;
  CoroutineExecutor executor(io_context, 1);
  auto guard = asio::make_work_guard(io_context);

  std::promise<std::thread::id> io_thread_promise;
  std::promise<std::thread::id> worker_thread_promise;
  std::promise<void> done_promise;

  auto done_future = done_promise.get_future();
  auto worker_future = worker_thread_promise.get_future();
  auto io_thread_future = io_thread_promise.get_future();

  std::thread io_thread([&]() {
    io_thread_promise.set_value(std::this_thread::get_id());
    io_context.run();
  });

  executor.Spawn(RunAsyncStorageTask(&executor, &worker_thread_promise, &done_promise));

  auto status = done_future.wait_for(std::chrono::seconds(2));
  guard.reset();
  io_context.stop();
  io_thread.join();

  ASSERT_EQ(status, std::future_status::ready);
  const auto io_thread_id = io_thread_future.get();
  const auto worker_thread_id = worker_future.get();
  EXPECT_NE(worker_thread_id, io_thread_id);
}

TEST_F(CoroutineExecutorTest, AsyncExceptionPropagates) {
  asio::io_context io_context;
  CoroutineExecutor executor(io_context, 1);
  auto guard = asio::make_work_guard(io_context);

  std::promise<std::string> result_promise;
  auto result_future = result_promise.get_future();

  std::thread io_thread([&]() { io_context.run(); });

  executor.Spawn(RunAsyncExceptionTask(&executor, &result_promise));

  auto status = result_future.wait_for(std::chrono::seconds(2));
  guard.reset();
  io_context.stop();
  io_thread.join();

  ASSERT_EQ(status, std::future_status::ready);
  EXPECT_EQ(result_future.get(), "boom");
}

TEST_F(CoroutineExecutorTest, AsyncTimeoutThrows) {
  using namespace std::chrono_literals;

  asio::io_context io_context;
  CoroutineExecutor executor(io_context, 1);
  auto guard = asio::make_work_guard(io_context);

  std::promise<std::string> result_promise;
  auto result_future = result_promise.get_future();

  std::thread io_thread([&]() { io_context.run(); });

  executor.Spawn(RunAsyncTimeoutTask(&executor, &result_promise));

  auto status = result_future.wait_for(std::chrono::seconds(2));
  guard.reset();
  io_context.stop();
  io_thread.join();

  ASSERT_EQ(status, std::future_status::ready);
  EXPECT_EQ(result_future.get(), "timeout");
}

TEST_F(CoroutineExecutorTest, StopAcceptingRejectsSpawnAndDrainCompletes) {
  using namespace std::chrono_literals;

  asio::io_context io_context;
  CoroutineExecutor executor(io_context, 1);
  auto guard = asio::make_work_guard(io_context);

  std::promise<void> timeout_done_promise;
  auto timeout_done_future = timeout_done_promise.get_future();

  std::thread io_thread([&]() { io_context.run(); });

  ASSERT_TRUE(executor.Spawn(RunTimeoutThenSignalTask(&executor, &timeout_done_promise)));
  ASSERT_EQ(timeout_done_future.wait_for(std::chrono::seconds(2)),
            std::future_status::ready);

  executor.StopAccepting();

  std::promise<void> rejected_task_promise;
  auto rejected_task_future = rejected_task_promise.get_future();
  EXPECT_FALSE(executor.Spawn(SignalTask(&rejected_task_promise)));
  EXPECT_EQ(rejected_task_future.wait_for(100ms), std::future_status::timeout);

  EXPECT_TRUE(executor.DrainAndJoin(std::chrono::seconds(2)));

  guard.reset();
  io_context.stop();
  io_thread.join();
}

}  // namespace mir2::logic
