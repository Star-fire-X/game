// =============================================================================
// Legend2 帧计时器实现 (Frame Timer Implementation)
// =============================================================================

#include "core/timer.h"
#include <thread>

namespace mir2::core {

FrameTimer::FrameTimer(int target_fps)
    : target_fps_(target_fps)
    , target_frame_time_(target_fps > 0 ? 1.0f / target_fps : 0.0f)
{
    start_time_ = std::chrono::high_resolution_clock::now();
    frame_start_ = start_time_;
    last_fps_update_ = start_time_;
}

void FrameTimer::begin_frame() {
    frame_start_ = std::chrono::high_resolution_clock::now();
}

void FrameTimer::end_frame() {
    auto frame_end = std::chrono::high_resolution_clock::now();

    // 计算帧间隔时间
    std::chrono::duration<float> frame_duration = frame_end - frame_start_;
    delta_time_ = frame_duration.count();

    // 帧率限制（当设置了目标FPS且垂直同步未生效时）
    if (target_fps_ > 0 && delta_time_ < target_frame_time_) {
        float sleep_time = target_frame_time_ - delta_time_;
        // 休眠90%的等待时间，节省CPU资源
        std::this_thread::sleep_for(std::chrono::duration<float>(sleep_time * 0.9f));

        // 忙等待剩余时间，确保精确的帧率控制
        while (true) {
            auto now = std::chrono::high_resolution_clock::now();
            std::chrono::duration<float> elapsed = now - frame_start_;
            if (elapsed.count() >= target_frame_time_) break;
        }

        // 重新计算实际的帧间隔时间
        frame_end = std::chrono::high_resolution_clock::now();
        frame_duration = frame_end - frame_start_;
        delta_time_ = frame_duration.count();
    }

    frame_count_++;
    fps_frame_count_++;

    // 每秒更新一次FPS统计
    std::chrono::duration<float> fps_elapsed = frame_end - last_fps_update_;
    if (fps_elapsed.count() >= 1.0f) {
        fps_ = fps_frame_count_ / fps_elapsed.count();
        fps_frame_count_ = 0;
        last_fps_update_ = frame_end;
    }
}

void FrameTimer::set_target_fps(int fps) {
    target_fps_ = fps;
    target_frame_time_ = fps > 0 ? 1.0f / fps : 0.0f;
}

float FrameTimer::get_elapsed_time() const {
    auto now = std::chrono::high_resolution_clock::now();
    std::chrono::duration<float> elapsed = now - start_time_;
    return elapsed.count();
}

} // namespace mir2::core
