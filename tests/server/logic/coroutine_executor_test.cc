/**
 * @file coroutine_executor_test.cc
 * @brief Tests for coroutine executor.
 */

#include <gtest/gtest.h>

#include <asio/executor_work_guard.hpp>
#include <asio/io_context.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <future>
#include <iterator>
#include <memory>
#include <mutex>
#include <stop_token>
#include <string>
#include <thread>
#include <unordered_map>
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

class TestMetricsSink : public monitor::IMetricsSink {
 public:
  void IncrementCounter(const std::string& name) override {
    IncrementCounter(name, 1);
  }

  void IncrementCounter(const std::string& name, uint64_t delta) override {
    std::lock_guard<std::mutex> lock(mutex_);
    counters_[name] += delta;
  }

  void SetGauge(const std::string& name, double value) override {
    std::lock_guard<std::mutex> lock(mutex_);
    gauges_[name] = value;
  }

  uint64_t CounterValue(const std::string& name) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = counters_.find(name);
    if (it == counters_.end()) {
      return 0;
    }
    return it->second;
  }

  bool HasGauge(const std::string& name) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return gauges_.find(name) != gauges_.end();
  }

  double GaugeValue(const std::string& name) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = gauges_.find(name);
    if (it == gauges_.end()) {
      return 0.0;
    }
    return it->second;
  }

 private:
  mutable std::mutex mutex_;
  std::unordered_map<std::string, uint64_t> counters_;
  std::unordered_map<std::string, double> gauges_;
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

Task<void> BlockedTask(CoroutineExecutor* executor,
                       std::shared_future<void> unblock_signal,
                       std::promise<void>* done_promise) {
  co_await executor->Async([unblock_signal]() mutable { unblock_signal.wait(); });
  done_promise->set_value();
  co_return;
}

Task<void> BlockedMetadataTask(CoroutineExecutor* executor,
                               std::promise<void>* entered_promise,
                               std::shared_future<void> unblock_signal,
                               std::promise<void>* done_promise) {
  entered_promise->set_value();
  co_await executor->Async([unblock_signal]() mutable { unblock_signal.wait(); });
  done_promise->set_value();
  co_return;
}

Task<void> NonSuspendingBlockedTask(std::promise<void>* entered_promise,
                                    std::shared_future<void> unblock_signal,
                                    std::promise<void>* done_promise) {
  entered_promise->set_value();
  unblock_signal.wait();
  done_promise->set_value();
  co_return;
}

Task<void> SleepThenSignalTask(CoroutineExecutor* executor,
                               std::chrono::milliseconds duration,
                               std::promise<void>* done_promise) {
  co_await executor->SleepFor(duration);
  done_promise->set_value();
  co_return;
}

Task<void> TimeoutThenReportBackgroundCompletionTask(
    CoroutineExecutor* executor,
    std::chrono::milliseconds worker_duration,
    std::chrono::milliseconds timeout,
    std::atomic<bool>* timed_out,
    std::atomic<bool>* background_completed,
    std::promise<void>* coroutine_done) {
  try {
    co_await executor->AsyncWithTimeout(
        [worker_duration, background_completed]() {
          std::this_thread::sleep_for(worker_duration);
          background_completed->store(true, std::memory_order_release);
          return 1;
        },
        timeout);
  } catch (const TimeoutError&) {
    timed_out->store(true, std::memory_order_release);
  }

  coroutine_done->set_value();
  co_return;
}

Task<void> RunAsyncCancelledByStopTokenTask(CoroutineExecutor* executor,
                                            std::stop_token stop_token,
                                            std::promise<std::string>* result_promise) {
  using namespace std::chrono_literals;

  try {
    co_await executor->AsyncWithTimeout([]() -> int {
      std::this_thread::sleep_for(300ms);
      return 1;
    }, 5s, stop_token);
    result_promise->set_value("no_cancel");
  } catch (const CancelledError&) {
    result_promise->set_value("cancelled");
  } catch (const TimeoutError&) {
    result_promise->set_value("timeout");
  } catch (const std::exception&) {
    result_promise->set_value("other");
  }
  co_return;
}

Task<void> RunSleepCancelledByStopTokenTask(CoroutineExecutor* executor,
                                            std::stop_token stop_token,
                                            std::promise<std::string>* result_promise) {
  using namespace std::chrono_literals;
  try {
    co_await executor->SleepFor(5s, stop_token);
    result_promise->set_value("no_cancel");
  } catch (const CancelledError&) {
    result_promise->set_value("cancelled");
  } catch (const std::exception&) {
    result_promise->set_value("other");
  }
  co_return;
}

Task<void> RunAsyncPreCancelledTask(CoroutineExecutor* executor,
                                    std::stop_token stop_token,
                                    std::atomic<int>* executed_count,
                                    std::promise<std::string>* result_promise) {
  using namespace std::chrono_literals;

  try {
    co_await executor->AsyncWithTimeout([executed_count]() -> int {
      executed_count->fetch_add(1, std::memory_order_relaxed);
      return 1;
    }, 1s, stop_token);
    result_promise->set_value("no_cancel");
  } catch (const CancelledError&) {
    result_promise->set_value("cancelled");
  } catch (const std::exception&) {
    result_promise->set_value("other");
  }
  co_return;
}

Task<void> RunDelayedCounterTask(CoroutineExecutor* executor,
                                 std::chrono::milliseconds delay,
                                 std::atomic<int>* completed_counter) {
  co_await executor->SleepFor(delay);
  completed_counter->fetch_add(1, std::memory_order_relaxed);
  co_return;
}

Task<void> RunWhenAllProbeTask(CoroutineExecutor* executor,
                               std::atomic<int>* completed_counter,
                               std::promise<void>* done_promise) {
  using namespace std::chrono_literals;
  std::vector<Task<void>> tasks;
  tasks.reserve(3);
  tasks.push_back(RunDelayedCounterTask(executor, 60ms, completed_counter));
  tasks.push_back(RunDelayedCounterTask(executor, 20ms, completed_counter));
  tasks.push_back(RunDelayedCounterTask(executor, 40ms, completed_counter));
  co_await executor->WhenAll(std::move(tasks));
  done_promise->set_value();
  co_return;
}

Task<void> RunWhenAnyProbeTask(CoroutineExecutor* executor,
                               std::promise<size_t>* winner_promise) {
  using namespace std::chrono_literals;
  std::vector<Task<void>> tasks;
  tasks.reserve(3);
  tasks.push_back(executor->SleepFor(120ms));
  tasks.push_back(executor->SleepFor(20ms));
  tasks.push_back(executor->SleepFor(80ms));
  const size_t winner = co_await executor->WhenAny(std::move(tasks));
  winner_promise->set_value(winner);
  co_return;
}

Task<void> RunWhenAllOverLimitProbeTask(CoroutineExecutor* executor,
                                        std::atomic<int>* over_limit_rejected,
                                        std::promise<void>* done_promise) {
  using namespace std::chrono_literals;
  std::vector<Task<void>> tasks;
  tasks.push_back(executor->SleepFor(5ms));
  co_await executor->WhenAll(
      std::move(tasks),
      [over_limit_rejected](size_t /*index*/, SpawnResult reason) {
        if (reason == SpawnResult::kOverLimit) {
          over_limit_rejected->fetch_add(1, std::memory_order_relaxed);
        }
      });
  done_promise->set_value();
  co_return;
}

Task<void> ThrowUnhandledTask() {
  throw std::runtime_error("task-unhandled");
  co_return;
}

Task<void> AwaitAndCatchUnhandledTask(std::promise<void>* done_promise) {
  try {
    co_await ThrowUnhandledTask();
  } catch (const std::exception&) {
  }
  done_promise->set_value();
  co_return;
}

std::atomic<int>* g_unhandled_exception_report_count = nullptr;
std::atomic<int>* g_detached_exception_report_count = nullptr;

void CountUnhandledExceptionReport(bool /*detached*/, const std::exception_ptr& /*exception*/) {
  if (g_unhandled_exception_report_count != nullptr) {
    g_unhandled_exception_report_count->fetch_add(1, std::memory_order_relaxed);
  }
}

void CountDetachedExceptionReport(const std::exception_ptr& /*exception*/) {
  if (g_detached_exception_report_count != nullptr) {
    g_detached_exception_report_count->fetch_add(1, std::memory_order_relaxed);
  }
}

class TaskExceptionReporterScope {
 public:
  TaskExceptionReporterScope(std::atomic<int>* unhandled_report_count,
                             std::atomic<int>* detached_report_count)
      : previous_unhandled_reporter_(
            SetTaskUnhandledExceptionReporter(&CountUnhandledExceptionReport)),
        previous_detached_reporter_(
            SetTaskDetachedExceptionReporter(&CountDetachedExceptionReport)) {
    g_unhandled_exception_report_count = unhandled_report_count;
    g_detached_exception_report_count = detached_report_count;
  }

  ~TaskExceptionReporterScope() {
    g_unhandled_exception_report_count = nullptr;
    g_detached_exception_report_count = nullptr;
    SetTaskUnhandledExceptionReporter(previous_unhandled_reporter_);
    SetTaskDetachedExceptionReporter(previous_detached_reporter_);
  }

 private:
  TaskUnhandledExceptionReporter previous_unhandled_reporter_ = nullptr;
  TaskDetachedExceptionReporter previous_detached_reporter_ = nullptr;
};

Task<void> CountExecutedTask(std::atomic<int>* executed_counter) {
  executed_counter->fetch_add(1, std::memory_order_relaxed);
  co_return;
}

bool WaitUntil(const std::function<bool()>& condition,
               std::chrono::steady_clock::duration timeout) {
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    if (condition()) {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  return condition();
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

TEST_F(CoroutineExecutorTest, AsyncWithTimeoutStopTokenCancelsAwaiterEarly) {
  using namespace std::chrono_literals;

  asio::io_context io_context;
  CoroutineExecutor executor(io_context, 1);
  auto guard = asio::make_work_guard(io_context);

  std::stop_source stop_source;
  std::promise<std::string> result_promise;
  auto result_future = result_promise.get_future();

  std::thread io_thread([&]() { io_context.run(); });

  ASSERT_TRUE(executor.Spawn(RunAsyncCancelledByStopTokenTask(
      &executor, stop_source.get_token(), &result_promise)));

  std::this_thread::sleep_for(20ms);
  (void)stop_source.request_stop();

  ASSERT_EQ(result_future.wait_for(std::chrono::seconds(2)),
            std::future_status::ready);
  EXPECT_EQ(result_future.get(), "cancelled");

  EXPECT_TRUE(executor.DrainAndJoin(std::chrono::seconds(2)));
  EXPECT_EQ(executor.TimeoutBackgroundInflightCount(), 0);

  guard.reset();
  io_context.stop();
  io_thread.join();
}

TEST_F(CoroutineExecutorTest, SleepForStopTokenCancelsAwaiterEarly) {
  using namespace std::chrono_literals;

  asio::io_context io_context;
  CoroutineExecutor executor(io_context, 1);
  auto guard = asio::make_work_guard(io_context);

  std::stop_source stop_source;
  std::promise<std::string> result_promise;
  auto result_future = result_promise.get_future();

  std::thread io_thread([&]() { io_context.run(); });

  ASSERT_TRUE(executor.Spawn(RunSleepCancelledByStopTokenTask(
      &executor, stop_source.get_token(), &result_promise)));

  std::this_thread::sleep_for(20ms);
  (void)stop_source.request_stop();

  ASSERT_EQ(result_future.wait_for(std::chrono::seconds(2)),
            std::future_status::ready);
  EXPECT_EQ(result_future.get(), "cancelled");

  EXPECT_TRUE(executor.DrainAndJoin(std::chrono::seconds(2)));

  guard.reset();
  io_context.stop();
  io_thread.join();
}

TEST_F(CoroutineExecutorTest, WhenAllWaitsForAllTasks) {
  using namespace std::chrono_literals;

  asio::io_context io_context;
  CoroutineExecutor executor(io_context, 1);
  auto guard = asio::make_work_guard(io_context);

  std::atomic<int> completed_counter{0};
  std::promise<void> done_promise;
  auto done_future = done_promise.get_future();

  std::thread io_thread([&]() { io_context.run(); });

  ASSERT_TRUE(executor.Spawn(
      RunWhenAllProbeTask(&executor, &completed_counter, &done_promise)));
  ASSERT_EQ(done_future.wait_for(2s), std::future_status::ready);
  EXPECT_EQ(completed_counter.load(std::memory_order_relaxed), 3);

  EXPECT_TRUE(executor.DrainAndJoin(std::chrono::seconds(2)));

  guard.reset();
  io_context.stop();
  io_thread.join();
}

TEST_F(CoroutineExecutorTest, WhenAnyReturnsFirstCompletedTaskIndex) {
  using namespace std::chrono_literals;

  asio::io_context io_context;
  CoroutineExecutor executor(io_context, 1);
  auto guard = asio::make_work_guard(io_context);

  std::promise<size_t> winner_promise;
  auto winner_future = winner_promise.get_future();

  std::thread io_thread([&]() { io_context.run(); });

  ASSERT_TRUE(executor.Spawn(RunWhenAnyProbeTask(&executor, &winner_promise)));
  ASSERT_EQ(winner_future.wait_for(2s), std::future_status::ready);
  EXPECT_EQ(winner_future.get(), 1u);

  EXPECT_TRUE(executor.DrainAndJoin(std::chrono::seconds(2)));

  guard.reset();
  io_context.stop();
  io_thread.join();
}

TEST_F(CoroutineExecutorTest, WhenAllSpawnRejectUsesCallbackWithoutThrowing) {
  using namespace std::chrono_literals;

  asio::io_context io_context;
  CoroutineExecutor executor(io_context, 1, 1);
  auto guard = asio::make_work_guard(io_context);

  std::atomic<int> over_limit_rejected{0};
  std::promise<void> done_promise;
  auto done_future = done_promise.get_future();

  std::thread io_thread([&]() { io_context.run(); });

  ASSERT_TRUE(executor.Spawn(
      RunWhenAllOverLimitProbeTask(&executor, &over_limit_rejected, &done_promise)));
  ASSERT_EQ(done_future.wait_for(2s), std::future_status::ready);
  EXPECT_EQ(over_limit_rejected.load(std::memory_order_relaxed), 1);

  EXPECT_TRUE(executor.DrainAndJoin(std::chrono::seconds(2)));

  guard.reset();
  io_context.stop();
  io_thread.join();
}

TEST_F(CoroutineExecutorTest, PreCancelledAsyncWithTimeoutSkipsBlockingWorkDispatch) {
  using namespace std::chrono_literals;

  asio::io_context io_context;
  CoroutineExecutor executor(io_context, 1);
  auto guard = asio::make_work_guard(io_context);

  std::stop_source stop_source;
  (void)stop_source.request_stop();

  std::atomic<int> executed_count{0};
  std::promise<std::string> result_promise;
  auto result_future = result_promise.get_future();

  std::thread io_thread([&]() { io_context.run(); });

  ASSERT_TRUE(executor.Spawn(RunAsyncPreCancelledTask(
      &executor,
      stop_source.get_token(),
      &executed_count,
      &result_promise)));

  ASSERT_EQ(result_future.wait_for(std::chrono::seconds(2)),
            std::future_status::ready);
  EXPECT_EQ(result_future.get(), "cancelled");
  EXPECT_EQ(executed_count.load(std::memory_order_relaxed), 0);

  EXPECT_TRUE(executor.DrainAndJoin(std::chrono::seconds(2)));

  guard.reset();
  io_context.stop();
  io_thread.join();
}

TEST_F(CoroutineExecutorTest, AwaitedUnhandledExceptionInvokesReporterOnly) {
  using namespace std::chrono_literals;

  asio::io_context io_context;
  CoroutineExecutor executor(io_context, 1);
  auto guard = asio::make_work_guard(io_context);

  std::atomic<int> unhandled_report_count{0};
  std::atomic<int> detached_report_count{0};
  TaskExceptionReporterScope reporter_scope(
      &unhandled_report_count, &detached_report_count);

  std::promise<void> done_promise;
  auto done_future = done_promise.get_future();

  std::thread io_thread([&]() { io_context.run(); });

  ASSERT_TRUE(executor.Spawn(AwaitAndCatchUnhandledTask(&done_promise)));
  ASSERT_EQ(done_future.wait_for(std::chrono::seconds(2)),
            std::future_status::ready);

  const auto deadline = std::chrono::steady_clock::now() + 500ms;
  while (std::chrono::steady_clock::now() < deadline &&
         unhandled_report_count.load(std::memory_order_relaxed) == 0) {
    std::this_thread::sleep_for(1ms);
  }

  EXPECT_GE(unhandled_report_count.load(std::memory_order_relaxed), 1);
  EXPECT_EQ(detached_report_count.load(std::memory_order_relaxed), 0);

  EXPECT_TRUE(executor.DrainAndJoin(std::chrono::seconds(2)));

  guard.reset();
  io_context.stop();
  io_thread.join();
}

TEST_F(CoroutineExecutorTest, DetachedUnhandledExceptionInvokesTerminalReporter) {
  using namespace std::chrono_literals;

  asio::io_context io_context;
  CoroutineExecutor executor(io_context, 1);
  auto guard = asio::make_work_guard(io_context);

  std::atomic<int> unhandled_report_count{0};
  std::atomic<int> detached_report_count{0};
  TaskExceptionReporterScope reporter_scope(
      &unhandled_report_count, &detached_report_count);

  std::thread io_thread([&]() { io_context.run(); });

  ASSERT_TRUE(executor.Spawn(ThrowUnhandledTask()));

  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
  while (std::chrono::steady_clock::now() < deadline &&
         (unhandled_report_count.load(std::memory_order_relaxed) == 0 ||
          detached_report_count.load(std::memory_order_relaxed) == 0)) {
    std::this_thread::sleep_for(1ms);
  }

  EXPECT_GE(unhandled_report_count.load(std::memory_order_relaxed), 1);
  EXPECT_GE(detached_report_count.load(std::memory_order_relaxed), 1);

  EXPECT_TRUE(executor.DrainAndJoin(std::chrono::seconds(2)));

  guard.reset();
  io_context.stop();
  io_thread.join();
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

TEST_F(CoroutineExecutorTest, SpawnOrDropReportsNotAcceptingReason) {
  asio::io_context io_context;
  CoroutineExecutor executor(io_context, 1);

  executor.StopAccepting();

  bool callback_called = false;
  SpawnResult callback_reason = SpawnResult::kSpawned;
  std::promise<void> rejected_task_promise;
  auto rejected_task_future = rejected_task_promise.get_future();

  const SpawnResult result = executor.SpawnOrDrop(
      SignalTask(&rejected_task_promise),
      [&callback_called, &callback_reason](SpawnResult reason) {
        callback_called = true;
        callback_reason = reason;
      });

  EXPECT_EQ(result, SpawnResult::kNotAccepting);
  EXPECT_TRUE(callback_called);
  EXPECT_EQ(callback_reason, SpawnResult::kNotAccepting);
  EXPECT_EQ(rejected_task_future.wait_for(std::chrono::milliseconds(50)),
            std::future_status::timeout);
}

TEST_F(CoroutineExecutorTest, InjectedMetricsSinkCapturesExecutorMetrics) {
  asio::io_context io_context;
  auto metrics_sink = std::make_shared<TestMetricsSink>();
  CoroutineExecutor executor(io_context, 1, 1, metrics_sink);

  ASSERT_TRUE(metrics_sink->HasGauge("logic.coroutine.accepting"));
  EXPECT_DOUBLE_EQ(metrics_sink->GaugeValue("logic.coroutine.accepting"), 1.0);
  EXPECT_DOUBLE_EQ(metrics_sink->GaugeValue("logic.coroutine.max_running_limit"), 1.0);

  executor.StopAccepting();
  EXPECT_DOUBLE_EQ(metrics_sink->GaugeValue("logic.coroutine.accepting"), 0.0);

  std::promise<void> rejected_task_promise;
  auto rejected_task_future = rejected_task_promise.get_future();
  EXPECT_EQ(executor.TrySpawn(SignalTask(&rejected_task_promise)),
            SpawnResult::kNotAccepting);
  EXPECT_EQ(rejected_task_future.wait_for(std::chrono::milliseconds(50)),
            std::future_status::timeout);

  EXPECT_EQ(metrics_sink->CounterValue("logic.coroutine.spawn_rejected_total"), 1u);
  EXPECT_EQ(metrics_sink->CounterValue("logic.coroutine.spawn_rejected_not_accepting_total"),
            1u);
}

TEST_F(CoroutineExecutorTest, TrySpawnReturnsOverLimitWhenMaxRunningReached) {
  using namespace std::chrono_literals;

  asio::io_context io_context;
  CoroutineExecutor executor(io_context, 1, 1);
  auto guard = asio::make_work_guard(io_context);

  std::promise<void> unblock_promise;
  auto unblock_signal = unblock_promise.get_future().share();
  std::promise<void> blocked_done_promise;
  auto blocked_done_future = blocked_done_promise.get_future();

  std::thread io_thread([&]() { io_context.run(); });

  ASSERT_EQ(executor.TrySpawn(
                BlockedTask(&executor, unblock_signal, &blocked_done_promise)),
            SpawnResult::kSpawned);

  std::promise<void> rejected_task_promise;
  auto rejected_task_future = rejected_task_promise.get_future();
  EXPECT_EQ(executor.TrySpawn(SignalTask(&rejected_task_promise)),
            SpawnResult::kOverLimit);
  EXPECT_EQ(rejected_task_future.wait_for(50ms), std::future_status::timeout);

  unblock_promise.set_value();
  EXPECT_EQ(blocked_done_future.wait_for(std::chrono::seconds(2)),
            std::future_status::ready);
  EXPECT_TRUE(executor.DrainAndJoin(std::chrono::seconds(2)));

  guard.reset();
  io_context.stop();
  io_thread.join();
}

TEST_F(CoroutineExecutorTest, QueuedCallbackAfterExecutorDestructionDoesNotTouchDanglingState) {
  using namespace std::chrono_literals;

  asio::io_context io_context;
  auto guard = asio::make_work_guard(io_context);
  std::thread io_thread([&]() { io_context.run(); });

  std::promise<void> unblock_promise;
  auto unblock_signal = unblock_promise.get_future().share();
  std::promise<void> blocked_done_promise;
  auto blocked_done_future = blocked_done_promise.get_future();

  {
    auto executor = std::make_unique<CoroutineExecutor>(io_context, 1);
    ASSERT_EQ(executor->TrySpawn(
                  BlockedTask(executor.get(), unblock_signal, &blocked_done_promise)),
              SpawnResult::kSpawned);

    const auto wait_deadline = std::chrono::steady_clock::now() + 1s;
    while (std::chrono::steady_clock::now() < wait_deadline &&
           executor->SuspendedCount() == 0) {
      std::this_thread::sleep_for(1ms);
    }
    ASSERT_GT(executor->SuspendedCount(), 0);

    guard.reset();
    io_context.stop();
    io_thread.join();

    unblock_promise.set_value();
    executor.reset();
  }

  io_context.restart();
  io_context.run_for(200ms);

  EXPECT_EQ(blocked_done_future.wait_for(50ms), std::future_status::timeout);
}

TEST_F(CoroutineExecutorTest, RepeatedStartStopDrainWithInFlightTasks) {
  using namespace std::chrono_literals;

  for (int round = 0; round < 40; ++round) {
    asio::io_context io_context;
    CoroutineExecutor executor(io_context, 1);
    auto guard = asio::make_work_guard(io_context);

    std::promise<void> sleep_done_promise;
    std::promise<void> timeout_done_promise;
    auto sleep_done_future = sleep_done_promise.get_future();
    auto timeout_done_future = timeout_done_promise.get_future();

    std::thread io_thread([&]() { io_context.run(); });

    ASSERT_TRUE(executor.Spawn(
        SleepThenSignalTask(&executor, std::chrono::milliseconds(20), &sleep_done_promise)));
    ASSERT_TRUE(executor.Spawn(
        RunTimeoutThenSignalTask(&executor, &timeout_done_promise)));

    executor.StopAccepting();
    EXPECT_TRUE(executor.DrainAndJoin(std::chrono::seconds(2))) << "round=" << round;
    EXPECT_EQ(sleep_done_future.wait_for(100ms), std::future_status::ready)
        << "round=" << round;
    EXPECT_EQ(timeout_done_future.wait_for(100ms), std::future_status::ready)
        << "round=" << round;
    EXPECT_EQ(executor.RunningCount(), 0) << "round=" << round;
    EXPECT_EQ(executor.SuspendedCount(), 0) << "round=" << round;

    guard.reset();
    io_context.stop();
    io_thread.join();
  }
}

TEST_F(CoroutineExecutorTest, TimeoutDoesNotCancelBackgroundWorkCompletion) {
  using namespace std::chrono_literals;

  asio::io_context io_context;
  CoroutineExecutor executor(io_context, 1);
  auto guard = asio::make_work_guard(io_context);

  std::atomic<bool> timed_out{false};
  std::atomic<bool> background_completed{false};
  std::promise<void> coroutine_done_promise;
  auto coroutine_done_future = coroutine_done_promise.get_future();

  std::thread io_thread([&]() { io_context.run(); });

  ASSERT_TRUE(executor.Spawn(TimeoutThenReportBackgroundCompletionTask(
      &executor,
      std::chrono::milliseconds(400),
      std::chrono::milliseconds(10),
      &timed_out,
      &background_completed,
      &coroutine_done_promise)));

  ASSERT_EQ(coroutine_done_future.wait_for(std::chrono::seconds(2)),
            std::future_status::ready);
  EXPECT_TRUE(timed_out.load(std::memory_order_acquire));
  EXPECT_FALSE(background_completed.load(std::memory_order_acquire));

  const auto worker_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
  while (std::chrono::steady_clock::now() < worker_deadline &&
         !background_completed.load(std::memory_order_acquire)) {
    std::this_thread::sleep_for(5ms);
  }
  EXPECT_TRUE(background_completed.load(std::memory_order_acquire));

  EXPECT_TRUE(executor.DrainAndJoin(std::chrono::seconds(2)));
  EXPECT_EQ(executor.RunningCount(), 0);
  EXPECT_EQ(executor.SuspendedCount(), 0);

  guard.reset();
  io_context.stop();
  io_thread.join();
}

TEST_F(CoroutineExecutorTest, OverLimitRejectionsDoNotCorruptRunningCount) {
  using namespace std::chrono_literals;

  asio::io_context io_context;
  CoroutineExecutor executor(io_context, 1, 1);
  auto guard = asio::make_work_guard(io_context);

  std::promise<void> unblock_promise;
  auto unblock_signal = unblock_promise.get_future().share();
  std::promise<void> blocked_done_promise;
  auto blocked_done_future = blocked_done_promise.get_future();
  std::atomic<int> rejected_task_executed{0};

  std::thread io_thread([&]() { io_context.run(); });

  ASSERT_EQ(executor.TrySpawn(
                BlockedTask(&executor, unblock_signal, &blocked_done_promise)),
            SpawnResult::kSpawned);
  ASSERT_EQ(executor.RunningCount(), 1);

  for (int i = 0; i < 200; ++i) {
    EXPECT_EQ(executor.TrySpawn(CountExecutedTask(&rejected_task_executed)),
              SpawnResult::kOverLimit);
    EXPECT_EQ(executor.RunningCount(), 1) << "attempt=" << i;
  }

  unblock_promise.set_value();
  EXPECT_EQ(blocked_done_future.wait_for(std::chrono::seconds(2)),
            std::future_status::ready);
  EXPECT_TRUE(executor.DrainAndJoin(std::chrono::seconds(2)));
  EXPECT_EQ(executor.RunningCount(), 0);
  EXPECT_EQ(executor.SuspendedCount(), 0);
  EXPECT_EQ(rejected_task_executed.load(std::memory_order_relaxed), 0);

  guard.reset();
  io_context.stop();
  io_thread.join();
}

TEST_F(CoroutineExecutorTest, SnapshotTracksCoroutineMetadataAndLifecycle) {
  using namespace std::chrono_literals;

  asio::io_context io_context;
  CoroutineExecutor executor(io_context, 1);
  auto guard = asio::make_work_guard(io_context);

  std::promise<void> entered_promise;
  auto entered_future = entered_promise.get_future();
  std::promise<void> unblock_promise;
  auto unblock_signal = unblock_promise.get_future().share();
  std::promise<void> done_promise;
  auto done_future = done_promise.get_future();

  std::thread io_thread([&]() { io_context.run(); });

  CoroutineMetadata metadata;
  metadata.trace_id = 9001;
  metadata.name = "handler.snapshot_test";
  metadata.source = "unit_test";
  metadata.handler_key = "snapshot_test";
  metadata.msg_id = 2201;
  metadata.client_id = 7788;

  ASSERT_EQ(executor.TrySpawn(BlockedMetadataTask(
                &executor, &entered_promise, unblock_signal, &done_promise),
            std::move(metadata)),
            SpawnResult::kSpawned);
  ASSERT_EQ(entered_future.wait_for(2s), std::future_status::ready);
  ASSERT_TRUE(WaitUntil([&executor]() { return executor.SuspendedCount() > 0; }, 2s));

  const auto snapshots = executor.SnapshotActiveCoroutines();
  ASSERT_FALSE(snapshots.empty());
  const auto it = std::find_if(
      snapshots.begin(),
      snapshots.end(),
      [](const CoroutineSnapshot& snapshot) {
        return snapshot.name == "handler.snapshot_test";
      });
  ASSERT_NE(it, snapshots.end());
  EXPECT_EQ(it->trace_id, 9001u);
  EXPECT_EQ(it->msg_id, 2201u);
  EXPECT_EQ(it->client_id, 7788u);
  EXPECT_EQ(it->handler_key, "snapshot_test");
  EXPECT_EQ(it->status, CoroutineStatus::kSuspendedAsync);
  EXPECT_GT(it->suspend_count, 0u);

  const auto single_snapshot = executor.SnapshotCoroutine(it->coroutine_id);
  ASSERT_TRUE(single_snapshot.has_value());
  EXPECT_EQ(single_snapshot->trace_id, 9001u);

  unblock_promise.set_value();
  ASSERT_EQ(done_future.wait_for(2s), std::future_status::ready);
  EXPECT_TRUE(executor.DrainAndJoin(2s));
  EXPECT_FALSE(executor.SnapshotCoroutine(it->coroutine_id).has_value());

  guard.reset();
  io_context.stop();
  io_thread.join();
}

TEST_F(CoroutineExecutorTest, DetectHungCoroutinesReportsOnlyOncePerSuspensionWindow) {
  using namespace std::chrono_literals;

  asio::io_context io_context;
  CoroutineExecutor executor(io_context, 1);
  auto guard = asio::make_work_guard(io_context);

  std::promise<void> entered_promise;
  auto entered_future = entered_promise.get_future();
  std::promise<void> unblock_promise;
  auto unblock_signal = unblock_promise.get_future().share();
  std::promise<void> done_promise;
  auto done_future = done_promise.get_future();

  std::thread io_thread([&]() { io_context.run(); });

  CoroutineMetadata metadata;
  metadata.name = "handler.hung_test";
  metadata.source = "unit_test";
  metadata.handler_key = "hung_test";

  ASSERT_EQ(executor.TrySpawn(BlockedMetadataTask(
                &executor, &entered_promise, unblock_signal, &done_promise),
            std::move(metadata)),
            SpawnResult::kSpawned);
  ASSERT_EQ(entered_future.wait_for(2s), std::future_status::ready);
  ASSERT_TRUE(WaitUntil([&executor]() { return executor.SuspendedCount() > 0; }, 2s));

  std::this_thread::sleep_for(60ms);
  const auto first_report = executor.DetectHungCoroutines(20ms);
  ASSERT_FALSE(first_report.empty());
  EXPECT_EQ(first_report.front().name, "handler.hung_test");
  EXPECT_EQ(first_report.front().status, CoroutineStatus::kSuspendedAsync);

  const auto second_report = executor.DetectHungCoroutines(20ms);
  EXPECT_TRUE(second_report.empty());

  unblock_promise.set_value();
  ASSERT_EQ(done_future.wait_for(2s), std::future_status::ready);
  EXPECT_TRUE(executor.DrainAndJoin(2s));

  guard.reset();
  io_context.stop();
  io_thread.join();
}

TEST_F(CoroutineExecutorTest, DetectStarvingCoroutinesReportsLongRunningWithoutSuspension) {
  using namespace std::chrono_literals;

  asio::io_context io_context;
  CoroutineExecutor executor(io_context, 1);
  auto guard = asio::make_work_guard(io_context);

  std::promise<void> entered_promise;
  auto entered_future = entered_promise.get_future();
  std::promise<void> unblock_promise;
  auto unblock_signal = unblock_promise.get_future().share();
  std::promise<void> done_promise;
  auto done_future = done_promise.get_future();

  std::thread io_thread([&]() { io_context.run(); });

  CoroutineMetadata metadata;
  metadata.name = "handler.starvation_test";
  metadata.source = "unit_test";
  metadata.handler_key = "starvation_test";

  ASSERT_EQ(executor.TrySpawn(
                NonSuspendingBlockedTask(&entered_promise, unblock_signal, &done_promise),
                std::move(metadata)),
            SpawnResult::kSpawned);
  ASSERT_EQ(entered_future.wait_for(2s), std::future_status::ready);

  std::this_thread::sleep_for(60ms);
  const auto first_report = executor.DetectStarvingCoroutines(20ms);
  ASSERT_FALSE(first_report.empty());
  EXPECT_EQ(first_report.front().name, "handler.starvation_test");
  EXPECT_EQ(first_report.front().status, CoroutineStatus::kRunning);
  EXPECT_GE(first_report.front().since_last_state_ms, 20u);

  const auto second_report = executor.DetectStarvingCoroutines(20ms);
  EXPECT_TRUE(second_report.empty());

  unblock_promise.set_value();
  ASSERT_EQ(done_future.wait_for(2s), std::future_status::ready);
  EXPECT_TRUE(executor.DrainAndJoin(2s));

  guard.reset();
  io_context.stop();
  io_thread.join();
}

TEST_F(CoroutineExecutorTest, StarvationWatchdogAutoDetectsAndIncrementsMetric) {
  using namespace std::chrono_literals;

  asio::io_context io_context;
  auto metrics_sink = std::make_shared<TestMetricsSink>();
  CoroutineExecutor executor(io_context, 1, 0, metrics_sink);
  executor.ConfigureStarvationWatchdog(20ms, 5ms);
  auto guard = asio::make_work_guard(io_context);

  std::promise<void> entered_promise;
  auto entered_future = entered_promise.get_future();
  std::promise<void> unblock_promise;
  auto unblock_signal = unblock_promise.get_future().share();
  std::promise<void> done_promise;
  auto done_future = done_promise.get_future();

  std::thread io_thread([&]() { io_context.run(); });

  CoroutineMetadata metadata;
  metadata.name = "handler.starvation_watchdog_test";
  metadata.source = "unit_test";
  metadata.handler_key = "starvation_watchdog_test";

  ASSERT_EQ(executor.TrySpawn(
                NonSuspendingBlockedTask(&entered_promise, unblock_signal, &done_promise),
                std::move(metadata)),
            SpawnResult::kSpawned);
  ASSERT_EQ(entered_future.wait_for(2s), std::future_status::ready);

  const bool detected = WaitUntil(
      [metrics_sink]() {
        return metrics_sink->CounterValue("logic.coroutine.starvation_detected_total") > 0;
      },
      2s);

  unblock_promise.set_value();
  ASSERT_EQ(done_future.wait_for(2s), std::future_status::ready);
  EXPECT_TRUE(executor.DrainAndJoin(2s));

  guard.reset();
  io_context.stop();
  io_thread.join();

  EXPECT_TRUE(detected);
}

TEST_F(CoroutineExecutorTest, TraceExportWritesCoroutineEventsToJson) {
  using namespace std::chrono_literals;

  const auto unique_id = std::to_string(
      std::chrono::steady_clock::now().time_since_epoch().count());
  const std::filesystem::path trace_path =
      std::filesystem::path("/tmp") / ("legend2_coroutine_trace_" + unique_id + ".json");

  asio::io_context io_context;
  CoroutineExecutor executor(io_context, 1);
  auto guard = asio::make_work_guard(io_context);

  std::promise<void> done_promise;
  auto done_future = done_promise.get_future();

  std::thread io_thread([&]() { io_context.run(); });

  ASSERT_TRUE(executor.ConfigureTraceExport(trace_path.string()));
  EXPECT_TRUE(executor.IsTraceExportEnabled());

  CoroutineMetadata metadata;
  metadata.name = "handler.trace_export_test";
  metadata.source = "unit_test";
  metadata.handler_key = "trace_export_test";

  ASSERT_TRUE(executor.Spawn(
      SleepThenSignalTask(&executor, std::chrono::milliseconds(20), &done_promise),
      std::move(metadata)));
  ASSERT_EQ(done_future.wait_for(2s), std::future_status::ready);

  EXPECT_TRUE(executor.DrainAndJoin(2s));
  ASSERT_TRUE(executor.ConfigureTraceExport(""));
  EXPECT_FALSE(executor.IsTraceExportEnabled());

  guard.reset();
  io_context.stop();
  io_thread.join();

  std::ifstream input(trace_path);
  ASSERT_TRUE(input.is_open());
  const std::string trace_text((std::istreambuf_iterator<char>(input)),
                               std::istreambuf_iterator<char>());
  EXPECT_NE(trace_text.find("\"traceEvents\""), std::string::npos);
  EXPECT_NE(trace_text.find("\"cat\":\"coroutine\""), std::string::npos);
  EXPECT_NE(trace_text.find("handler.trace_export_test"), std::string::npos);

  std::error_code remove_ec;
  std::filesystem::remove(trace_path, remove_ec);
}

}  // namespace mir2::logic
