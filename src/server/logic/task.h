/**
 * @file task.h
 * @brief C++20 coroutine task wrapper.
 */

#ifndef MIR2_LOGIC_TASK_H_
#define MIR2_LOGIC_TASK_H_

#include <atomic>
#include <concepts>
#include <coroutine>
#include <exception>
#include <optional>
#include <stop_token>
#include <type_traits>
#include <utility>

namespace mir2::logic {

template <typename T = void>
class Task;

using TaskUnhandledExceptionReporter =
    void (*)(bool detached, const std::exception_ptr& exception);
using TaskDetachedExceptionReporter =
    void (*)(const std::exception_ptr& exception);

namespace detail {

template <typename T>
concept Awaitable =
    !std::is_void_v<std::remove_cvref_t<T>> &&
    requires(std::remove_cvref_t<T>& awaitable, std::coroutine_handle<> continuation) {
      { awaitable.await_ready() } -> std::convertible_to<bool>;
      awaitable.await_suspend(continuation);
      awaitable.await_resume();
    };

inline std::atomic<TaskUnhandledExceptionReporter>& TaskUnhandledExceptionReporterSlot() {
  static std::atomic<TaskUnhandledExceptionReporter> slot{nullptr};
  return slot;
}

inline std::atomic<TaskDetachedExceptionReporter>& TaskDetachedExceptionReporterSlot() {
  static std::atomic<TaskDetachedExceptionReporter> slot{nullptr};
  return slot;
}

inline void ReportTaskUnhandledException(bool detached,
                                         const std::exception_ptr& exception) noexcept {
  TaskUnhandledExceptionReporter reporter =
      TaskUnhandledExceptionReporterSlot().load(std::memory_order_acquire);
  if (reporter == nullptr) {
    return;
  }

  try {
    reporter(detached, exception);
  } catch (...) {
  }
}

inline void ReportTaskDetachedException(const std::exception_ptr& exception) noexcept {
  TaskDetachedExceptionReporter reporter =
      TaskDetachedExceptionReporterSlot().load(std::memory_order_acquire);
  if (reporter == nullptr) {
    return;
  }

  try {
    reporter(exception);
  } catch (...) {
  }
}

struct TaskPromiseBase {
  struct FinalAwaiter {
    bool await_ready() const noexcept { return false; }

    template <typename Promise>
    std::coroutine_handle<> await_suspend(std::coroutine_handle<Promise> handle) noexcept {
      auto& promise = handle.promise();
      if (promise.detached_) {
        if (promise.exception_) {
          ReportTaskDetachedException(promise.exception_);
        }
        handle.destroy();
        return std::noop_coroutine();
      }

      if (promise.continuation_) {
        return promise.continuation_;
      }
      return std::noop_coroutine();
    }

    void await_resume() noexcept {}
  };

  std::suspend_always initial_suspend() noexcept { return {}; }
  FinalAwaiter final_suspend() noexcept { return {}; }

  void unhandled_exception() noexcept {
    exception_ = std::current_exception();
    const bool terminal_exception = detached_ || continuation_ == nullptr;
    ReportTaskUnhandledException(terminal_exception, exception_);
  }

  std::stop_token GetStopToken() const noexcept {
    return stop_source_.get_token();
  }

  bool RequestStop() noexcept {
    return stop_source_.request_stop();
  }

  std::exception_ptr exception_;
  std::coroutine_handle<> continuation_;
  bool detached_ = false;
  std::stop_source stop_source_;
};

template <typename T>
struct TaskPromiseStorage {
  template <typename U>
  void ReturnValue(U&& value) noexcept(std::is_nothrow_constructible_v<T, U&&>) {
    value_.emplace(std::forward<U>(value));
  }

  T ConsumeValue() {
    return std::move(value_.value());
  }

  std::optional<T> value_;
};

template <>
struct TaskPromiseStorage<void> {
  void ReturnVoid() noexcept {}
  void ConsumeValue() noexcept {}
};

template <typename T>
struct TaskPromise : TaskPromiseBase, TaskPromiseStorage<T> {
  using Handle = std::coroutine_handle<TaskPromise<T>>;

  Task<T> get_return_object() noexcept {
    return Task<T>(Handle::from_promise(*this));
  }

  template <typename U = T>
    requires (!std::is_void_v<U>)
  void return_value(U value) noexcept(std::is_nothrow_move_constructible_v<U>) {
    this->ReturnValue(std::move(value));
  }
};

template <>
struct TaskPromise<void> : TaskPromiseBase, TaskPromiseStorage<void> {
  using Handle = std::coroutine_handle<TaskPromise<void>>;

  Task<void> get_return_object() noexcept;

  void return_void() noexcept {
    this->ReturnVoid();
  }
};

}  // namespace detail

inline TaskUnhandledExceptionReporter SetTaskUnhandledExceptionReporter(
    TaskUnhandledExceptionReporter reporter) noexcept {
  return detail::TaskUnhandledExceptionReporterSlot().exchange(
      reporter, std::memory_order_acq_rel);
}

inline TaskDetachedExceptionReporter SetTaskDetachedExceptionReporter(
    TaskDetachedExceptionReporter reporter) noexcept {
  return detail::TaskDetachedExceptionReporterSlot().exchange(
      reporter, std::memory_order_acq_rel);
}

template <typename T>
class Task {
 public:
  using promise_type = detail::TaskPromise<T>;
  using Handle = std::coroutine_handle<promise_type>;

  explicit Task(Handle handle) noexcept : handle_(handle) {}

  Task(Task&& other) noexcept : handle_(std::exchange(other.handle_, {})) {}

  Task& operator=(Task&& other) noexcept {
    if (this != &other) {
      if (handle_) {
        handle_.destroy();
      }
      handle_ = std::exchange(other.handle_, {});
    }
    return *this;
  }

  Task(const Task&) = delete;
  Task& operator=(const Task&) = delete;

  ~Task() noexcept {
    if (handle_) {
      handle_.destroy();
    }
  }

  bool await_ready() const noexcept {
    return !handle_ || handle_.done();
  }

  std::coroutine_handle<> await_suspend(std::coroutine_handle<> continuation) noexcept {
    handle_.promise().continuation_ = continuation;
    return handle_;
  }

  decltype(auto) await_resume() {
    auto& promise = handle_.promise();
    if (promise.exception_) {
      std::rethrow_exception(promise.exception_);
    }

    if constexpr (std::is_void_v<T>) {
      promise.ConsumeValue();
      return;
    } else {
      return promise.ConsumeValue();
    }
  }

  Handle Release() noexcept {
    if (handle_) {
      handle_.promise().detached_ = true;
    }
    return std::exchange(handle_, {});
  }

  bool IsValid() const noexcept { return handle_ != nullptr; }

  std::stop_token GetStopToken() const noexcept {
    if (!handle_) {
      return {};
    }
    return handle_.promise().GetStopToken();
  }

  bool RequestStop() noexcept {
    if (!handle_) {
      return false;
    }
    return handle_.promise().RequestStop();
  }

 private:
  Handle handle_{};
};

template <typename T>
inline constexpr bool kTaskIsAwaitable = detail::Awaitable<Task<T>&>;

static_assert(kTaskIsAwaitable<void>);

inline Task<void> detail::TaskPromise<void>::get_return_object() noexcept {
  return Task<void>(Handle::from_promise(*this));
}

template <typename AwaitableT>
  requires detail::Awaitable<AwaitableT>
Task<void> AwaitAsTask(AwaitableT awaitable) {
  co_await std::move(awaitable);
}

}  // namespace mir2::logic

#endif  // MIR2_LOGIC_TASK_H_
