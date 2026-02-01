/**
 * @file position_interpolator.h
 * @brief Legend2 位置插值器
 * 
 * 本文件包含客户端渲染用的平滑位置插值功能，包括：
 * - 浮点位置结构
 * - 线性插值和平滑插值
 * - 位置插值器（用于平滑移动）
 * - 实体插值器（用于网络同步）
 */

#ifndef LEGEND2_POSITION_INTERPOLATOR_H
#define LEGEND2_POSITION_INTERPOLATOR_H

#include "common/types.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace legend2 {

using mir2::common::Position;

/**
 * @brief 浮点位置（用于平滑插值）
 */
struct PositionF {
    float x = 0.0f;  ///< X坐标
    float y = 0.0f;  ///< Y坐标
    
    PositionF() = default;
    PositionF(float px, float py) : x(px), y(py) {}
    explicit PositionF(const Position& p) : x(static_cast<float>(p.x)), y(static_cast<float>(p.y)) {}
    
    /// 转换为整数位置（瓦片坐标）
    Position to_position() const {
        if (std::isnan(x) || std::isnan(y) || std::isinf(x) || std::isinf(y)) {
            return Position{0, 0};
        }

        const float max_coord = static_cast<float>(std::numeric_limits<int>::max());
        const float min_coord = static_cast<float>(std::numeric_limits<int>::min());
        const float safe_x = std::clamp(x, min_coord, max_coord);
        const float safe_y = std::clamp(y, min_coord, max_coord);

        return Position{
            static_cast<int>(std::round(safe_x)),
            static_cast<int>(std::round(safe_y))
        };
    }
    
    bool operator==(const PositionF& other) const {
        return std::abs(x - other.x) < 0.0001f && std::abs(y - other.y) < 0.0001f;
    }
    
    bool operator!=(const PositionF& other) const {
        return !(*this == other);
    }
    
    /// 计算到另一个位置的距离
    float distance(const PositionF& other) const {
        float dx = x - other.x;
        float dy = y - other.y;
        return std::sqrt(dx * dx + dy * dy);
    }
    
    /// 计算距离平方（更快，无开方）
    float distance_squared(const PositionF& other) const {
        float dx = x - other.x;
        float dy = y - other.y;
        return dx * dx + dy * dy;
    }
};

/**
 * @brief 两个值之间的线性插值
 * @param a 起始值
 * @param b 结束值
 * @param t 插值因子 [0, 1]
 * @return 插值结果
 */
inline float lerp(float a, float b, float t) {
    t = std::max(0.0f, std::min(1.0f, t));
    return a + (b - a) * t;
}

/**
 * @brief 两个位置之间的线性插值
 * @param start 起始位置
 * @param end 结束位置
 * @param t 插值因子 [0, 1]
 * @return 插值后的位置
 */
inline PositionF lerp(const PositionF& start, const PositionF& end, float t) {
    return PositionF{
        lerp(start.x, end.x, t),
        lerp(start.y, end.y, t)
    };
}

/**
 * @brief 两个整数位置之间的线性插值
 * @param start 起始位置
 * @param end 结束位置
 * @param t 插值因子 [0, 1]
 * @return 插值后的位置（浮点）
 */
inline PositionF lerp(const Position& start, const Position& end, float t) {
    return lerp(PositionF(start), PositionF(end), t);
}

/**
 * @brief 平滑步进插值（缓入缓出）
 * @param t 输入值 [0, 1]
 * @return 平滑后的值 [0, 1]
 */
inline float smoothstep(float t) {
    t = std::max(0.0f, std::min(1.0f, t));
    return t * t * (3.0f - 2.0f * t);
}

/**
 * @brief 更平滑的步进插值（Ken Perlin改进版）
 * @param t 输入值 [0, 1]
 * @return 平滑后的值 [0, 1]
 */
inline float smootherstep(float t) {
    t = std::max(0.0f, std::min(1.0f, t));
    return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
}

/**
 * @brief 位置插值器（用于平滑实体移动）
 */
class PositionInterpolator {
public:
    /// 默认构造函数
    PositionInterpolator() = default;
    
    /// 带初始位置的构造函数
    explicit PositionInterpolator(const Position& initial)
        : current_(initial), target_(initial), start_(initial) {}
    
    /**
     * @brief 设置插值目标位置
     * @param target 新目标位置
     * @param duration_ms 插值持续时间（毫秒）
     */
    void set_target(const Position& target, uint32_t duration_ms = 200) {
        start_ = current_;
        target_ = PositionF(target);
        duration_ = static_cast<float>(duration_ms);
        elapsed_ = 0.0f;
        interpolating_ = true;
    }
    
    /// 设置目标位置（浮点版本）
    void set_target(const PositionF& target, uint32_t duration_ms = 200) {
        start_ = current_;
        target_ = target;
        duration_ = static_cast<float>(duration_ms);
        elapsed_ = 0.0f;
        interpolating_ = true;
    }
    
    /// 立即设置位置（无插值）
    void set_immediate(const Position& pos) {
        current_ = PositionF(pos);
        target_ = current_;
        start_ = current_;
        interpolating_ = false;
        elapsed_ = 0.0f;
    }
    
    /// 立即设置位置（浮点版本）
    void set_immediate(const PositionF& pos) {
        current_ = pos;
        target_ = pos;
        start_ = pos;
        interpolating_ = false;
        elapsed_ = 0.0f;
    }
    
    /**
     * @brief 更新插值
     * @param delta_ms 自上次更新以来的时间（毫秒）
     */
    void update(float delta_ms) {
        if (!interpolating_) {
            return;
        }
        
        elapsed_ += delta_ms;
        
        if (elapsed_ >= duration_) {
            // 插值完成
            current_ = target_;
            interpolating_ = false;
        } else {
            // 计算插值因子
            float t = elapsed_ / duration_;
            
            // 应用平滑
            if (use_smoothing_) {
                t = smoothstep(t);
            }
            
            // 插值位置
            current_ = lerp(start_, target_, t);
        }
    }
    
    /// 获取当前插值位置
    PositionF get_position() const {
        return current_;
    }
    
    /// 获取当前位置（整数，瓦片坐标）
    Position get_tile_position() const {
        return current_.to_position();
    }
    
    /// 获取目标位置
    PositionF get_target() const {
        return target_;
    }
    
    /// 检查是否正在插值
    bool is_interpolating() const {
        return interpolating_;
    }
    
    /// 获取插值进度 [0, 1]
    float get_progress() const {
        if (!interpolating_ || duration_ <= 0.0f) {
            return 1.0f;
        }
        return std::min(1.0f, elapsed_ / duration_);
    }
    
    /// 启用/禁用平滑
    void set_smoothing(bool enable) {
        use_smoothing_ = enable;
    }
    
    /// 检查是否启用平滑
    bool is_smoothing_enabled() const {
        return use_smoothing_;
    }
    
    /// 获取剩余插值时间（毫秒）
    float get_remaining_time() const {
        if (!interpolating_) {
            return 0.0f;
        }
        return std::max(0.0f, duration_ - elapsed_);
    }
    
private:
    PositionF current_;          ///< 当前位置
    PositionF target_;           ///< 目标位置
    PositionF start_;            ///< 起始位置
    float duration_ = 0.0f;      ///< 插值持续时间
    float elapsed_ = 0.0f;       ///< 已过时间
    bool interpolating_ = false; ///< 是否正在插值
    bool use_smoothing_ = true;  ///< 是否使用平滑
};

/**
 * @brief 实体状态插值器（用于网络同步）
 * 
 * 处理服务器状态更新的位置插值
 */
class EntityInterpolator {
public:
    /// 构造函数
    EntityInterpolator() = default;
    
    /**
     * @brief 接收服务器状态更新
     * @param server_pos 服务器发来的位置
     * @param server_time 服务器时间戳
     */
    void receive_state(const Position& server_pos, uint32_t server_time) {
        // 保存之前的状态
        prev_pos_ = target_pos_;
        prev_time_ = target_time_;
        
        // 更新目标状态
        target_pos_ = PositionF(server_pos);
        target_time_ = server_time;
        
        // 根据更新间隔计算插值持续时间
        if (prev_time_ > 0 && server_time > prev_time_) {
            interpolation_duration_ = static_cast<float>(server_time - prev_time_);
        }
        
        // 从当前渲染位置开始插值
        start_pos_ = current_pos_;
        elapsed_ = 0.0f;
        has_target_ = true;
    }
    
    /**
     * @brief 更新插值
     * @param delta_ms 自上次更新以来的时间
     */
    void update(float delta_ms) {
        if (!has_target_) {
            return;
        }
        
        elapsed_ += delta_ms;
        
        if (interpolation_duration_ > 0.0f) {
            float t = std::min(1.0f, elapsed_ / interpolation_duration_);
            current_pos_ = lerp(start_pos_, target_pos_, t);
        } else {
            current_pos_ = target_pos_;
        }
    }
    
    /// 获取当前插值位置
    PositionF get_position() const {
        return current_pos_;
    }
    
    /// 获取位置（瓦片坐标）
    Position get_tile_position() const {
        return current_pos_.to_position();
    }
    
    /// 设置初始位置
    void set_initial(const Position& pos) {
        current_pos_ = PositionF(pos);
        target_pos_ = current_pos_;
        start_pos_ = current_pos_;
        has_target_ = false;
    }
    
    /// 设置插值延迟（用于延迟补偿）
    void set_interpolation_delay(float delay_ms) {
        interpolation_delay_ = delay_ms;
    }
    
private:
    PositionF current_pos_;              ///< 当前位置
    PositionF target_pos_;               ///< 目标位置
    PositionF start_pos_;                ///< 起始位置
    PositionF prev_pos_;                 ///< 之前的位置
    
    uint32_t target_time_ = 0;           ///< 目标时间戳
    uint32_t prev_time_ = 0;             ///< 之前的时间戳
    
    float elapsed_ = 0.0f;               ///< 已过时间
    float interpolation_duration_ = 100.0f;  ///< 插值持续时间（默认100ms）
    float interpolation_delay_ = 0.0f;   ///< 插值延迟
    
    bool has_target_ = false;            ///< 是否有目标
};

} // namespace legend2

#endif // LEGEND2_POSITION_INTERPOLATOR_H
