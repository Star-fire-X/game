#include "circuit_breaker.h"

#include <spdlog/spdlog.h>

namespace mir2::cache {

CircuitBreaker::CircuitBreaker(const Config& config) : config_(config) {
    auto logger = spdlog::get("mir2");
    if (logger) {
        logger->info(
            "CircuitBreaker initialized with failure_threshold={}, "
            "failure_rate_threshold={}, open_timeout_ms={}",
            config_.failure_threshold, config_.failure_rate_threshold,
            config_.open_timeout_ms);
    }
}

CircuitBreaker::~CircuitBreaker() = default;

void CircuitBreaker::OnSuccess() {
    std::unique_lock<std::shared_mutex> lock(stats_mutex_);

    State current_state = state_.load(std::memory_order_acquire);

    if (current_state == State::HALF_OPEN) {
        ++half_open_successes_;
        if (half_open_successes_ >= config_.half_open_success_threshold) {
            // 从 HALF_OPEN 恢复到 CLOSED
            state_.store(State::CLOSED, std::memory_order_release);
            consecutive_failures_ = 0;
            half_open_successes_ = 0;
            half_open_requests_ = 0;

            auto logger = spdlog::get("mir2");
            if (logger) {
                logger->info(
                    "CircuitBreaker: HALF_OPEN -> CLOSED (recovered after {} "
                    "successes)",
                    config_.half_open_success_threshold);
            }
        }
    } else if (current_state == State::CLOSED) {
        consecutive_failures_ = 0;
    }
    // 在 OPEN 状态下不更新任何信息
}

void CircuitBreaker::OnFailure() {
    std::unique_lock<std::shared_mutex> lock(stats_mutex_);

    State current_state = state_.load(std::memory_order_acquire);

    ++total_requests_;
    ++total_failures_;
    ++consecutive_failures_;

    if (current_state == State::HALF_OPEN) {
        // HALF_OPEN 状态下任何失败都回到 OPEN
        state_.store(State::OPEN, std::memory_order_release);
        open_time_ = std::chrono::steady_clock::now();
        half_open_successes_ = 0;
        half_open_requests_ = 0;

        auto logger = spdlog::get("mir2");
        if (logger) {
            logger->warn("CircuitBreaker: HALF_OPEN -> OPEN (failed during recovery)");
        }
    } else if (current_state == State::CLOSED) {
        // 检查是否应该从 CLOSED 转移到 OPEN
        bool should_open = false;

        // 条件 1: 连续失败次数
        if (consecutive_failures_ >= config_.failure_threshold) {
            should_open = true;
        }

        // 条件 2: 失败率超过阈值
        if (total_requests_ >= config_.min_requests) {
            double failure_rate = static_cast<double>(total_failures_) / total_requests_;
            if (failure_rate > config_.failure_rate_threshold) {
                should_open = true;
            }
        }

        if (should_open) {
            state_.store(State::OPEN, std::memory_order_release);
            open_time_ = std::chrono::steady_clock::now();

            auto logger = spdlog::get("mir2");
            if (logger) {
                double failure_rate = total_requests_ > 0
                                          ? static_cast<double>(total_failures_) /
                                                total_requests_
                                          : 0.0;
                logger->warn(
                    "CircuitBreaker: CLOSED -> OPEN (consecutive_failures={}, "
                    "failure_rate={:.2%})",
                    consecutive_failures_, failure_rate);
            }
        }
    }
}

CircuitBreaker::State CircuitBreaker::GetState() const {
    std::shared_lock<std::shared_mutex> lock(stats_mutex_);

    State current_state = state_.load(std::memory_order_acquire);

    // 如果处于 OPEN 状态，检查是否应该转移到 HALF_OPEN
    if (current_state == State::OPEN) {
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - open_time_).count();

        if (elapsed >= static_cast<int64_t>(config_.open_timeout_ms)) {
            // 释放读锁，获取写锁进行状态转移
            lock.unlock();
            std::unique_lock<std::shared_mutex> write_lock(stats_mutex_);

            // 再次检查状态（Double-check）
            if (state_.load(std::memory_order_acquire) == State::OPEN) {
                state_.store(State::HALF_OPEN, std::memory_order_release);
                half_open_successes_ = 0;
                half_open_requests_ = 0;

                auto logger = spdlog::get("mir2");
                if (logger) {
                    logger->info(
                        "CircuitBreaker: OPEN -> HALF_OPEN (timeout after {}ms)",
                        config_.open_timeout_ms);
                }
            }

            return State::HALF_OPEN;
        }
    }

    return current_state;
}

bool CircuitBreaker::IsOpen() const {
    return GetState() == State::OPEN;
}

CircuitBreaker::Stats CircuitBreaker::GetStats() const {
    std::shared_lock<std::shared_mutex> lock(stats_mutex_);

    State current_state = GetState();

    double failure_rate = total_requests_ > 0
                              ? static_cast<double>(total_failures_) / total_requests_
                              : 0.0;

    int64_t time_until_retry_ms = 0;
    if (current_state == State::OPEN) {
        auto now = std::chrono::steady_clock::now();
        auto elapsed =
            std::chrono::duration_cast<std::chrono::milliseconds>(now - open_time_).count();
        time_until_retry_ms = std::max(0LL, static_cast<int64_t>(config_.open_timeout_ms) - elapsed);
    }

    return Stats{
        .state = current_state,
        .total_requests = total_requests_,
        .total_failures = total_failures_,
        .failure_rate = failure_rate,
        .consecutive_failures = consecutive_failures_,
        .half_open_successes = half_open_successes_,
        .time_until_retry_ms = time_until_retry_ms,
    };
}

void CircuitBreaker::Reset() {
    std::unique_lock<std::shared_mutex> lock(stats_mutex_);
    state_.store(State::CLOSED, std::memory_order_release);
    total_requests_ = 0;
    total_failures_ = 0;
    consecutive_failures_ = 0;
    half_open_successes_ = 0;
    half_open_requests_ = 0;
}

}  // namespace mir2::cache
