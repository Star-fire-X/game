#include "common/network/fallback_controller.h"

#include <algorithm>

namespace mir2::common {

FallbackController::FallbackController()
    : FallbackController(Config{}) {
}

FallbackController::FallbackController(const Config& config)
    : config_(config), backoff_ms_(config.recovery_interval_ms) {
}

FallbackController::State FallbackController::GetState() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return state_;
}

bool FallbackController::IsKcpAllowed() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return state_ == State::kNormal;
}

bool FallbackController::IsHandshakeTimedOut(int64_t now_ms) const {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!handshake_in_progress_) {
    return false;
  }
  if (now_ms < handshake_start_ms_) {
    return true;
  }
  return (now_ms - handshake_start_ms_) >= config_.handshake_timeout_ms;
}

bool FallbackController::ShouldAttemptRecovery(int64_t now_ms) const {
  std::lock_guard<std::mutex> lock(mutex_);
  if (state_ != State::kFallback) {
    return false;
  }
  if (next_recovery_ms_ == 0) {
    return true;
  }
  return now_ms >= next_recovery_ms_;
}

void FallbackController::StartHandshake(int64_t now_ms) {
  std::lock_guard<std::mutex> lock(mutex_);
  handshake_in_progress_ = true;
  handshake_start_ms_ = now_ms;
}

void FallbackController::OnRecoveryAttempt(int64_t now_ms) {
  std::lock_guard<std::mutex> lock(mutex_);
  state_ = State::kRecovering;
  handshake_in_progress_ = true;
  handshake_start_ms_ = now_ms;
}

void FallbackController::OnKcpHandshakeSuccess() {
  std::lock_guard<std::mutex> lock(mutex_);
  state_ = State::kNormal;
  handshake_in_progress_ = false;
  handshake_start_ms_ = 0;
  next_recovery_ms_ = 0;
  backoff_ms_ = config_.recovery_interval_ms;
}

void FallbackController::OnKcpHandshakeTimeout(int64_t now_ms) {
  std::lock_guard<std::mutex> lock(mutex_);
  handshake_in_progress_ = false;
  EnterFallback(now_ms);
}

void FallbackController::OnKcpDisconnect(int64_t now_ms) {
  std::lock_guard<std::mutex> lock(mutex_);
  handshake_in_progress_ = false;
  EnterFallback(now_ms);
}

void FallbackController::Reset() {
  std::lock_guard<std::mutex> lock(mutex_);
  state_ = State::kNormal;
  handshake_in_progress_ = false;
  handshake_start_ms_ = 0;
  next_recovery_ms_ = 0;
  backoff_ms_ = config_.recovery_interval_ms;
}

void FallbackController::EnterFallback(int64_t now_ms) {
  state_ = State::kFallback;
  next_recovery_ms_ = now_ms + static_cast<int64_t>(backoff_ms_);
  backoff_ms_ = std::min(backoff_ms_ * 2, config_.max_recovery_interval_ms);
}

}  // namespace mir2::common
