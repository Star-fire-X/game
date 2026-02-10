#include "network/ip_rate_limiter.h"

namespace mir2::network {

IpRateLimiter::IpRateLimiter() : config_({}) {}
IpRateLimiter::IpRateLimiter(Config config) : config_(config) {}

bool IpRateLimiter::Allow(uint32_t ip, int64_t now_ms) {
  std::lock_guard<std::mutex> lock(mutex_);

  auto& bucket = buckets_[ip];
  if (!bucket.initialized) {
    bucket.initialized = true;
    bucket.window_start_ms = now_ms;
  }

  int64_t elapsed = now_ms - bucket.window_start_ms;
  if (elapsed >= config_.window_ms) {
    bucket.window_start_ms = now_ms;
    bucket.count = 0;
    bucket.prev_count = 0;
  }

  if (bucket.count >= config_.max_packets_per_sec) {
    bucket.last_seen_ms = now_ms;
    return false;
  }

  ++bucket.count;
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
