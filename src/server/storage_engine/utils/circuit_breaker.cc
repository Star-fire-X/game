#include <storage_engine/utils/circuit_breaker.h>

#include <algorithm>

#include <spdlog/spdlog.h>

namespace mir2::storage_engine::utils {

CircuitBreaker::CircuitBreaker() : CircuitBreaker(Config{}) {}

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

    ++total_requests_;
    State current_state = state_.load(std::memory_order_acquire);

    if (current_state == State::HALF_OPEN) {
        ++half_open_successes_;
        if (half_open_successes_ >= config_.half_open_success_threshold) {
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
}

void CircuitBreaker::OnFailure() {
    std::unique_lock<std::shared_mutex> lock(stats_mutex_);

    State current_state = state_.load(std::memory_order_acquire);

    ++total_requests_;
    ++total_failures_;
    ++consecutive_failures_;

    if (current_state == State::HALF_OPEN) {
        state_.store(State::OPEN, std::memory_order_release);
        open_time_ = std::chrono::steady_clock::now();
        half_open_successes_ = 0;
        half_open_requests_ = 0;

        auto logger = spdlog::get("mir2");
        if (logger) {
            logger->warn("CircuitBreaker: HALF_OPEN -> OPEN (failed during recovery)");
        }
    } else if (current_state == State::CLOSED) {
        bool should_open = false;

        if (consecutive_failures_ >= config_.failure_threshold) {
            should_open = true;
        }

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
                    "failure_rate={:.2f}%)",
                    consecutive_failures_, failure_rate * 100.0);
            }
        }
    }
}

CircuitBreaker::State CircuitBreaker::GetState() const {
    State current_state = state_.load(std::memory_order_acquire);
    if (current_state == State::OPEN) {
        TryTransitionToHalfOpen();
        current_state = state_.load(std::memory_order_acquire);
    }
    return current_state;
}

bool CircuitBreaker::IsOpen() const {
    // Fast path for the common CLOSED/HALF_OPEN states.
    if (state_.load(std::memory_order_acquire) != State::OPEN) {
        return false;
    }

    // OPEN is less frequent; use exclusive lock directly to avoid
    // shared->unique upgrade races on the hot path.
    TryTransitionToHalfOpen();
    return state_.load(std::memory_order_acquire) == State::OPEN;
}

void CircuitBreaker::TryTransitionToHalfOpen() const {
    if (state_.load(std::memory_order_acquire) != State::OPEN) {
        return;
    }

    std::unique_lock<std::shared_mutex> write_lock(stats_mutex_);

    if (state_.load(std::memory_order_acquire) != State::OPEN) {
        return;
    }

    const auto now = std::chrono::steady_clock::now();
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        now - open_time_).count();
    if (elapsed < static_cast<int64_t>(config_.open_timeout_ms)) {
        return;
    }

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

CircuitBreaker::Stats CircuitBreaker::GetStats() const {
    std::shared_lock<std::shared_mutex> lock(stats_mutex_);

    State current_state = state_.load(std::memory_order_acquire);

    double failure_rate = total_requests_ > 0
                              ? static_cast<double>(total_failures_) / total_requests_
                              : 0.0;

    int64_t time_until_retry_ms = 0;
    if (current_state == State::OPEN) {
        auto now = std::chrono::steady_clock::now();
        auto elapsed =
            std::chrono::duration_cast<std::chrono::milliseconds>(now - open_time_).count();
        time_until_retry_ms = std::max<int64_t>(
            0, static_cast<int64_t>(config_.open_timeout_ms) - elapsed);
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

uint32_t CircuitBreaker::FailureCount() const {
    std::shared_lock<std::shared_mutex> lock(stats_mutex_);
    return total_failures_;
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

}  // namespace mir2::storage_engine::utils
