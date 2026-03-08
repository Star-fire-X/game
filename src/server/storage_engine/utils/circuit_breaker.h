#ifndef MIR2_STORAGE_ENGINE_UTILS_CIRCUIT_BREAKER_H_
#define MIR2_STORAGE_ENGINE_UTILS_CIRCUIT_BREAKER_H_

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <optional>
#include <shared_mutex>
#include <type_traits>
#include <utility>

namespace mir2::storage_engine::utils {

// Circuit Breaker implementation.
// State machine: CLOSED -> OPEN -> HALF_OPEN -> CLOSED
class CircuitBreaker {
public:
    enum class State : uint8_t {
        CLOSED = 0,
        OPEN = 1,
        HALF_OPEN = 2
    };

    struct Config {
        uint32_t failure_threshold = 10;
        double failure_rate_threshold = 0.5;
        uint32_t min_requests = 20;
        uint32_t open_timeout_ms = 30000;
        uint32_t half_open_max_requests = 5;
        uint32_t half_open_success_threshold = 3;
    };

    CircuitBreaker();
    explicit CircuitBreaker(const Config& config);
    ~CircuitBreaker();

    template <typename Func,
              typename ReturnType = std::invoke_result_t<Func>,
              std::enable_if_t<!std::is_void_v<ReturnType>, int> = 0>
    std::optional<ReturnType> Execute(Func&& func) {
        if (IsOpen()) {
            return std::nullopt;
        }

        try {
            auto result = std::forward<Func>(func)();
            OnSuccess();
            return result;
        } catch (const std::exception&) {
            OnFailure();
            return std::nullopt;
        }
    }

    template <typename Func,
              typename ReturnType = std::invoke_result_t<Func>,
              std::enable_if_t<std::is_void_v<ReturnType>, int> = 0>
    bool Execute(Func&& func) {
        if (IsOpen()) {
            return false;
        }

        try {
            std::forward<Func>(func)();
            OnSuccess();
            return true;
        } catch (const std::exception&) {
            OnFailure();
            return false;
        }
    }

    void OnSuccess();
    void OnFailure();

    State GetState() const;
    bool IsOpen() const;

    struct Stats {
        State state;
        uint32_t total_requests;
        uint32_t total_failures;
        double failure_rate;
        uint32_t consecutive_failures;
        uint32_t half_open_successes;
        int64_t time_until_retry_ms;
    };

    Stats GetStats() const;

    uint32_t FailureCount() const;

    void Reset();

private:
    void TryTransitionToHalfOpen() const;

    Config config_;
    mutable std::atomic<State> state_{State::CLOSED};
    mutable std::shared_mutex stats_mutex_;
    uint32_t total_requests_ = 0;
    uint32_t total_failures_ = 0;
    uint32_t consecutive_failures_ = 0;
    mutable uint32_t half_open_successes_ = 0;
    mutable uint32_t half_open_requests_ = 0;
    std::chrono::steady_clock::time_point open_time_;
};

}  // namespace mir2::storage_engine::utils

#endif  // MIR2_STORAGE_ENGINE_UTILS_CIRCUIT_BREAKER_H_
