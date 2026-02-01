/**
 * @file timer.h
 * @brief 服务器Tick计时器
 */

#ifndef MIR2_CORE_TIMER_H
#define MIR2_CORE_TIMER_H

#include <chrono>
#include <cstdint>

namespace mir2::core {

/**
 * @brief Tick计时器
 *
 * 用于控制固定Tick间隔，提供delta时间与实际Tick统计。
 */
class TickTimer {
 public:
  /**
   * @brief 构造函数
   * @param tick_interval_ms Tick间隔（毫秒）
   */
  explicit TickTimer(int tick_interval_ms);

  /**
   * @brief 开始新一帧Tick计时
   */
  void BeginTick();

  /**
   * @brief 结束本帧Tick并进行节流
   */
  void EndTick();

  /**
   * @brief 获取本帧delta时间（秒）
   */
  float GetDeltaTime() const { return delta_time_sec_; }

  /**
   * @brief 获取实际Tick频率
   */
  float GetActualTickRate() const { return actual_tick_rate_; }

  /**
   * @brief 获取Tick计数
   */
  uint64_t GetTickCount() const { return tick_count_; }

 private:
  int tick_interval_ms_;
  float target_tick_sec_;
  float delta_time_sec_ = 0.0f;
  float actual_tick_rate_ = 0.0f;
  uint64_t tick_count_ = 0;

  std::chrono::high_resolution_clock::time_point tick_start_;
  std::chrono::high_resolution_clock::time_point last_rate_update_;
  uint64_t rate_tick_count_ = 0;
};

}  // namespace mir2::core

#endif  // MIR2_CORE_TIMER_H
