#include "security/rate_limiter.h"

#include <algorithm>
#include <cstdint>

namespace mir2::security {

bool RateLimiter::TryAcquire(const std::string& key, int tokens) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto& bucket = buckets_[key];
  RefillBucket(bucket);
  if (bucket.tokens >= tokens) {
    bucket.tokens -= tokens;
    return true;
  }
  return false;
}

int RateLimiter::GetTokens(const std::string& key) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto& bucket = buckets_[key];
  RefillBucket(bucket);
  return bucket.tokens;
}

void RateLimiter::SetConfig(const Config& config, bool clear_buckets) {
  std::lock_guard<std::mutex> lock(mutex_);
  config_.capacity = std::max(1, config.capacity);
  config_.refill_rate = std::max(1, config.refill_rate);
  config_.refill_interval_seconds = std::max(1, config.refill_interval_seconds);
  if (clear_buckets) {
    buckets_.clear();
  }
}

void RateLimiter::RefillBucket(Bucket& bucket) {
  const auto now = std::chrono::steady_clock::now();
  if (bucket.last_refill.time_since_epoch().count() == 0) {
    bucket.tokens = config_.capacity;
    bucket.last_refill = now;
    return;
  }

  const auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
      now - bucket.last_refill);
  const int interval_seconds = std::max(1, config_.refill_interval_seconds);
  const int64_t intervals = elapsed.count() / interval_seconds;
  if (intervals > 0) {
    const int64_t refill = intervals * static_cast<int64_t>(config_.refill_rate);
    bucket.tokens = std::min(config_.capacity,
                             bucket.tokens + static_cast<int>(refill));
    bucket.last_refill += std::chrono::seconds(intervals * interval_seconds);
  }
}

}  // namespace mir2::security
