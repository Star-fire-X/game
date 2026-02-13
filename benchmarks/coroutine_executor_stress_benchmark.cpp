#include <benchmark/benchmark.h>

#include <asio/executor_work_guard.hpp>
#include <asio/io_context.hpp>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <future>
#include <thread>

#include "logic/coroutine_executor.h"

namespace {

using mir2::logic::CancelledError;
using mir2::logic::CoroutineExecutor;
using mir2::logic::SpawnResult;
using mir2::logic::Task;
using mir2::logic::TimeoutError;

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

bool WaitExecutorIdle(CoroutineExecutor& executor,
                      std::chrono::steady_clock::duration timeout) {
  return WaitUntil(
      [&executor]() {
        return executor.RunningCount() == 0 &&
               executor.SuspendedCount() == 0 &&
               executor.TimeoutBackgroundInflightCount() == 0;
      },
      timeout);
}

Task<void> IncrementCounterTask(std::atomic<uint64_t>* completed) {
  completed->fetch_add(1, std::memory_order_relaxed);
  co_return;
}

Task<void> TimeoutStormTask(CoroutineExecutor* executor,
                            std::atomic<uint64_t>* timed_out,
                            std::atomic<uint64_t>* cancelled,
                            std::atomic<uint64_t>* completed) {
  using namespace std::chrono_literals;

  try {
    co_await executor->AsyncWithTimeout([]() {
      std::this_thread::sleep_for(3ms);
      return 1;
    }, 200us);
  } catch (const TimeoutError&) {
    timed_out->fetch_add(1, std::memory_order_relaxed);
  } catch (const CancelledError&) {
    cancelled->fetch_add(1, std::memory_order_relaxed);
  }

  completed->fetch_add(1, std::memory_order_relaxed);
  co_return;
}

Task<void> ShutdownBlockedTask(CoroutineExecutor* executor,
                               std::shared_future<void> unblock_signal,
                               std::atomic<uint64_t>* completed) {
  co_await executor->Async([unblock_signal]() mutable { unblock_signal.wait(); });
  completed->fetch_add(1, std::memory_order_relaxed);
  co_return;
}

class ExecutorRuntime {
 public:
  explicit ExecutorRuntime(size_t blocking_threads, int64_t max_running_tasks = 1000000)
      : work_guard_(asio::make_work_guard(io_context_)),
        executor_(io_context_, blocking_threads, max_running_tasks),
        io_thread_([this]() { io_context_.run(); }) {}

  ~ExecutorRuntime() {
    executor_.StopAccepting();
    (void)executor_.DrainAndJoin(std::chrono::seconds(20));
    work_guard_.reset();
    io_context_.stop();
    if (io_thread_.joinable()) {
      io_thread_.join();
    }
  }

  CoroutineExecutor& executor() { return executor_; }

 private:
  asio::io_context io_context_;
  asio::executor_work_guard<asio::io_context::executor_type> work_guard_;
  CoroutineExecutor executor_;
  std::thread io_thread_;
};

void BM_CoroutineStorm_SpawnAndComplete(benchmark::State& state) {
  const int64_t batch = state.range(0);
  ExecutorRuntime runtime(/*blocking_threads=*/4);

  uint64_t rejected_total = 0;
  uint64_t accepted_total = 0;
  for (auto _ : state) {
    std::atomic<uint64_t> completed{0};
    uint64_t accepted = 0;

    for (int64_t i = 0; i < batch; ++i) {
      const SpawnResult result = runtime.executor().TrySpawn(IncrementCounterTask(&completed));
      if (result == SpawnResult::kSpawned) {
        ++accepted;
      } else {
        ++rejected_total;
      }
    }

    if (!WaitUntil(
            [&completed, accepted]() {
              return completed.load(std::memory_order_relaxed) == accepted;
            },
            std::chrono::seconds(5))) {
      state.SkipWithError("coroutine storm completion wait timed out");
      break;
    }

    if (!WaitExecutorIdle(runtime.executor(), std::chrono::seconds(5))) {
      state.SkipWithError("coroutine storm executor did not drain");
      break;
    }
    accepted_total += accepted;
  }

  state.SetItemsProcessed(
      static_cast<int64_t>(state.iterations()) * static_cast<int64_t>(batch));
  state.counters["accepted_total"] = static_cast<double>(accepted_total);
  state.counters["rejected_total"] = static_cast<double>(rejected_total);
}

void BM_TimeoutStorm_AsyncWithTimeout(benchmark::State& state) {
  const int64_t batch = state.range(0);
  ExecutorRuntime runtime(/*blocking_threads=*/8);

  uint64_t timed_out_total = 0;
  uint64_t cancelled_total = 0;
  uint64_t accepted_total = 0;
  uint64_t rejected_total = 0;

  for (auto _ : state) {
    std::atomic<uint64_t> timed_out{0};
    std::atomic<uint64_t> cancelled{0};
    std::atomic<uint64_t> completed{0};
    uint64_t accepted = 0;

    for (int64_t i = 0; i < batch; ++i) {
      const SpawnResult result = runtime.executor().TrySpawn(
          TimeoutStormTask(&runtime.executor(), &timed_out, &cancelled, &completed));
      if (result == SpawnResult::kSpawned) {
        ++accepted;
      } else {
        ++rejected_total;
      }
    }

    if (!WaitUntil(
            [&completed, accepted]() {
              return completed.load(std::memory_order_relaxed) == accepted;
            },
            std::chrono::seconds(20))) {
      state.SkipWithError("timeout storm coroutine completion wait timed out");
      break;
    }

    if (!WaitExecutorIdle(runtime.executor(), std::chrono::seconds(20))) {
      state.SkipWithError("timeout storm executor did not drain");
      break;
    }

    accepted_total += accepted;
    timed_out_total += timed_out.load(std::memory_order_relaxed);
    cancelled_total += cancelled.load(std::memory_order_relaxed);
  }

  state.SetItemsProcessed(
      static_cast<int64_t>(state.iterations()) * static_cast<int64_t>(batch));
  state.counters["accepted_total"] = static_cast<double>(accepted_total);
  state.counters["rejected_total"] = static_cast<double>(rejected_total);
  state.counters["timed_out_total"] = static_cast<double>(timed_out_total);
  state.counters["cancelled_total"] = static_cast<double>(cancelled_total);
  state.counters["timeout_ratio"] =
      accepted_total == 0 ? 0.0 : static_cast<double>(timed_out_total) /
                                    static_cast<double>(accepted_total);
}

void BM_ShutdownStorm_DrainAndJoin(benchmark::State& state) {
  const int64_t inflight = state.range(0);

  uint64_t round_success = 0;
  for (auto _ : state) {
    state.PauseTiming();

    asio::io_context io_context;
    auto work_guard = asio::make_work_guard(io_context);
    CoroutineExecutor executor(io_context, /*blocking_threads=*/4, /*max_running_tasks=*/1000000);
    std::thread io_thread([&io_context]() { io_context.run(); });

    std::promise<void> unblock_promise;
    std::shared_future<void> unblock_signal = unblock_promise.get_future().share();
    std::atomic<uint64_t> completed{0};

    uint64_t accepted = 0;
    for (int64_t i = 0; i < inflight; ++i) {
      const SpawnResult result =
          executor.TrySpawn(ShutdownBlockedTask(&executor, unblock_signal, &completed));
      if (result == SpawnResult::kSpawned) {
        ++accepted;
      }
    }
    if (accepted == 0) {
      work_guard.reset();
      io_context.stop();
      if (io_thread.joinable()) {
        io_thread.join();
      }
      state.SkipWithError("shutdown storm accepted=0");
      break;
    }

    if (!WaitUntil(
            [&executor, accepted]() {
              return executor.SuspendedCount() == static_cast<int64_t>(accepted);
            },
            std::chrono::seconds(10))) {
      work_guard.reset();
      io_context.stop();
      if (io_thread.joinable()) {
        io_thread.join();
      }
      state.SkipWithError("shutdown storm pre-drain wait timed out");
      break;
    }

    state.ResumeTiming();

    executor.StopAccepting();
    unblock_promise.set_value();
    const bool drained = executor.DrainAndJoin(std::chrono::seconds(20));

    state.PauseTiming();
    work_guard.reset();
    io_context.stop();
    if (io_thread.joinable()) {
      io_thread.join();
    }

    if (!drained || completed.load(std::memory_order_relaxed) != accepted) {
      state.SkipWithError("shutdown storm drain failed");
      break;
    }

    ++round_success;
    state.ResumeTiming();
  }

  state.SetItemsProcessed(
      static_cast<int64_t>(state.iterations()) * static_cast<int64_t>(inflight));
  state.counters["round_success"] = static_cast<double>(round_success);
}

BENCHMARK(BM_CoroutineStorm_SpawnAndComplete)
    ->Arg(256)
    ->Arg(1024)
    ->Arg(4096)
    ->Unit(benchmark::kMillisecond);

BENCHMARK(BM_TimeoutStorm_AsyncWithTimeout)
    ->Arg(64)
    ->Arg(256)
    ->Arg(1024)
    ->Unit(benchmark::kMillisecond);

BENCHMARK(BM_ShutdownStorm_DrainAndJoin)
    ->Arg(64)
    ->Arg(256)
    ->Arg(1024)
    ->Unit(benchmark::kMillisecond);

}  // namespace

BENCHMARK_MAIN();
