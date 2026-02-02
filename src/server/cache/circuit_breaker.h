#ifndef MIR2_SERVER_CACHE_CIRCUIT_BREAKER_H_
#define MIR2_SERVER_CACHE_CIRCUIT_BREAKER_H_

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <optional>
#include <shared_mutex>

namespace mir2::cache {

// 熔断器 (Circuit Breaker) 实现
// 用于检测和隔离故障，防止级联失败
// 状态机: CLOSED -> OPEN -> HALF_OPEN -> CLOSED
class CircuitBreaker {
public:
    enum class State : uint8_t {
        CLOSED = 0,      // 正常状态，允许请求
        OPEN = 1,        // 熔断状态，拒绝请求
        HALF_OPEN = 2    // 恢复测试状态，允许部分请求
    };

    // 熔断器配置结构体
    struct Config {
        // 触发 OPEN 的连续失败次数阈值
        uint32_t failure_threshold = 10;

        // 失败率阈值 (0.0 ~ 1.0)
        double failure_rate_threshold = 0.5;

        // 计算失败率的最小请求数
        uint32_t min_requests = 20;

        // OPEN 状态持续时间（毫秒），超时后进入 HALF_OPEN
        uint32_t open_timeout_ms = 30000;

        // HALF_OPEN 状态最多允许的请求数
        uint32_t half_open_max_requests = 5;

        // HALF_OPEN 状态成功后回到 CLOSED 的成功次数阈值
        uint32_t half_open_success_threshold = 3;
    };

    // 构造函数
    explicit CircuitBreaker(const Config& config = Config());

    // 析构函数
    ~CircuitBreaker();

    // 执行操作，带熔断保护
    // 返回操作结果，若熔断器 OPEN 则返回 std::nullopt
    template <typename Func>
    std::optional<typename std::invoke_result_t<Func>> Execute(Func&& func) {
        using ReturnType = typename std::invoke_result_t<Func>;

        // 检查是否应该拒绝请求
        if (IsOpen()) {
            return std::nullopt;
        }

        try {
            auto result = std::forward<Func>(func)();
            OnSuccess();
            return result;
        } catch (const std::exception& e) {
            OnFailure();
            return std::nullopt;
        }
    }

    // 特化版本：void 返回类型
    template <typename Func>
    bool Execute(Func&& func,
                 std::enable_if_t<std::is_void_v<std::invoke_result_t<Func>>> * =
                     nullptr) {
        if (IsOpen()) {
            return false;
        }

        try {
            std::forward<Func>(func)();
            OnSuccess();
            return true;
        } catch (const std::exception& e) {
            OnFailure();
            return false;
        }
    }

    // 记录成功操作
    void OnSuccess();

    // 记录失败操作
    void OnFailure();

    // 获取当前状态
    State GetState() const;

    // 检查是否处于 OPEN 状态
    bool IsOpen() const;

    // 获取统计信息
    struct Stats {
        State state;
        uint32_t total_requests;       // 总请求数
        uint32_t total_failures;       // 总失败数
        double failure_rate;           // 失败率
        uint32_t consecutive_failures; // 连续失败次数
        uint32_t half_open_successes;  // HALF_OPEN 状态成功次数
        int64_t time_until_retry_ms;   // 距离 HALF_OPEN 还需时间（毫秒）
    };

    Stats GetStats() const;

    // 手动重置熔断器（用于测试）
    void Reset();

private:
    // 尝试从 HALF_OPEN 转移到 CLOSED
    void TryTransitionToClosed();

    // 尝试从 OPEN 转移到 HALF_OPEN
    void TryTransitionToHalfOpen();

    // 配置信息
    Config config_;

    // 当前状态
    std::atomic<State> state_{State::CLOSED};

    // 统计信息（保护锁）
    mutable std::shared_mutex stats_mutex_;

    // 总请求数
    uint32_t total_requests_ = 0;

    // 总失败数
    uint32_t total_failures_ = 0;

    // 连续失败次数
    uint32_t consecutive_failures_ = 0;

    // HALF_OPEN 状态的成功次数
    uint32_t half_open_successes_ = 0;

    // HALF_OPEN 状态的请求次数
    uint32_t half_open_requests_ = 0;

    // OPEN 状态的时间戳
    std::chrono::steady_clock::time_point open_time_;
};

}  // namespace mir2::cache

#endif  // MIR2_SERVER_CACHE_CIRCUIT_BREAKER_H_
