/**
 * @file coroutine_executor.h
 * @brief Coroutine executor for async logic tasks.
 */

#ifndef MIR2_LOGIC_COROUTINE_EXECUTOR_H_
#define MIR2_LOGIC_COROUTINE_EXECUTOR_H_

#include <asio/error_code.hpp>
#include <asio/io_context.hpp>
#include <asio/post.hpp>
#include <asio/steady_timer.hpp>
#include <asio/thread_pool.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <condition_variable>
#include <coroutine>
#include <cstdint>
#include <exception>
#include <functional>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <stop_token>
#include <stdexcept>
#include <string>
#include <thread>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

#include "logic/task.h"
#include "logic/thread_affinity.h"
#include "monitor/metrics_sink.h"

namespace mir2::logic {

namespace detail {

template <typename Result>
struct AsyncResultStorage {
  std::optional<Result> value;

  void Set(Result result) { value = std::move(result); }

  Result Take() { return std::move(value.value()); }
};

template <>
struct AsyncResultStorage<void> {
  void Set() {}
  void Take() {}
};

}  // namespace detail

class TimeoutError : public std::runtime_error {
 public:
  explicit TimeoutError(const std::string& message) : std::runtime_error(message) {}
};

class CancelledError : public std::runtime_error {
 public:
  explicit CancelledError(const std::string& message) : std::runtime_error(message) {}
};

enum class SpawnResult : uint8_t {
  kSpawned = 0,
  kInvalidTask = 1,
  kNotAccepting = 2,
  kOverLimit = 3,
};

const char* ToString(SpawnResult result) noexcept;

enum class CoroutineStatus : uint8_t {
  kQueued = 0,
  kRunning = 1,
  kSuspendedAsync = 2,
  kSuspendedSleep = 3,
};

const char* ToString(CoroutineStatus status) noexcept;

struct CoroutineMetadata {
  uint64_t trace_id = 0;
  std::string name;
  std::string source;
  std::string handler_key;
  uint16_t msg_id = 0;
  uint64_t client_id = 0;
};

struct CoroutineSnapshot {
  uint64_t coroutine_id = 0;
  uint64_t trace_id = 0;
  std::string name;
  std::string source;
  std::string handler_key;
  uint16_t msg_id = 0;
  uint64_t client_id = 0;
  CoroutineStatus status = CoroutineStatus::kQueued;
  std::string last_state_reason;
  uint64_t age_ms = 0;
  uint64_t since_last_state_ms = 0;
  uint64_t suspended_for_ms = 0;
  uint64_t resume_count = 0;
  uint64_t suspend_count = 0;
};

class CoroutineExecutor {
 public:
  using ParallelSpawnRejectedCallback = std::function<void(size_t, SpawnResult)>;
  static constexpr size_t kWhenAnyNoTaskIndex = std::numeric_limits<size_t>::max();

  struct WhenAllOptions {
    bool fail_fast = false;
    std::stop_token stop_token{};
  };

  struct WhenAnyOptions {
    bool cancel_losers = false;
  };

  explicit CoroutineExecutor(asio::io_context& io_context,
                             size_t blocking_threads = std::max(1u, std::thread::hardware_concurrency()),
                             int64_t max_running_tasks = 0,
                             std::shared_ptr<monitor::IMetricsSink> metrics_sink =
                                 monitor::CreateDefaultMetricsSink());
  ~CoroutineExecutor();

  CoroutineExecutor(const CoroutineExecutor&) = delete;
  CoroutineExecutor& operator=(const CoroutineExecutor&) = delete;

  asio::io_context& GetIoContext() const noexcept { return io_context_; }

  template <typename T>
  SpawnResult TrySpawn(Task<T> task) {
    return TrySpawn(std::move(task), {});
  }

  template <typename T>
  SpawnResult TrySpawn(Task<T> task, CoroutineMetadata metadata) {
    if (!task.IsValid()) {
      RecordSpawnRejected(SpawnResult::kInvalidTask);
      return SpawnResult::kInvalidTask;
    }

    const SpawnResult begin_result = BeginSpawn();
    if (begin_result != SpawnResult::kSpawned) {
      return begin_result;
    }

    auto runtime = RegisterCoroutine(std::move(metadata));
    auto wrapped = SpawnWrapped(this, lifetime_state_, runtime, std::move(task));

    auto handle = wrapped.Release();
    if (!handle) {
      CompleteCoroutine(runtime);
      DecrementRunning();
      RecordSpawnRejected(SpawnResult::kInvalidTask);
      return SpawnResult::kInvalidTask;
    }

    asio::post(io_context_, [this, handle, runtime]() mutable {
      ResumeHandle(handle, runtime, "spawn");
    });
    return SpawnResult::kSpawned;
  }

  template <typename T>
  bool Spawn(Task<T> task) {
    return Spawn(std::move(task), {});
  }

  template <typename T>
  bool Spawn(Task<T> task, CoroutineMetadata metadata) {
    return TrySpawn(std::move(task), std::move(metadata)) == SpawnResult::kSpawned;
  }

  template <typename T, typename ErrorCallback>
  SpawnResult SpawnOrDrop(Task<T> task,
                          ErrorCallback&& on_error,
                          CoroutineMetadata metadata = {}) {
    const SpawnResult result = TrySpawn(std::move(task), std::move(metadata));
    if (result == SpawnResult::kSpawned) {
      return result;
    }

    if constexpr (std::is_invocable_v<ErrorCallback, SpawnResult>) {
      std::invoke(std::forward<ErrorCallback>(on_error), result);
    } else if constexpr (std::is_invocable_v<ErrorCallback>) {
      std::invoke(std::forward<ErrorCallback>(on_error));
    } else {
      static_assert(std::is_invocable_v<ErrorCallback, SpawnResult> ||
                        std::is_invocable_v<ErrorCallback>,
                    "SpawnOrDrop callback must accept SpawnResult or no arguments");
    }
    return result;
  }

  void StopAccepting() noexcept;
  bool IsAccepting() const noexcept {
    return accepting_tasks_.load(std::memory_order_acquire);
  }
  bool DrainAndJoin(std::chrono::steady_clock::duration timeout);

  Task<void> SleepFor(std::chrono::steady_clock::duration duration,
                      std::stop_token stop_token = {});

  // Empty input is treated as already completed.
  Task<void> WhenAll(std::vector<Task<void>> tasks,
                     ParallelSpawnRejectedCallback on_spawn_rejected = {});

  Task<void> WhenAll(std::vector<Task<void>> tasks,
                     ParallelSpawnRejectedCallback on_spawn_rejected,
                     WhenAllOptions options);

  // Empty input returns kWhenAnyNoTaskIndex.
  Task<size_t> WhenAny(std::vector<Task<void>> tasks,
                       ParallelSpawnRejectedCallback on_spawn_rejected = {});

  Task<size_t> WhenAny(std::vector<Task<void>> tasks,
                       ParallelSpawnRejectedCallback on_spawn_rejected,
                       WhenAnyOptions options);

  template <typename Fn>
  auto Async(Fn&& fn) -> Task<std::invoke_result_t<Fn>> {
    return Async(std::forward<Fn>(fn), std::stop_token{});
  }

  template <typename Fn>
  auto Async(Fn&& fn, std::stop_token stop_token) -> Task<std::invoke_result_t<Fn>> {
    using FnType = std::decay_t<Fn>;
    using Result = std::invoke_result_t<FnType>;
    static_assert(!std::is_reference_v<Result>, "Async cannot return references");
    static_assert(!detail::Awaitable<Result>,
                  "Async callable must return a plain value, not an awaitable");
    return RunAsync<FnType, Result>(
        this, FnType(std::forward<Fn>(fn)),
        std::chrono::steady_clock::duration::zero(), false, stop_token);
  }

  template <typename Fn>
  auto AsyncWithTimeout(Fn&& fn, std::chrono::steady_clock::duration timeout)
      -> Task<std::invoke_result_t<Fn>> {
    return AsyncWithTimeout(std::forward<Fn>(fn), timeout, std::stop_token{});
  }

  template <typename Fn>
  auto AsyncWithTimeout(Fn&& fn,
                        std::chrono::steady_clock::duration timeout,
                        std::stop_token stop_token)
      -> Task<std::invoke_result_t<Fn>> {
    using FnType = std::decay_t<Fn>;
    using Result = std::invoke_result_t<FnType>;
    static_assert(!std::is_reference_v<Result>, "AsyncWithTimeout cannot return references");
    static_assert(!detail::Awaitable<Result>,
                  "AsyncWithTimeout callable must return a plain value, not an awaitable");
    return RunAsync<FnType, Result>(
        this, FnType(std::forward<Fn>(fn)), timeout, true, stop_token);
  }

  int64_t SuspendedCount() const noexcept {
    return suspended_count_.load(std::memory_order_acquire);
  }
  int64_t RunningCount() const noexcept {
    return running_count_.load(std::memory_order_acquire);
  }
  int64_t MaxRunningTasks() const noexcept { return max_running_tasks_; }
  int64_t TimeoutBackgroundInflightCount() const noexcept {
    return timeout_background_inflight_.load(std::memory_order_acquire);
  }
  uint64_t AllocateTraceId() noexcept;
  size_t ActiveCoroutineCount() const noexcept;
  std::vector<CoroutineSnapshot> SnapshotActiveCoroutines(
      size_t max_entries = 0) const;
  std::optional<CoroutineSnapshot> SnapshotCoroutine(uint64_t coroutine_id) const;
  std::vector<CoroutineSnapshot> DetectHungCoroutines(
      std::chrono::steady_clock::duration threshold,
      size_t max_entries = 64);
  std::vector<CoroutineSnapshot> DetectStarvingCoroutines(
      std::chrono::steady_clock::duration threshold,
      size_t max_entries = 64);
  void ConfigureStarvationWatchdog(
      std::chrono::steady_clock::duration threshold,
      std::chrono::steady_clock::duration poll_interval =
          std::chrono::steady_clock::duration::zero());
  bool ConfigureTraceExport(const std::string& trace_file_path);
  bool IsTraceExportEnabled() const noexcept;

 private:
  struct ExecutorLifetimeState {
    explicit ExecutorLifetimeState(asio::io_context& io_context)
        : io_context(io_context) {}

    bool IsStopRequested() const noexcept {
      return stop_requested.load(std::memory_order_acquire);
    }

    void RequestStop() noexcept {
      stop_requested.store(true, std::memory_order_release);
    }

    void IncrementSuspended() const {
      if (increment_suspended && !IsStopRequested()) {
        increment_suspended();
      }
    }

    void DecrementSuspended() const {
      if (decrement_suspended && !IsStopRequested()) {
        decrement_suspended();
      }
    }

    void DecrementRunning() const {
      if (decrement_running && !IsStopRequested()) {
        decrement_running();
      }
    }

    void IncrementPendingResumeCallbacks() const {
      if (increment_pending_resume_callbacks && !IsStopRequested()) {
        increment_pending_resume_callbacks();
      }
    }

    void DecrementPendingResumeCallbacks() const {
      if (decrement_pending_resume_callbacks && !IsStopRequested()) {
        decrement_pending_resume_callbacks();
      }
    }

    void IncrementTimeoutBackgroundInflight() const {
      if (increment_timeout_background_inflight && !IsStopRequested()) {
        increment_timeout_background_inflight();
      }
    }

    void DecrementTimeoutBackgroundInflight() const {
      if (decrement_timeout_background_inflight && !IsStopRequested()) {
        decrement_timeout_background_inflight();
      }
    }

    void IncrementTimeoutBackgroundTotal() const {
      if (increment_timeout_background_total && !IsStopRequested()) {
        increment_timeout_background_total();
      }
    }

    asio::io_context& io_context;
    std::atomic<bool> stop_requested{false};
    std::function<void()> increment_suspended;
    std::function<void()> decrement_suspended;
    std::function<void()> decrement_running;
    std::function<void()> increment_pending_resume_callbacks;
    std::function<void()> decrement_pending_resume_callbacks;
    std::function<void()> increment_timeout_background_inflight;
    std::function<void()> decrement_timeout_background_inflight;
    std::function<void()> increment_timeout_background_total;
  };

  struct CoroutineRuntimeState {
    std::chrono::steady_clock::time_point created_at;
    std::chrono::steady_clock::time_point last_state_time;
    std::chrono::steady_clock::time_point last_suspend_time;
    uint64_t coroutine_id = 0;
    uint64_t trace_id = 0;
    uint16_t msg_id = 0;
    uint64_t client_id = 0;
    std::string name;
    std::string source;
    std::string handler_key;
    std::string last_state_reason;
    CoroutineStatus status = CoroutineStatus::kQueued;
    uint64_t resume_count = 0;
    uint64_t suspend_count = 0;
    bool hung_reported = false;
    bool starvation_reported = false;
  };

  struct TraceWriter;
  struct ReusableTimerPool {
    explicit ReusableTimerPool(asio::io_context& io_context)
        : io_context(io_context) {}

    asio::io_context& io_context;
    std::atomic<bool> recycle_enabled{true};
    std::mutex mutex;
    std::vector<std::unique_ptr<asio::steady_timer>> free_timers;
    size_t max_cached = 4096;
  };

  static std::shared_ptr<CoroutineRuntimeState>& CurrentRuntimeSlot() {
    thread_local std::shared_ptr<CoroutineRuntimeState> runtime;
    return runtime;
  }

  template <typename T>
  static Task<void> SpawnWrapped(CoroutineExecutor* executor,
                                 std::shared_ptr<ExecutorLifetimeState> lifetime_state,
                                 std::shared_ptr<CoroutineRuntimeState> runtime,
                                 Task<T> task) {
    struct RunningGuard {
      CoroutineExecutor* executor = nullptr;
      std::shared_ptr<ExecutorLifetimeState> lifetime_state;
      std::shared_ptr<CoroutineRuntimeState> runtime;
      ~RunningGuard() {
        if (executor) {
          executor->CompleteCoroutine(runtime);
        }
        if (lifetime_state) {
          lifetime_state->DecrementRunning();
        }
      }
    } guard{executor, std::move(lifetime_state), std::move(runtime)};

    co_await std::move(task);
  }

  template <typename Fn, typename Result>
  static Task<Result> RunAsync(CoroutineExecutor* executor,
                               Fn fn,
                               std::chrono::steady_clock::duration timeout,
                               bool enable_timeout,
                               std::stop_token stop_token) {
    co_return co_await AsyncAwaiter<Fn, Result>(
        executor, std::move(fn), timeout, enable_timeout, stop_token);
  }

  enum class AsyncCompletion : uint8_t {
    kCompleted = 0,
    kTimedOut = 1,
    kCancelled = 2,
  };

  template <typename Result>
  struct AsyncState {
    AsyncState(CoroutineExecutor* executor,
               std::shared_ptr<ExecutorLifetimeState> lifetime_state,
               std::coroutine_handle<> continuation,
               std::shared_ptr<CoroutineRuntimeState> runtime,
               const char* suspend_reason)
        : executor(executor),
          lifetime_state(std::move(lifetime_state)),
          continuation(continuation),
          runtime(std::move(runtime)),
          suspend_reason(suspend_reason == nullptr ? "" : suspend_reason) {}

    void TryResume(AsyncCompletion completion) {
      if (completed.exchange(true, std::memory_order_acq_rel)) {
        return;
      }
      completion_reason = completion;
      if (completion != AsyncCompletion::kTimedOut && timer) {
        asio::error_code ignored;
        timer->cancel(ignored);
      }
      if (completion == AsyncCompletion::kTimedOut &&
          !worker_completed.load(std::memory_order_acquire) &&
          lifetime_state) {
        bool expected = false;
        if (timeout_background_tracked.compare_exchange_strong(
                expected, true, std::memory_order_acq_rel, std::memory_order_acquire)) {
          lifetime_state->IncrementTimeoutBackgroundInflight();
          lifetime_state->IncrementTimeoutBackgroundTotal();
        }
      }
      if (!lifetime_state || lifetime_state->IsStopRequested()) {
        return;
      }
      lifetime_state->DecrementSuspended();
      if (executor) {
        executor->ResumeHandle(continuation, runtime, ResumeReason(completion));
      } else {
        continuation.resume();
      }
    }

    void MarkWorkerCompleted() {
      worker_completed.store(true, std::memory_order_release);
      if (!lifetime_state) {
        return;
      }
      if (timeout_background_tracked.exchange(false,
                                              std::memory_order_acq_rel)) {
        lifetime_state->DecrementTimeoutBackgroundInflight();
      }
    }

    static const char* ResumeReason(AsyncCompletion completion) noexcept {
      switch (completion) {
        case AsyncCompletion::kCompleted:
          return "async_complete";
        case AsyncCompletion::kTimedOut:
          return "async_timeout";
        case AsyncCompletion::kCancelled:
          return "async_cancelled";
      }
      return "async_complete";
    }

    CoroutineExecutor* executor = nullptr;
    std::shared_ptr<ExecutorLifetimeState> lifetime_state;
    std::coroutine_handle<> continuation;
    std::shared_ptr<CoroutineRuntimeState> runtime;
    std::string suspend_reason;
    std::atomic<bool> completed{false};
    std::atomic<bool> worker_completed{false};
    std::atomic<bool> timeout_background_tracked{false};
    std::exception_ptr exception;
    AsyncCompletion completion_reason = AsyncCompletion::kCompleted;
    detail::AsyncResultStorage<Result> result;
    std::shared_ptr<asio::steady_timer> timer;
  };

  struct ParallelWaitState {
    using StopCallback = std::stop_callback<std::function<void()>>;

    ParallelWaitState(CoroutineExecutor* executor,
                      asio::io_context& io_context,
                      size_t total_tasks,
                      std::shared_ptr<CoroutineRuntimeState> runtime,
                      bool when_all_fail_fast,
                      bool when_any_cancel_losers = false)
        : executor(executor),
          io_context(io_context),
          remaining(total_tasks),
          runtime(std::move(runtime)),
          fail_fast_enabled(when_all_fail_fast),
          cancel_losers_enabled(when_any_cancel_losers) {}

    bool FailFastEnabled() const noexcept { return fail_fast_enabled; }

    bool IsCancellationRequested() const noexcept {
      return cancellation_requested.load(std::memory_order_acquire);
    }

    void AttachExternalStopToken(std::stop_token stop_token) {
      if (!stop_token.stop_possible()) {
        return;
      }

      external_stop_callback = std::make_unique<StopCallback>(
          stop_token,
          [this]() {
            RequestCancel(std::make_exception_ptr(CancelledError("WhenAll cancelled")));
          });

      if (stop_token.stop_requested()) {
        RequestCancel(std::make_exception_ptr(CancelledError("WhenAll cancelled")));
      }
    }

    void RegisterCancellableTask(const std::shared_ptr<Task<void>>& task) {
      if (!task || !task->IsValid()) {
        return;
      }

      bool cancel_now = false;
      {
        std::lock_guard<std::mutex> lock(cancellation_mutex);
        if (cancellation_requested.load(std::memory_order_acquire)) {
          cancel_now = true;
        } else {
          cancellable_tasks.emplace_back(task);
        }
      }

      if (cancel_now) {
        (void)task->RequestStop();
      }
    }

    void RecordException(const std::exception_ptr& exception) {
      if (!exception) {
        return;
      }
      std::lock_guard<std::mutex> lock(exception_mutex);
      if (!first_exception) {
        first_exception = exception;
      }
    }

    void CompleteAll() {
      const size_t previous = remaining.fetch_sub(1, std::memory_order_acq_rel);
      if (previous == 1) {
        PostResume();
      }
    }

    void RequestCancel(const std::exception_ptr& exception = nullptr) {
      if (exception) {
        RecordException(exception);
      }

      if (!CancelRegisteredTasks()) {
        return;
      }

      if (executor) {
        executor->IncrementMetricCounter("logic.coroutine.whenall.cancel_requested_total");
        if (fail_fast_enabled) {
          executor->IncrementMetricCounter(
              "logic.coroutine.whenall.fail_fast_triggered_total");
        }
      }

      PostResume();
    }

    void CompleteAny(size_t index, const std::exception_ptr& exception = nullptr) {
      size_t expected = kWhenAnyNoTaskIndex;
      if (winner_index.compare_exchange_strong(expected,
                                               index,
                                               std::memory_order_acq_rel,
                                               std::memory_order_acquire)) {
        if (exception) {
          RecordException(exception);
        }
        if (cancel_losers_enabled) {
          (void)CancelRegisteredTasks();
        }
        PostResume();
      }
      remaining.fetch_sub(1, std::memory_order_acq_rel);
    }

    void PostResume() {
      if (resume_posted.exchange(true, std::memory_order_acq_rel)) {
        return;
      }
      if (!continuation) {
        return;
      }
      CoroutineExecutor* executor_ptr = executor;
      asio::post(io_context,
                 [executor_ptr, continuation = continuation, runtime = runtime]() mutable {
        if (executor_ptr) {
          executor_ptr->ResumeHandle(continuation, runtime, "parallel_wait_resume");
          return;
        }
        continuation.resume();
      });
    }

    CoroutineExecutor* executor = nullptr;
    asio::io_context& io_context;
    std::atomic<size_t> remaining{0};
    std::atomic<size_t> winner_index{kWhenAnyNoTaskIndex};
    std::atomic<bool> resume_posted{false};
    std::mutex exception_mutex;
    std::exception_ptr first_exception;
    std::coroutine_handle<> continuation{};
    std::shared_ptr<CoroutineRuntimeState> runtime;
    std::stop_source cancel_source;
    std::atomic<bool> cancellation_requested{false};
    bool fail_fast_enabled = false;
    bool cancel_losers_enabled = false;
    std::mutex cancellation_mutex;
    std::vector<std::weak_ptr<Task<void>>> cancellable_tasks;
    std::unique_ptr<StopCallback> external_stop_callback;

   private:
    bool CancelRegisteredTasks() {
      const bool first_cancel =
          !cancellation_requested.exchange(true, std::memory_order_acq_rel);
      if (!first_cancel) {
        return false;
      }

      cancel_source.request_stop();

      std::vector<std::shared_ptr<Task<void>>> tasks_to_cancel;
      {
        std::lock_guard<std::mutex> lock(cancellation_mutex);
        tasks_to_cancel.reserve(cancellable_tasks.size());
        for (const auto& weak_task : cancellable_tasks) {
          if (auto task = weak_task.lock()) {
            tasks_to_cancel.push_back(std::move(task));
          }
        }
        cancellable_tasks.clear();
      }

      for (const auto& task : tasks_to_cancel) {
        (void)task->RequestStop();
      }

      return true;
    }
  };

  static std::exception_ptr BuildSpawnRejectedException(SpawnResult reason,
                                                        size_t index) {
    return std::make_exception_ptr(
        std::runtime_error("parallel spawn rejected index=" + std::to_string(index) +
                           " reason=" + ToString(reason)));
  }

  static Task<void> RunWhenAllBranch(std::shared_ptr<ParallelWaitState> state,
                                     Task<void> task) {
    auto branch_task = std::make_shared<Task<void>>(std::move(task));
    state->RegisterCancellableTask(branch_task);
    if (state->IsCancellationRequested()) {
      (void)branch_task->RequestStop();
    }

    try {
      if (branch_task->IsValid()) {
        co_await *branch_task;
      }
    } catch (...) {
      const std::exception_ptr exception = std::current_exception();
      state->RecordException(exception);
      if (state->FailFastEnabled()) {
        state->RequestCancel(exception);
      }
    }
    state->CompleteAll();
    co_return;
  }

  static Task<void> RunWhenAnyBranch(std::shared_ptr<ParallelWaitState> state,
                                     size_t index,
                                     Task<void> task) {
    auto branch_task = std::make_shared<Task<void>>(std::move(task));
    state->RegisterCancellableTask(branch_task);
    if (state->IsCancellationRequested()) {
      (void)branch_task->RequestStop();
      state->CompleteAny(index);
      co_return;
    }

    std::exception_ptr exception;
    try {
      if (branch_task->IsValid()) {
        co_await *branch_task;
      }
    } catch (...) {
      exception = std::current_exception();
    }
    state->CompleteAny(index, exception);
    co_return;
  }

  template <typename Fn, typename Result>
  class AsyncAwaiter {
   public:
    AsyncAwaiter(CoroutineExecutor* executor,
                 Fn&& fn,
                 std::chrono::steady_clock::duration timeout,
                 bool enable_timeout,
                 std::stop_token stop_token)
        : executor_(executor),
          fn_(std::move(fn)),
          timeout_(timeout),
          enable_timeout_(enable_timeout),
          stop_token_(stop_token) {}

    bool await_ready() const noexcept { return false; }

    void await_suspend(std::coroutine_handle<> handle) {
      auto runtime = executor_ ? executor_->CurrentRuntime() : nullptr;
      auto lifetime_state = executor_->lifetime_state_;
      if (!lifetime_state) {
        asio::post(executor_->io_context_,
                   [executor = executor_, handle, runtime]() mutable {
                     if (executor) {
                       executor->ResumeHandle(handle, runtime, "async_resume_no_lifetime");
                       return;
                     }
                     handle.resume();
                   });
        return;
      }

      lifetime_state->IncrementSuspended();
      auto state = std::make_shared<AsyncState<Result>>(
          executor_,
          lifetime_state,
          handle,
          runtime,
          enable_timeout_ ? "async_with_timeout" : "async");
      if (executor_ && runtime) {
        executor_->MarkCoroutineSuspended(runtime, state->suspend_reason.c_str());
      }
      state_ = state;

      if (enable_timeout_) {
        state->timer = executor_ ? executor_->AcquireReusableTimer()
                                 : std::make_shared<asio::steady_timer>(lifetime_state->io_context);
        state->timer->expires_after(timeout_);
        state->timer->async_wait([state](const asio::error_code& ec) {
          if (ec) {
            return;
          }
          state->TryResume(AsyncCompletion::kTimedOut);
        });
      }

      if (stop_token_.stop_possible()) {
        std::weak_ptr<AsyncState<Result>> weak_state = state;
        stop_callback_ = std::make_unique<StopCallback>(
            stop_token_,
            [weak_state]() {
              auto state = weak_state.lock();
              if (!state || !state->lifetime_state || state->lifetime_state->IsStopRequested()) {
                return;
              }

              state->lifetime_state->IncrementPendingResumeCallbacks();
              asio::post(state->lifetime_state->io_context, [state]() {
                state->TryResume(AsyncCompletion::kCancelled);
                if (!state->lifetime_state) {
                  return;
                }
                state->lifetime_state->DecrementPendingResumeCallbacks();
              });
            });
        if (stop_token_.stop_requested()) {
          return;
        }
      }

      auto fn = std::move(fn_);
      asio::post(executor_->blocking_pool_, [state, fn = std::move(fn)]() mutable {
        AssertNotOnLogicThread("CoroutineExecutor::AsyncWorker");
        try {
          if constexpr (std::is_void_v<Result>) {
            fn();
            state->result.Set();
          } else {
            state->result.Set(fn());
          }
        } catch (...) {
          state->exception = std::current_exception();
        }

        state->MarkWorkerCompleted();

        auto lifetime_state = state->lifetime_state;
        if (!lifetime_state || lifetime_state->IsStopRequested()) {
          return;
        }

        lifetime_state->IncrementPendingResumeCallbacks();
        asio::post(lifetime_state->io_context, [state]() {
          state->TryResume(AsyncCompletion::kCompleted);
          if (!state->lifetime_state) {
            return;
          }
          state->lifetime_state->DecrementPendingResumeCallbacks();
        });
      });
    }

    Result await_resume() {
      auto state = state_;
      if (!state) {
        if constexpr (std::is_void_v<Result>) {
          return;
        }
        throw std::runtime_error("Async awaiter state missing");
      }

      if (state->completion_reason == AsyncCompletion::kTimedOut) {
        throw TimeoutError("Coroutine operation timed out");
      }

      if (state->completion_reason == AsyncCompletion::kCancelled) {
        throw CancelledError("Coroutine operation cancelled");
      }

      if (state->exception) {
        std::rethrow_exception(state->exception);
      }

      if constexpr (std::is_void_v<Result>) {
        state->result.Take();
        return;
      } else {
        return state->result.Take();
      }
    }

   private:
    CoroutineExecutor* executor_ = nullptr;
    Fn fn_;
    std::shared_ptr<AsyncState<Result>> state_;
    std::chrono::steady_clock::duration timeout_{};
    bool enable_timeout_ = false;
    std::stop_token stop_token_;
    using StopCallback = std::stop_callback<std::function<void()>>;
    std::unique_ptr<StopCallback> stop_callback_;
  };

  class SleepAwaiter {
   public:
    SleepAwaiter(CoroutineExecutor* executor,
                 std::shared_ptr<ExecutorLifetimeState> lifetime_state,
                 std::chrono::steady_clock::duration duration,
                 std::stop_token stop_token)
        : executor_(executor),
          lifetime_state_(std::move(lifetime_state)),
          duration_(duration),
          stop_token_(stop_token),
          runtime_(executor ? executor->CurrentRuntime() : nullptr) {
      if (lifetime_state_ && duration_ > std::chrono::steady_clock::duration::zero()) {
        timer_ = executor_ ? executor_->AcquireReusableTimer()
                           : std::make_shared<asio::steady_timer>(lifetime_state_->io_context);
        timer_->expires_after(duration_);
      }
    }

    bool await_ready() const noexcept {
      return duration_ <= std::chrono::steady_clock::duration::zero();
    }

    void await_suspend(std::coroutine_handle<> handle) {
      if (!lifetime_state_) {
        return;
      }

      if (stop_token_.stop_possible()) {
        std::weak_ptr<asio::steady_timer> weak_timer = timer_;
        auto lifetime_state = lifetime_state_;
        stop_callback_ = std::make_unique<StopCallback>(
            stop_token_,
            [this, weak_timer, lifetime_state]() {
              cancelled_.store(true, std::memory_order_release);
              if (auto timer = weak_timer.lock()) {
                asio::error_code ignored;
                timer->cancel(ignored);
                return;
              }
              if (!lifetime_state || lifetime_state->IsStopRequested()) {
                return;
              }
              asio::post(lifetime_state->io_context, [this]() {
                if (continuation_) {
                  if (executor_) {
                    executor_->ResumeHandle(continuation_, runtime_, "sleep_cancelled");
                    return;
                  }
                  continuation_.resume();
                }
              });
            });
      }

      if (!timer_) {
        // Resume via post to avoid inline resume on await_suspend path.
        asio::post(lifetime_state_->io_context,
                   [this, handle]() mutable {
                     if (executor_) {
                       executor_->ResumeHandle(handle, runtime_, "sleep_resume_posted");
                       return;
                     }
                     handle.resume();
                   });
        return;
      }

      if (stop_token_.stop_requested()) {
        cancelled_.store(true, std::memory_order_release);
        asio::post(lifetime_state_->io_context,
                   [this, handle]() mutable {
                     if (executor_) {
                       executor_->ResumeHandle(handle, runtime_, "sleep_cancelled");
                       return;
                     }
                     handle.resume();
                   });
        return;
      }

      continuation_ = handle;
      lifetime_state_->IncrementSuspended();
      if (executor_ && runtime_) {
        executor_->MarkCoroutineSuspended(runtime_, "sleep_for");
      }
      timer_->async_wait([this, handle, timer = timer_, lifetime_state = lifetime_state_]
                         (const asio::error_code& ec) {
        error_ = ec;
        if (!lifetime_state) {
          return;
        }
        lifetime_state->DecrementSuspended();
        if (lifetime_state->IsStopRequested()) {
          return;
        }
        if (executor_) {
          const char* reason =
              cancelled_.load(std::memory_order_acquire)
                  ? "sleep_cancelled"
                  : "sleep_ready";
          executor_->ResumeHandle(handle, runtime_, reason);
          return;
        }
        handle.resume();
      });
    }

    void await_resume() {
      if (cancelled_.load(std::memory_order_acquire)) {
        throw CancelledError("SleepFor cancelled");
      }
      if (error_ && error_ != asio::error::operation_aborted) {
        throw std::runtime_error("SleepFor failed: " + error_.message());
      }
    }

   private:
    CoroutineExecutor* executor_ = nullptr;
    std::shared_ptr<ExecutorLifetimeState> lifetime_state_;
    std::chrono::steady_clock::duration duration_{};
    std::shared_ptr<asio::steady_timer> timer_;
    asio::error_code error_;
    std::stop_token stop_token_;
    std::atomic<bool> cancelled_{false};
    std::coroutine_handle<> continuation_{};
    std::shared_ptr<CoroutineRuntimeState> runtime_;
    using StopCallback = std::stop_callback<std::function<void()>>;
    std::unique_ptr<StopCallback> stop_callback_;
  };

  class WhenAllAwaiter {
   public:
    WhenAllAwaiter(CoroutineExecutor* executor,
                   std::vector<Task<void>>&& tasks,
                   ParallelSpawnRejectedCallback&& on_spawn_rejected,
                   WhenAllOptions options)
        : executor_(executor),
          tasks_(std::move(tasks)),
          on_spawn_rejected_(std::move(on_spawn_rejected)),
          options_(options) {}

    bool await_ready() const noexcept { return tasks_.empty(); }

    bool await_suspend(std::coroutine_handle<> continuation) {
      if (!executor_) {
        return false;
      }
      state_ = std::make_shared<ParallelWaitState>(
          executor_,
          executor_->io_context_,
          tasks_.size(),
          executor_->CurrentRuntime(),
          options_.fail_fast);
      state_->continuation = continuation;
      state_->AttachExternalStopToken(options_.stop_token);

      for (size_t i = 0; i < tasks_.size(); ++i) {
        if (options_.fail_fast && state_->IsCancellationRequested()) {
          state_->CompleteAll();
          continue;
        }

        SpawnResult result =
            executor_->TrySpawn(RunWhenAllBranch(state_, std::move(tasks_[i])));
        if (result == SpawnResult::kSpawned) {
          continue;
        }
        std::exception_ptr spawn_exception;
        if (on_spawn_rejected_) {
          on_spawn_rejected_(i, result);
        } else {
          spawn_exception = BuildSpawnRejectedException(result, i);
          state_->RecordException(spawn_exception);
        }
        if (options_.fail_fast) {
          state_->RequestCancel(spawn_exception);
        }
        state_->CompleteAll();
      }
      tasks_.clear();
      return true;
    }

    void await_resume() {
      if (!state_) {
        return;
      }
      std::exception_ptr exception;
      {
        std::lock_guard<std::mutex> lock(state_->exception_mutex);
        exception = state_->first_exception;
      }
      if (exception) {
        std::rethrow_exception(exception);
      }
    }

   private:
    CoroutineExecutor* executor_ = nullptr;
    std::vector<Task<void>> tasks_;
    ParallelSpawnRejectedCallback on_spawn_rejected_;
    WhenAllOptions options_{};
    std::shared_ptr<ParallelWaitState> state_;
  };

  class WhenAnyAwaiter {
   public:
    WhenAnyAwaiter(CoroutineExecutor* executor,
                   std::vector<Task<void>>&& tasks,
                   ParallelSpawnRejectedCallback&& on_spawn_rejected,
                   WhenAnyOptions options)
        : executor_(executor),
          tasks_(std::move(tasks)),
          on_spawn_rejected_(std::move(on_spawn_rejected)),
          options_(options) {}

    bool await_ready() const noexcept { return tasks_.empty(); }

    bool await_suspend(std::coroutine_handle<> continuation) {
      if (!executor_) {
        return false;
      }
      state_ = std::make_shared<ParallelWaitState>(
          executor_,
          executor_->io_context_,
          tasks_.size(),
          executor_->CurrentRuntime(),
          false,
          options_.cancel_losers);
      state_->continuation = continuation;

      for (size_t i = 0; i < tasks_.size(); ++i) {
        SpawnResult result =
            executor_->TrySpawn(RunWhenAnyBranch(state_, i, std::move(tasks_[i])));
        if (result == SpawnResult::kSpawned) {
          continue;
        }
        if (on_spawn_rejected_) {
          on_spawn_rejected_(i, result);
        } else {
          state_->RecordException(BuildSpawnRejectedException(result, i));
        }
        state_->CompleteAny(i);
      }
      tasks_.clear();
      return true;
    }

    size_t await_resume() {
      if (!state_) {
        return kWhenAnyNoTaskIndex;
      }
      std::exception_ptr exception;
      {
        std::lock_guard<std::mutex> lock(state_->exception_mutex);
        exception = state_->first_exception;
      }
      if (exception) {
        std::rethrow_exception(exception);
      }
      return state_->winner_index.load(std::memory_order_acquire);
    }

   private:
    CoroutineExecutor* executor_ = nullptr;
    std::vector<Task<void>> tasks_;
    ParallelSpawnRejectedCallback on_spawn_rejected_;
    WhenAnyOptions options_{};
    std::shared_ptr<ParallelWaitState> state_;
  };

  void IncrementSuspended();
  void DecrementSuspended();
  void IncrementRunning();
  void DecrementRunning();
  void IncrementPendingResumeCallbacks();
  void DecrementPendingResumeCallbacks();
  void IncrementTimeoutBackgroundInflight();
  void DecrementTimeoutBackgroundInflight();
  void IncrementTimeoutBackgroundTotal();
  std::shared_ptr<asio::steady_timer> AcquireReusableTimer();
  static void RecycleTimer(std::weak_ptr<ReusableTimerPool> weak_pool,
                           asio::steady_timer* timer) noexcept;
  void IncrementMetricCounter(const std::string& name, uint64_t delta = 1) const;
  void SetMetricGauge(const std::string& name, double value) const;
  SpawnResult BeginSpawn();
  void RecordSpawnRejected(SpawnResult reason) noexcept;
  void StopStarvationWatchdog() noexcept;
  void RunStarvationWatchdog(std::stop_token stop_token);
  bool IsDrained() const noexcept;
  void NotifyDrainWaiters() noexcept;
  bool WaitForDrain(std::chrono::steady_clock::duration timeout);
  std::shared_ptr<CoroutineRuntimeState> RegisterCoroutine(CoroutineMetadata metadata);
  void CompleteCoroutine(const std::shared_ptr<CoroutineRuntimeState>& runtime);
  void ResumeHandle(std::coroutine_handle<> handle,
                    const std::shared_ptr<CoroutineRuntimeState>& runtime,
                    const char* reason);
  std::shared_ptr<CoroutineRuntimeState> CurrentRuntime() const;
  void MarkCoroutineRunning(const std::shared_ptr<CoroutineRuntimeState>& runtime,
                            const char* reason);
  void MarkCoroutineSuspended(const std::shared_ptr<CoroutineRuntimeState>& runtime,
                              const char* reason);
  CoroutineSnapshot BuildSnapshotLocked(
      const CoroutineRuntimeState& runtime,
      std::chrono::steady_clock::time_point now) const;
  void EmitTraceInstant(const char* phase,
                        const CoroutineRuntimeState& runtime,
                        std::chrono::steady_clock::time_point ts,
                        const char* reason);
  std::string BuildHandlerGaugeMetricName(const std::string& handler_key) const;

  asio::io_context& io_context_;
  asio::thread_pool blocking_pool_;
  std::shared_ptr<ExecutorLifetimeState> lifetime_state_;
  std::atomic<int64_t> suspended_count_{0};
  std::atomic<int64_t> running_count_{0};
  std::atomic<int64_t> pending_resume_callbacks_{0};
  std::atomic<int64_t> timeout_background_inflight_{0};
  std::atomic<bool> accepting_tasks_{true};
  std::atomic<bool> blocking_pool_joined_{false};
  std::shared_ptr<monitor::IMetricsSink> metrics_sink_;
  std::shared_ptr<ReusableTimerPool> timer_pool_;
  const int64_t max_running_tasks_;
  std::chrono::steady_clock::duration starvation_watchdog_threshold_{};
  std::chrono::steady_clock::duration starvation_watchdog_poll_interval_{};
  std::jthread starvation_watchdog_thread_;
  mutable std::mutex coroutine_mutex_;
  std::unordered_map<uint64_t, std::shared_ptr<CoroutineRuntimeState>> active_coroutines_;
  std::unordered_map<std::string, int64_t> handler_inflight_counts_;
  std::unordered_map<std::string, std::string> handler_metric_names_;
  std::shared_ptr<TraceWriter> trace_writer_;
  std::atomic<uint64_t> next_coroutine_id_{1};
  std::atomic<uint64_t> next_trace_id_{1};
  mutable std::mutex drain_mutex_;
  std::condition_variable drain_cv_;
};

inline Task<void> CoroutineExecutor::SleepFor(std::chrono::steady_clock::duration duration,
                                              std::stop_token stop_token) {
  co_await SleepAwaiter(this, lifetime_state_, duration, stop_token);
}

inline Task<void> CoroutineExecutor::WhenAll(
    std::vector<Task<void>> tasks,
    ParallelSpawnRejectedCallback on_spawn_rejected) {
  co_await WhenAllAwaiter(
      this, std::move(tasks), std::move(on_spawn_rejected), WhenAllOptions{});
}

inline Task<void> CoroutineExecutor::WhenAll(
    std::vector<Task<void>> tasks,
    ParallelSpawnRejectedCallback on_spawn_rejected,
    WhenAllOptions options) {
  co_await WhenAllAwaiter(
      this, std::move(tasks), std::move(on_spawn_rejected), options);
}

inline Task<size_t> CoroutineExecutor::WhenAny(
    std::vector<Task<void>> tasks,
    ParallelSpawnRejectedCallback on_spawn_rejected) {
  co_return co_await WhenAny(
      std::move(tasks), std::move(on_spawn_rejected), WhenAnyOptions{});
}

inline Task<size_t> CoroutineExecutor::WhenAny(
    std::vector<Task<void>> tasks,
    ParallelSpawnRejectedCallback on_spawn_rejected,
    WhenAnyOptions options) {
  co_return co_await WhenAnyAwaiter(
      this, std::move(tasks), std::move(on_spawn_rejected), options);
}

}  // namespace mir2::logic

#endif  // MIR2_LOGIC_COROUTINE_EXECUTOR_H_
