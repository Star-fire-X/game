/**
 * @file rate_limiter.h
 * @brief 令牌桶限流器
 */

#ifndef MIR2_SECURITY_RATE_LIMITER_H
#define MIR2_SECURITY_RATE_LIMITER_H

#include <chrono>
#include <mutex>
#include <string>
#include <unordered_map>

namespace mir2::security {

/**
 * @brief 令牌桶限流器
 */
class RateLimiter {
 public:
  struct Config {
    int capacity = 10;
    int refill_rate = 10;
  };

  explicit RateLimiter(const Config& config) : config_(config) {}

  /**
   * @brief 尝试获取令牌
   */
  bool TryAcquire(const std::string& key, int tokens = 1);

  /**
   * @brief 获取当前令牌数
   */
  int GetTokens(const std::string& key);

 private:
  struct Bucket {
    int tokens = 0;
    std::chrono::steady_clock::time_point last_refill;
  };

  void RefillBucket(Bucket& bucket);

  Config config_;
  std::unordered_map<std::string, Bucket> buckets_;
  std::mutex mutex_;
};

}  // namespace mir2::security

#endif  // MIR2_SECURITY_RATE_LIMITER_H
