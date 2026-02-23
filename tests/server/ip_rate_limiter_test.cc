#include <gtest/gtest.h>

#include "network/ip_rate_limiter.h"

namespace {

using mir2::network::IpRateLimiter;

}  // namespace

TEST(IpRateLimiterTest, SlidingWindowBlocksAfterLimit) {
  IpRateLimiter::Config config{};
  config.max_packets_per_sec = 3;
  config.window_ms = 1000;
  config.idle_expire_ms = 2000;
  config.cleanup_interval_ms = 0;

  IpRateLimiter limiter(config);
  const uint32_t ip = 0x7F000001;  // 127.0.0.1

  EXPECT_TRUE(limiter.Allow(ip, 0));
  EXPECT_TRUE(limiter.Allow(ip, 10));
  EXPECT_TRUE(limiter.Allow(ip, 20));
  EXPECT_FALSE(limiter.Allow(ip, 30));

  EXPECT_TRUE(limiter.Allow(ip, 1000));
}

TEST(IpRateLimiterTest, CleanupExpiresIdleBuckets) {
  IpRateLimiter::Config config{};
  config.max_packets_per_sec = 2;
  config.window_ms = 1000;
  config.idle_expire_ms = 500;
  config.cleanup_interval_ms = 0;

  IpRateLimiter limiter(config);
  const uint32_t ip = 0x01020304;

  EXPECT_TRUE(limiter.Allow(ip, 0));
  limiter.Cleanup(1000);
  EXPECT_TRUE(limiter.Allow(ip, 1000));
}
