/**
 * @file ip_rate_limiter.h
 * @brief Per-IP UDP packet rate limiter.
 */

#ifndef MIR2_NETWORK_IP_RATE_LIMITER_H_
#define MIR2_NETWORK_IP_RATE_LIMITER_H_

#include <cstdint>
#include <mutex>
#include <unordered_map>

namespace mir2::network {

/**
 * @brief Weighted sliding-window rate limiter for UDP packets per IP.
 */
class IpRateLimiter {
 public:
  struct Config {
    uint32_t max_packets_per_sec = 1000;
    int64_t window_ms = 1000;
    int64_t idle_expire_ms = 60000;
    int64_t cleanup_interval_ms = 5000;
  };

  IpRateLimiter();
  explicit IpRateLimiter(Config config);

  /**
   * @brief Check whether a packet from the IP should be allowed.
   */
  bool Allow(uint32_t ip, int64_t now_ms);

  /**
   * @brief Cleanup expired entries.
   */
  void Cleanup(int64_t now_ms);

 private:
  struct Bucket {
    bool initialized = false;
    int64_t window_start_ms = 0;
    uint32_t curr_count = 0;
    uint32_t prev_count = 0;
    int64_t last_seen_ms = 0;
  };

  Config config_;
  std::unordered_map<uint32_t, Bucket> buckets_;
  std::mutex mutex_;
  int64_t last_cleanup_ms_ = 0;
};

}  // namespace mir2::network

#endif  // MIR2_NETWORK_IP_RATE_LIMITER_H_
