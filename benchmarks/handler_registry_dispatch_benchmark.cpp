#include <benchmark/benchmark.h>

#include <asio/executor_work_guard.hpp>
#include <asio/io_context.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#include "logic/coroutine_executor.h"
#include "logic/handler_context.h"
#include "logic/handler_registry.h"
#include "monitor/metrics.h"

namespace {

using mir2::logic::CoroutineExecutor;
using mir2::logic::HandlerContext;
using mir2::logic::HandlerRegistry;
using mir2::logic::SpawnResult;
using mir2::logic::Task;

constexpr size_t kDispatchBatchSize = 256;

Task<void> NoopHandler(HandlerContext /*ctx*/,
                       const uint8_t* /*payload*/,
                       size_t /*payload_size*/) {
  co_return;
}

Task<void> RunLegacyMeasuredTask(std::shared_ptr<std::vector<uint8_t>> payload_copy,
                                 Task<void> task,
                                 std::chrono::steady_clock::time_point start) {
  auto keep_payload_alive = std::move(payload_copy);
  (void)keep_payload_alive;

  struct LatencyGuard {
    std::chrono::steady_clock::time_point start_time;
    ~LatencyGuard() noexcept {
      const auto elapsed_ms =
          std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() -
                                                    start_time)
              .count();
      mir2::monitor::Metrics::Instance().ObserveHandlerLatency(elapsed_ms);
    }
  } guard{start};

  co_await std::move(task);
}

class LegacyHandlerRegistryModel {
 public:
  using HandlerFunc = HandlerRegistry::HandlerFunc;

  explicit LegacyHandlerRegistryModel(CoroutineExecutor& executor)
      : executor_(executor) {}

  void RegisterHandler(uint16_t msg_id, HandlerFunc handler) {
    handlers_[msg_id] = std::move(handler);
  }

  bool CreateTask(const HandlerContext& context,
                  uint16_t msg_id,
                  const uint8_t* payload,
                  size_t payload_size,
                  Task<void>* out_task) const {
    if (!out_task) {
      return false;
    }

    auto it = handlers_.find(msg_id);
    if (it == handlers_.end() || !it->second) {
      return false;
    }

    auto payload_copy = std::make_shared<std::vector<uint8_t>>();
    if (payload && payload_size > 0) {
      payload_copy->assign(payload, payload + payload_size);
    }

    const auto start = std::chrono::steady_clock::now();
    HandlerContext dispatch_context = context;
    dispatch_context.msg_id = msg_id;
    auto task = it->second(dispatch_context, payload_copy->data(), payload_copy->size());
    *out_task = RunLegacyMeasuredTask(std::move(payload_copy), std::move(task), start);
    return true;
  }

  bool DispatchMessage(const HandlerContext& context,
                       uint16_t msg_id,
                       const uint8_t* payload,
                       size_t payload_size) const {
    Task<void> task(Task<void>::Handle{});
    if (!CreateTask(context, msg_id, payload, payload_size, &task)) {
      return false;
    }
    return executor_.Spawn(std::move(task));
  }

 private:
  CoroutineExecutor& executor_;
  std::unordered_map<uint16_t, HandlerFunc> handlers_;
};

struct ExecutorRuntime {
  asio::io_context io_context;
  asio::executor_work_guard<asio::io_context::executor_type> work_guard =
      asio::make_work_guard(io_context);
  CoroutineExecutor executor{io_context, 1, 1000000};
  std::thread io_thread;

  ExecutorRuntime()
      : io_thread([this]() { io_context.run(); }) {}

  ~ExecutorRuntime() {
    executor.StopAccepting();
    (void)executor.DrainAndJoin(std::chrono::seconds(5));
    work_guard.reset();
    io_context.stop();
    if (io_thread.joinable()) {
      io_thread.join();
    }
  }
};

void WaitExecutorIdle(CoroutineExecutor& executor) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  while (executor.RunningCount() > 0 ||
         executor.SuspendedCount() > 0 ||
         executor.TimeoutBackgroundInflightCount() > 0) {
    if (std::chrono::steady_clock::now() >= deadline) {
      break;
    }
    std::this_thread::sleep_for(std::chrono::microseconds(50));
  }
}

void BM_HandlerRegistry_CreateTaskHit_Legacy(benchmark::State& state) {
  const size_t payload_size = static_cast<size_t>(state.range(0));
  std::vector<uint8_t> payload(payload_size, 0xAB);
  const uint8_t* raw_payload = payload.empty() ? nullptr : payload.data();

  asio::io_context io_context;
  CoroutineExecutor executor(io_context, 1, 1000000);
  LegacyHandlerRegistryModel registry(executor);
  constexpr uint16_t kMsgId = 4040;
  registry.RegisterHandler(kMsgId, &NoopHandler);

  HandlerContext context;
  context.client_id = 10001;
  context.msg_id = kMsgId;

  for (auto _ : state) {
    Task<void> task(Task<void>::Handle{});
    const bool ok =
        registry.CreateTask(context, kMsgId, raw_payload, payload_size, &task);
    benchmark::DoNotOptimize(static_cast<int>(ok));
    benchmark::DoNotOptimize(static_cast<int>(task.IsValid()));
  }

  state.SetItemsProcessed(static_cast<int64_t>(state.iterations()));
}

void BM_HandlerRegistry_CreateTaskHit_Current(benchmark::State& state) {
  const size_t payload_size = static_cast<size_t>(state.range(0));
  std::vector<uint8_t> payload(payload_size, 0xAB);
  const uint8_t* raw_payload = payload.empty() ? nullptr : payload.data();

  asio::io_context io_context;
  CoroutineExecutor executor(io_context, 1, 1000000);
  HandlerRegistry registry(executor);
  constexpr uint16_t kMsgId = 4040;
  registry.RegisterHandler(kMsgId, &NoopHandler);

  HandlerContext context;
  context.client_id = 10001;
  context.msg_id = kMsgId;

  for (auto _ : state) {
    Task<void> task(Task<void>::Handle{});
    const bool ok =
        registry.CreateTask(context, kMsgId, raw_payload, payload_size, &task);
    benchmark::DoNotOptimize(static_cast<int>(ok));
    benchmark::DoNotOptimize(static_cast<int>(task.IsValid()));
  }

  state.SetItemsProcessed(static_cast<int64_t>(state.iterations()));
}

void BM_HandlerRegistry_CreateTaskMiss_Legacy(benchmark::State& state) {
  const size_t payload_size = static_cast<size_t>(state.range(0));
  std::vector<uint8_t> payload(payload_size, 0xCD);
  const uint8_t* raw_payload = payload.empty() ? nullptr : payload.data();

  asio::io_context io_context;
  CoroutineExecutor executor(io_context, 1, 1000000);
  LegacyHandlerRegistryModel registry(executor);
  constexpr uint16_t kExistingMsgId = 4040;
  constexpr uint16_t kMissingMsgId = 65530;
  registry.RegisterHandler(kExistingMsgId, &NoopHandler);

  HandlerContext context;
  context.client_id = 20001;
  context.msg_id = kMissingMsgId;

  for (auto _ : state) {
    Task<void> task(Task<void>::Handle{});
    const bool ok =
        registry.CreateTask(context, kMissingMsgId, raw_payload, payload_size, &task);
    benchmark::DoNotOptimize(static_cast<int>(ok));
    benchmark::DoNotOptimize(static_cast<int>(task.IsValid()));
  }

  state.SetItemsProcessed(static_cast<int64_t>(state.iterations()));
}

void BM_HandlerRegistry_CreateTaskMiss_Current(benchmark::State& state) {
  const size_t payload_size = static_cast<size_t>(state.range(0));
  std::vector<uint8_t> payload(payload_size, 0xCD);
  const uint8_t* raw_payload = payload.empty() ? nullptr : payload.data();

  asio::io_context io_context;
  CoroutineExecutor executor(io_context, 1, 1000000);
  HandlerRegistry registry(executor);
  constexpr uint16_t kExistingMsgId = 4040;
  constexpr uint16_t kMissingMsgId = 65530;
  registry.RegisterHandler(kExistingMsgId, &NoopHandler);

  HandlerContext context;
  context.client_id = 20001;
  context.msg_id = kMissingMsgId;

  for (auto _ : state) {
    Task<void> task(Task<void>::Handle{});
    const bool ok =
        registry.CreateTask(context, kMissingMsgId, raw_payload, payload_size, &task);
    benchmark::DoNotOptimize(static_cast<int>(ok));
    benchmark::DoNotOptimize(static_cast<int>(task.IsValid()));
  }

  state.SetItemsProcessed(static_cast<int64_t>(state.iterations()));
}

void BM_HandlerRegistry_DispatchHit_Legacy(benchmark::State& state) {
  const size_t payload_size = static_cast<size_t>(state.range(0));
  std::vector<uint8_t> payload(payload_size, 0xEE);
  const uint8_t* raw_payload = payload.empty() ? nullptr : payload.data();

  ExecutorRuntime runtime;
  LegacyHandlerRegistryModel registry(runtime.executor);
  constexpr uint16_t kMsgId = 4040;
  registry.RegisterHandler(kMsgId, &NoopHandler);

  HandlerContext context;
  context.client_id = 30001;
  context.msg_id = kMsgId;

  uint64_t rejected = 0;
  for (auto _ : state) {
    for (size_t i = 0; i < kDispatchBatchSize; ++i) {
      const bool ok =
          registry.DispatchMessage(context, kMsgId, raw_payload, payload_size);
      if (!ok) {
        ++rejected;
      }
      benchmark::DoNotOptimize(static_cast<int>(ok));
    }
    state.PauseTiming();
    WaitExecutorIdle(runtime.executor);
    state.ResumeTiming();
  }

  state.PauseTiming();
  WaitExecutorIdle(runtime.executor);
  state.ResumeTiming();
  state.SetItemsProcessed(
      static_cast<int64_t>(state.iterations() * kDispatchBatchSize));
  state.counters["batch_size"] = static_cast<double>(kDispatchBatchSize);
  state.counters["dispatch_rejected"] = static_cast<double>(rejected);
}

void BM_HandlerRegistry_DispatchHit_Current(benchmark::State& state) {
  const size_t payload_size = static_cast<size_t>(state.range(0));
  std::vector<uint8_t> payload(payload_size, 0xEE);
  const uint8_t* raw_payload = payload.empty() ? nullptr : payload.data();

  ExecutorRuntime runtime;
  HandlerRegistry registry(runtime.executor);
  constexpr uint16_t kMsgId = 4040;
  registry.RegisterHandler(kMsgId, &NoopHandler);

  HandlerContext context;
  context.client_id = 30001;
  context.msg_id = kMsgId;

  uint64_t rejected = 0;
  for (auto _ : state) {
    for (size_t i = 0; i < kDispatchBatchSize; ++i) {
      const SpawnResult result =
          registry.DispatchMessage(context, kMsgId, raw_payload, payload_size);
      if (result != SpawnResult::kSpawned) {
        ++rejected;
      }
      benchmark::DoNotOptimize(static_cast<int>(result));
    }
    state.PauseTiming();
    WaitExecutorIdle(runtime.executor);
    state.ResumeTiming();
  }

  state.PauseTiming();
  WaitExecutorIdle(runtime.executor);
  state.ResumeTiming();
  state.SetItemsProcessed(
      static_cast<int64_t>(state.iterations() * kDispatchBatchSize));
  state.counters["batch_size"] = static_cast<double>(kDispatchBatchSize);
  state.counters["dispatch_rejected"] = static_cast<double>(rejected);
}

}  // namespace

BENCHMARK(BM_HandlerRegistry_CreateTaskHit_Legacy)
    ->Arg(32)
    ->Arg(128)
    ->Arg(512)
    ->Unit(benchmark::kNanosecond);

BENCHMARK(BM_HandlerRegistry_CreateTaskHit_Current)
    ->Arg(32)
    ->Arg(128)
    ->Arg(512)
    ->Unit(benchmark::kNanosecond);

BENCHMARK(BM_HandlerRegistry_CreateTaskMiss_Legacy)
    ->Arg(32)
    ->Arg(128)
    ->Arg(512)
    ->Unit(benchmark::kNanosecond);

BENCHMARK(BM_HandlerRegistry_CreateTaskMiss_Current)
    ->Arg(32)
    ->Arg(128)
    ->Arg(512)
    ->Unit(benchmark::kNanosecond);

BENCHMARK(BM_HandlerRegistry_DispatchHit_Legacy)
    ->Arg(32)
    ->Arg(128)
    ->Arg(512)
    ->Unit(benchmark::kNanosecond);

BENCHMARK(BM_HandlerRegistry_DispatchHit_Current)
    ->Arg(32)
    ->Arg(128)
    ->Arg(512)
    ->Unit(benchmark::kNanosecond);

BENCHMARK_MAIN();
