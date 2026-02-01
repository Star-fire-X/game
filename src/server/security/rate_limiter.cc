#include "security/rate_limiter.h"

#include <algorithm>

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

void RateLimiter::RefillBucket(Bucket& bucket) {
  const auto now = std::chrono::steady_clock::now();
  if (bucket.last_refill.time_since_epoch().count() == 0) {
    bucket.tokens = config_.capacity;
    bucket.last_refill = now;
    return;
  }

  const auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - bucket.last_refill);
  if (elapsed.count() > 0) {
    const int refill = static_cast<int>(elapsed.count()) * config_.refill_rate;
    bucket.tokens = std::min(config_.capacity, bucket.tokens + refill);
    bucket.last_refill = now;
  }
}

}  // namespace mir2::security
