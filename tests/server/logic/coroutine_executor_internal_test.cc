#include <gtest/gtest.h>

#include <asio/executor_work_guard.hpp>
#include <asio/io_context.hpp>

#include <chrono>
#include <future>
#include <mutex>
#include <thread>
#include <unordered_map>

#define private public
#include "logic/coroutine_executor.h"
#undef private

namespace mir2::logic {
namespace {

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

Task<void> SleepThenSignalTask(CoroutineExecutor* executor,
                               std::chrono::milliseconds duration,
                               std::promise<void>* done_promise) {
  co_await executor->SleepFor(duration);
  done_promise->set_value();
  co_return;
}

TEST(CoroutineExecutorInternalTest, UnderflowCountersIncrementAndClampToZero) {
  asio::io_context io_context;
  auto metrics_sink = std::make_shared<TestMetricsSink>();
  CoroutineExecutor executor(io_context, 1, 1, metrics_sink);

  executor.DecrementRunning();
  executor.DecrementSuspended();
  executor.DecrementPendingResumeCallbacks();
  executor.DecrementTimeoutBackgroundInflight();

  EXPECT_EQ(metrics_sink->CounterValue("logic.coroutine.running_underflow_total"), 1u);
  EXPECT_EQ(metrics_sink->CounterValue("logic.coroutine.suspended_underflow_total"), 1u);
  EXPECT_EQ(metrics_sink->CounterValue("logic.coroutine.pending_resume_underflow_total"), 1u);
  EXPECT_EQ(metrics_sink->CounterValue(
                "logic.coroutine.timeout_background_inflight_underflow_total"),
            1u);

  EXPECT_DOUBLE_EQ(metrics_sink->GaugeValue("logic.coroutine.running_count"), 0.0);
  EXPECT_DOUBLE_EQ(metrics_sink->GaugeValue("logic.coroutine.suspended_count"), 0.0);
  EXPECT_DOUBLE_EQ(metrics_sink->GaugeValue("logic.coroutine.pending_resume_callbacks"), 0.0);
  EXPECT_DOUBLE_EQ(metrics_sink->GaugeValue("logic.coroutine.timeout_background_inflight"), 0.0);
}

TEST(CoroutineExecutorInternalTest, DrainAndJoinTimeoutIncrementsMetric) {
  using namespace std::chrono_literals;

  asio::io_context io_context;
  auto metrics_sink = std::make_shared<TestMetricsSink>();
  CoroutineExecutor executor(io_context, 1, 0, metrics_sink);
  auto guard = asio::make_work_guard(io_context);

  std::promise<void> done_promise;
  auto done_future = done_promise.get_future();

  std::thread io_thread([&]() { io_context.run(); });

  ASSERT_TRUE(executor.Spawn(SleepThenSignalTask(&executor, 250ms, &done_promise)));
  EXPECT_FALSE(executor.DrainAndJoin(10ms));
  EXPECT_EQ(metrics_sink->CounterValue("logic.coroutine.drain_timeout_total"), 1u);

  ASSERT_EQ(done_future.wait_for(2s), std::future_status::ready);
  EXPECT_TRUE(executor.DrainAndJoin(2s));
  EXPECT_EQ(executor.RunningCount(), 0);
  EXPECT_EQ(executor.SuspendedCount(), 0);

  guard.reset();
  io_context.stop();
  io_thread.join();
}

}  // namespace
}  // namespace mir2::logic
