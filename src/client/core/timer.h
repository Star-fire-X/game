// =============================================================================
// Legend2 帧计时器 (Frame Timer)
//
// 功能说明:
//   - 帧间隔时间计算
//   - FPS统计和限制
//   - 高精度时间测量
//
// 从 game_client.h 抽取的独立模块
// =============================================================================

#ifndef LEGEND2_CORE_TIMER_H
#define LEGEND2_CORE_TIMER_H

#include <chrono>
#include <cstdint>
#include "common/types.h"

namespace mir2::core {

// 引入公共类型定义
using namespace mir2::common;

/// 帧计时和帧率控制
/// 管理帧间隔时间和FPS限制
class FrameTimer {
public:
    /// 构造函数
    /// @param target_fps 目标帧率，0表示不限制
    explicit FrameTimer(int target_fps = constants::TARGET_FPS);

    /// 开始计时新的一帧
    void begin_frame();

    /// 结束帧并等待以维持目标FPS
    void end_frame();

    /// 获取帧间隔时间(秒)
    float get_delta_time() const { return delta_time_; }

    /// 获取当前FPS
    float get_fps() const { return fps_; }

    /// 获取目标FPS
    int get_target_fps() const { return target_fps_; }

    /// 设置目标FPS(0表示不限制)
    void set_target_fps(int fps);

    /// 获取总运行时间(秒)
    float get_elapsed_time() const;

    /// 获取帧计数
    uint64_t get_frame_count() const { return frame_count_; }

private:
    int target_fps_;              // 目标FPS
    float target_frame_time_;     // 目标帧时间(秒)
    float delta_time_ = 0.0f;     // 帧间隔时间
    float fps_ = 0.0f;            // 当前FPS
    uint64_t frame_count_ = 0;    // 总帧数

    std::chrono::high_resolution_clock::time_point start_time_;      // 启动时间
    std::chrono::high_resolution_clock::time_point frame_start_;     // 帧开始时间
    std::chrono::high_resolution_clock::time_point last_fps_update_; // 上次FPS更新时间
    int fps_frame_count_ = 0;     // FPS计算用帧计数
};

} // namespace mir2::core

#endif // LEGEND2_CORE_TIMER_H
