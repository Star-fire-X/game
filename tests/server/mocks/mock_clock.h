/**
 * @file mock_clock.h
 * @brief Thread-safe controllable steady_clock mock for tests
 */

#ifndef MIR2_TEST_MOCK_CLOCK_H
#define MIR2_TEST_MOCK_CLOCK_H

#include <chrono>
#include <mutex>

namespace mir2::persistence::test {

class MockClock {
 public:
  using TimePoint = std::chrono::steady_clock::time_point;
  using Duration = std::chrono::steady_clock::duration;

  MockClock() : current_time_(std::chrono::steady_clock::now()) {}

  TimePoint Now() const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!frozen_) {
      current_time_ = std::chrono::steady_clock::now();
    }
    return current_time_;
  }

  void Advance(Duration duration) {
    std::lock_guard<std::mutex> lock(mutex_);
    current_time_ += duration;
  }

  void SetTime(TimePoint time) {
    std::lock_guard<std::mutex> lock(mutex_);
    current_time_ = time;
  }

  void Freeze() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!frozen_) {
      current_time_ = std::chrono::steady_clock::now();
      frozen_ = true;
    }
  }

  void Unfreeze() {
    std::lock_guard<std::mutex> lock(mutex_);
    frozen_ = false;
    current_time_ = std::chrono::steady_clock::now();
  }

  void Reset() {
    std::lock_guard<std::mutex> lock(mutex_);
    current_time_ = std::chrono::steady_clock::now();
    frozen_ = false;
  }

 private:
  mutable std::mutex mutex_;
  mutable TimePoint current_time_;
  bool frozen_ = false;
};

}  // namespace mir2::persistence::test

#endif  // MIR2_TEST_MOCK_CLOCK_H