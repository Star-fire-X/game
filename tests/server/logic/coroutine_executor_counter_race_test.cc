/**
 * @file coroutine_executor_counter_race_test.cc
 * @brief Concurrency regression tests for CoroutineExecutor counters.
 */

#include <gtest/gtest.h>

#include <asio/io_context.hpp>

#include <barrier>
#include <cstdint>
#include <thread>
#include <vector>

#define private public
#include "logic/coroutine_executor.h"
#undef private

namespace mir2::logic {
namespace {

template <typename IncFn, typename DecFn>
int64_t RunCounterRace(IncFn inc, DecFn dec) {
  constexpr int kIncThreads = 6;
  constexpr int kDecThreads = 6;
  constexpr int kIncPerThread = 60000;
  constexpr int kDecPerThread = 30000;

  std::barrier sync(kIncThreads + kDecThreads);
  std::vector<std::thread> workers;
  workers.reserve(kIncThreads + kDecThreads);

  for (int i = 0; i < kIncThreads; ++i) {
    workers.emplace_back([&sync, &inc]() {
      sync.arrive_and_wait();
      for (int j = 0; j < kIncPerThread; ++j) {
        inc();
      }
    });
  }

  for (int i = 0; i < kDecThreads; ++i) {
    workers.emplace_back([&sync, &dec]() {
      sync.arrive_and_wait();
      for (int j = 0; j < kDecPerThread; ++j) {
        dec();
      }
    });
  }

  for (auto& worker : workers) {
    worker.join();
  }

  return static_cast<int64_t>(kIncThreads) * kIncPerThread -
         static_cast<int64_t>(kDecThreads) * kDecPerThread;
}

}  // namespace

TEST(CoroutineExecutorCounterRaceTest, RunningCounterStaysAboveTheoreticalMinimum) {
  asio::io_context io_context;
  CoroutineExecutor executor(io_context, 1);

  const int64_t min_expected = RunCounterRace(
      [&executor]() { executor.IncrementRunning(); },
      [&executor]() { executor.DecrementRunning(); });

  const int64_t observed = executor.RunningCount();
  EXPECT_GE(observed, min_expected);
  EXPECT_GE(observed, 0);

  executor.running_count_.store(0, std::memory_order_release);
}

TEST(CoroutineExecutorCounterRaceTest, SuspendedCounterStaysAboveTheoreticalMinimum) {
  asio::io_context io_context;
  CoroutineExecutor executor(io_context, 1);

  const int64_t min_expected = RunCounterRace(
      [&executor]() { executor.IncrementSuspended(); },
      [&executor]() { executor.DecrementSuspended(); });

  const int64_t observed = executor.SuspendedCount();
  EXPECT_GE(observed, min_expected);
  EXPECT_GE(observed, 0);

  executor.suspended_count_.store(0, std::memory_order_release);
}

}  // namespace mir2::logic
