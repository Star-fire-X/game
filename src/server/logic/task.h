/**
 * @file task.h
 * @brief C++20 coroutine task wrapper.
 */

#ifndef MIR2_LOGIC_TASK_H_
#define MIR2_LOGIC_TASK_H_

#include <coroutine>
#include <exception>
#include <optional>
#include <utility>

namespace mir2::logic {

template <typename T>
class Task {
 public:
  struct promise_type {
    using Handle = std::coroutine_handle<promise_type>;

    Task get_return_object() noexcept {
      return Task(Handle::from_promise(*this));
    }

    std::suspend_always initial_suspend() noexcept { return {}; }

    struct FinalAwaiter {
      bool await_ready() const noexcept { return false; }
      std::coroutine_handle<> await_suspend(Handle handle) noexcept {
        auto& promise = handle.promise();
        if (promise.detached_) {
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

    FinalAwaiter final_suspend() noexcept { return {}; }

    void return_value(T value) noexcept(std::is_nothrow_move_constructible_v<T>) {
      value_ = std::move(value);
    }

    void unhandled_exception() noexcept {
      exception_ = std::current_exception();
    }

    std::optional<T> value_;
    std::exception_ptr exception_;
    std::coroutine_handle<> continuation_;
    bool detached_ = false;
  };

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

  ~Task() {
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

  T await_resume() {
    auto& promise = handle_.promise();
    if (promise.exception_) {
      std::rethrow_exception(promise.exception_);
    }
    return std::move(promise.value_.value());
  }

  Handle Release() noexcept {
    if (handle_) {
      handle_.promise().detached_ = true;
    }
    return std::exchange(handle_, {});
  }

  bool IsValid() const noexcept { return handle_ != nullptr; }

 private:
  Handle handle_{};
};

template <>
class Task<void> {
 public:
  struct promise_type {
    using Handle = std::coroutine_handle<promise_type>;

    Task get_return_object() noexcept {
      return Task(Handle::from_promise(*this));
    }

    std::suspend_always initial_suspend() noexcept { return {}; }

    struct FinalAwaiter {
      bool await_ready() const noexcept { return false; }
      std::coroutine_handle<> await_suspend(Handle handle) noexcept {
        auto& promise = handle.promise();
        if (promise.detached_) {
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

    FinalAwaiter final_suspend() noexcept { return {}; }

    void return_void() noexcept {}

    void unhandled_exception() noexcept {
      exception_ = std::current_exception();
    }

    std::exception_ptr exception_;
    std::coroutine_handle<> continuation_;
    bool detached_ = false;
  };

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

  ~Task() {
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

  void await_resume() {
    auto& promise = handle_.promise();
    if (promise.exception_) {
      std::rethrow_exception(promise.exception_);
    }
  }

  Handle Release() noexcept {
    if (handle_) {
      handle_.promise().detached_ = true;
    }
    return std::exchange(handle_, {});
  }

  bool IsValid() const noexcept { return handle_ != nullptr; }

 private:
  Handle handle_{};
};

}  // namespace mir2::logic

#endif  // MIR2_LOGIC_TASK_H_
