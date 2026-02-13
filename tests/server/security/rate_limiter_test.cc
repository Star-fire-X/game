#include "security/rate_limiter.h"

#include <gtest/gtest.h>

#include <chrono>
#include <thread>

namespace mir2::security {
namespace {

TEST(RateLimiterTest, RespectsRefillIntervalSeconds) {
  RateLimiter limiter({.capacity = 2, .refill_rate = 1, .refill_interval_seconds = 2});
  const std::string key = "login:user";

  EXPECT_TRUE(limiter.TryAcquire(key));
  EXPECT_TRUE(limiter.TryAcquire(key));
  EXPECT_FALSE(limiter.TryAcquire(key));

  std::this_thread::sleep_for(std::chrono::milliseconds(1100));
  EXPECT_FALSE(limiter.TryAcquire(key));

  std::this_thread::sleep_for(std::chrono::milliseconds(1100));
  EXPECT_TRUE(limiter.TryAcquire(key));
  EXPECT_FALSE(limiter.TryAcquire(key));
}

TEST(RateLimiterTest, ClampsInvalidIntervalToOneSecond) {
  RateLimiter limiter({.capacity = 1, .refill_rate = 1, .refill_interval_seconds = 1});
  limiter.SetConfig({.capacity = 1, .refill_rate = 1, .refill_interval_seconds = 0});
  const std::string key = "login:user";

  EXPECT_TRUE(limiter.TryAcquire(key));
  EXPECT_FALSE(limiter.TryAcquire(key));

  std::this_thread::sleep_for(std::chrono::milliseconds(1100));
  EXPECT_TRUE(limiter.TryAcquire(key));
}

}  // namespace
}  // namespace mir2::security
