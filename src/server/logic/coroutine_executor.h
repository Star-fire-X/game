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
#include <condition_variable>
#include <coroutine>
#include <cstdint>
#include <exception>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <utility>

#include "logic/task.h"

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

class CoroutineExecutor {
 public:
  explicit CoroutineExecutor(asio::io_context& io_context,
                             size_t blocking_threads = std::max(1u, std::thread::hardware_concurrency()));
  ~CoroutineExecutor();

  CoroutineExecutor(const CoroutineExecutor&) = delete;
  CoroutineExecutor& operator=(const CoroutineExecutor&) = delete;

  asio::io_context& GetIoContext() const noexcept { return io_context_; }

  template <typename T>
  bool Spawn(Task<T> task) {
    if (!task.IsValid()) {
      return false;
    }
    if (!BeginSpawn()) {
      return false;
    }
    auto wrapped = SpawnWrapped(this, std::move(task));

    auto handle = wrapped.Release();
    if (!handle) {
      DecrementRunning();
      return false;
    }
    asio::post(io_context_, [handle]() mutable { handle.resume(); });
    return true;
  }

  void StopAccepting() noexcept;
  bool IsAccepting() const noexcept {
    return accepting_tasks_.load(std::memory_order_acquire);
  }
  bool DrainAndJoin(std::chrono::steady_clock::duration timeout);

  Task<void> SleepFor(std::chrono::steady_clock::duration duration);

  template <typename Fn>
  auto Async(Fn&& fn) -> Task<std::invoke_result_t<Fn>> {
    using FnType = std::decay_t<Fn>;
    using Result = std::invoke_result_t<FnType>;
    static_assert(!std::is_reference_v<Result>, "Async cannot return references");
    return RunAsync<FnType, Result>(
        this, FnType(std::forward<Fn>(fn)),
        std::chrono::steady_clock::duration::zero(), false);
  }

  template <typename Fn>
  auto AsyncWithTimeout(Fn&& fn, std::chrono::steady_clock::duration timeout)
      -> Task<std::invoke_result_t<Fn>> {
    using FnType = std::decay_t<Fn>;
    using Result = std::invoke_result_t<FnType>;
    static_assert(!std::is_reference_v<Result>, "AsyncWithTimeout cannot return references");
    return RunAsync<FnType, Result>(
        this, FnType(std::forward<Fn>(fn)), timeout, true);
  }

  int64_t SuspendedCount() const noexcept { return suspended_count_.load(); }
  int64_t RunningCount() const noexcept { return running_count_.load(); }

 private:
  template <typename T>
  static Task<void> SpawnWrapped(CoroutineExecutor* executor, Task<T> task) {
    struct RunningGuard {
      CoroutineExecutor* executor = nullptr;
      ~RunningGuard() {
        if (executor) {
          executor->DecrementRunning();
        }
      }
    } guard{executor};

    co_await std::move(task);
  }

  template <typename Fn, typename Result>
  static Task<Result> RunAsync(CoroutineExecutor* executor,
                               Fn fn,
                               std::chrono::steady_clock::duration timeout,
                               bool enable_timeout) {
    co_return co_await AsyncAwaiter<Fn, Result>(
        executor, std::move(fn), timeout, enable_timeout);
  }

  template <typename Result>
  struct AsyncState {
    AsyncState(CoroutineExecutor* executor, std::coroutine_handle<> continuation)
        : executor(executor), continuation(continuation) {}

    void TryResume(bool timed_out_flag) {
      if (completed.exchange(true)) {
        return;
      }
      timed_out = timed_out_flag;
      if (!timed_out_flag && timer) {
        asio::error_code ignored;
        timer->cancel(ignored);
      }
      executor->DecrementSuspended();
      continuation.resume();
    }

    CoroutineExecutor* executor = nullptr;
    std::coroutine_handle<> continuation;
    std::atomic<bool> completed{false};
    std::exception_ptr exception;
    bool timed_out = false;
    detail::AsyncResultStorage<Result> result;
    std::shared_ptr<asio::steady_timer> timer;
  };

  template <typename Fn, typename Result>
  class AsyncAwaiter {
   public:
    AsyncAwaiter(CoroutineExecutor* executor,
                 Fn&& fn,
                 std::chrono::steady_clock::duration timeout,
                 bool enable_timeout)
        : executor_(executor),
          fn_(std::move(fn)),
          timeout_(timeout),
          enable_timeout_(enable_timeout) {}

    bool await_ready() const noexcept { return false; }

    void await_suspend(std::coroutine_handle<> handle) {
      executor_->IncrementSuspended();
      auto state = std::make_shared<AsyncState<Result>>(executor_, handle);
      state_ = state;

      if (enable_timeout_) {
        state->timer = std::make_shared<asio::steady_timer>(executor_->io_context_);
        state->timer->expires_after(timeout_);
        state->timer->async_wait([state](const asio::error_code& ec) {
          if (ec) {
            return;
          }
          state->TryResume(true);
        });
      }

      auto fn = std::move(fn_);
      asio::post(executor_->blocking_pool_, [state, fn = std::move(fn)]() mutable {
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

        state->executor->IncrementPendingResumeCallbacks();
        asio::post(state->executor->io_context_, [state]() {
          state->TryResume(false);
          state->executor->DecrementPendingResumeCallbacks();
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

      if (state->timed_out) {
        throw TimeoutError("Coroutine operation timed out");
      }

      if (state->exception) {
        std::rethrow_exception(state->exception);
      }

      if constexpr (!std::is_void_v<Result>) {
        return state->result.Take();
      }
    }

   private:
    CoroutineExecutor* executor_ = nullptr;
    Fn fn_;
    std::shared_ptr<AsyncState<Result>> state_;
    std::chrono::steady_clock::duration timeout_{};
    bool enable_timeout_ = false;
  };

  class SleepAwaiter {
   public:
    SleepAwaiter(CoroutineExecutor* executor, std::chrono::steady_clock::duration duration)
        : executor_(executor), duration_(duration) {
      if (duration_ > std::chrono::steady_clock::duration::zero()) {
        timer_ = std::make_shared<asio::steady_timer>(executor_->io_context_);
        timer_->expires_after(duration_);
      }
    }

    bool await_ready() const noexcept {
      return duration_ <= std::chrono::steady_clock::duration::zero();
    }

    void await_suspend(std::coroutine_handle<> handle) {
      if (!timer_) {
        handle.resume();
        return;
      }

      executor_->IncrementSuspended();
      timer_->async_wait([this, handle, timer = timer_](const asio::error_code& ec) {
        error_ = ec;
        executor_->DecrementSuspended();
        handle.resume();
      });
    }

    void await_resume() {
      if (error_ && error_ != asio::error::operation_aborted) {
        throw std::runtime_error("SleepFor failed: " + error_.message());
      }
    }

   private:
    CoroutineExecutor* executor_ = nullptr;
    std::chrono::steady_clock::duration duration_{};
    std::shared_ptr<asio::steady_timer> timer_;
    asio::error_code error_;
  };

  void IncrementSuspended();
  void DecrementSuspended();
  void IncrementRunning();
  void DecrementRunning();
  void IncrementPendingResumeCallbacks();
  void DecrementPendingResumeCallbacks();
  bool BeginSpawn();
  bool IsDrained() const noexcept;
  void NotifyDrainWaiters() noexcept;
  bool WaitForDrain(std::chrono::steady_clock::duration timeout);

  asio::io_context& io_context_;
  asio::thread_pool blocking_pool_;
  std::atomic<int64_t> suspended_count_{0};
  std::atomic<int64_t> running_count_{0};
  std::atomic<int64_t> pending_resume_callbacks_{0};
  std::atomic<bool> accepting_tasks_{true};
  std::atomic<bool> blocking_pool_joined_{false};
  mutable std::mutex drain_mutex_;
  std::condition_variable drain_cv_;
};

inline Task<void> CoroutineExecutor::SleepFor(std::chrono::steady_clock::duration duration) {
  co_await SleepAwaiter(this, duration);
}

}  // namespace mir2::logic

#endif  // MIR2_LOGIC_COROUTINE_EXECUTOR_H_
