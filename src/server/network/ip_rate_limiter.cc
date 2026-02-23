#include "network/ip_rate_limiter.h"

namespace mir2::network {

IpRateLimiter::IpRateLimiter() : config_({}) {}
IpRateLimiter::IpRateLimiter(Config config) : config_(config) {}

bool IpRateLimiter::Allow(uint32_t ip, int64_t now_ms) {
  std::lock_guard<std::mutex> lock(mutex_);

  const int64_t window_ms = config_.window_ms > 0 ? config_.window_ms : 1;
  auto& bucket = buckets_[ip];
  if (!bucket.initialized) {
    bucket.initialized = true;
    bucket.window_start_ms = now_ms;
  }

  int64_t elapsed = now_ms - bucket.window_start_ms;
  if (elapsed < 0) {
    bucket.window_start_ms = now_ms;
    bucket.curr_count = 0;
    bucket.prev_count = 0;
    elapsed = 0;
  }

  if (elapsed >= window_ms) {
    const int64_t windows_passed = elapsed / window_ms;
    bucket.window_start_ms += windows_passed * window_ms;
    if (windows_passed == 1) {
      bucket.prev_count = bucket.curr_count;
    } else {
      bucket.prev_count = 0;
    }
    bucket.curr_count = 0;
    elapsed = now_ms - bucket.window_start_ms;
  }

  // Weighted sliding window: effective = current bucket + previous bucket weight.
  const double weight_prev = static_cast<double>(window_ms - elapsed) /
                             static_cast<double>(window_ms);
  const double effective = static_cast<double>(bucket.curr_count) +
                           static_cast<double>(bucket.prev_count) * weight_prev;
  if (effective >= static_cast<double>(config_.max_packets_per_sec)) {
    bucket.last_seen_ms = now_ms;
    return false;
  }

  ++bucket.curr_count;
  bucket.last_seen_ms = now_ms;

  return true;
}

void IpRateLimiter::Cleanup(int64_t now_ms) {
  std::lock_guard<std::mutex> lock(mutex_);
  last_cleanup_ms_ = now_ms;
  for (auto it = buckets_.begin(); it != buckets_.end();) {
    if (now_ms - it->second.last_seen_ms >= config_.idle_expire_ms) {
      it = buckets_.erase(it);
    } else {
      ++it;
    }
  }
}

}  // namespace mir2::network
