#include "core/timer.h"

#include <thread>

namespace mir2::core {

TickTimer::TickTimer(int tick_interval_ms)
    : tick_interval_ms_(tick_interval_ms),
      target_tick_sec_(tick_interval_ms > 0 ? tick_interval_ms / 1000.0f : 0.0f) {
  tick_start_ = std::chrono::high_resolution_clock::now();
  last_rate_update_ = tick_start_;
}

void TickTimer::BeginTick() {
  tick_start_ = std::chrono::high_resolution_clock::now();
}

void TickTimer::EndTick() {
  const auto tick_end = std::chrono::high_resolution_clock::now();
  std::chrono::duration<float> tick_duration = tick_end - tick_start_;
  delta_time_sec_ = tick_duration.count();

  if (target_tick_sec_ > 0.0f && delta_time_sec_ < target_tick_sec_) {
    const float sleep_time = target_tick_sec_ - delta_time_sec_;
    std::this_thread::sleep_for(std::chrono::duration<float>(sleep_time * 0.9f));

    while (true) {
      const auto now = std::chrono::high_resolution_clock::now();
      std::chrono::duration<float> elapsed = now - tick_start_;
      if (elapsed.count() >= target_tick_sec_) {
        break;
      }
    }
  }

  const auto tick_done = std::chrono::high_resolution_clock::now();
  std::chrono::duration<float> total_duration = tick_done - tick_start_;
  delta_time_sec_ = total_duration.count();

  tick_count_++;
  rate_tick_count_++;

  std::chrono::duration<float> rate_elapsed = tick_done - last_rate_update_;
  if (rate_elapsed.count() >= 1.0f) {
    actual_tick_rate_ = rate_tick_count_ / rate_elapsed.count();
    rate_tick_count_ = 0;
    last_rate_update_ = tick_done;
  }
}

}  // namespace mir2::core
