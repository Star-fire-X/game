#include <gtest/gtest.h>

#include "network/conv_blacklist.h"

namespace {

using mir2::network::ConvBlacklist;

}  // namespace

TEST(ConvBlacklistTest, RecordFailureAddsToBlacklist) {
  ConvBlacklist::Config config{};
  config.max_failures = 3;
  config.blacklist_ttl_ms = 1000;
  config.failure_ttl_ms = 1000;
  config.cleanup_interval_ms = 0;

  ConvBlacklist blacklist(config);
  const uint32_t conv = 42;

  blacklist.RecordFailure(conv, 0);
  EXPECT_FALSE(blacklist.IsBlacklisted(conv, 0));

  blacklist.RecordFailure(conv, 10);
  EXPECT_FALSE(blacklist.IsBlacklisted(conv, 10));

  blacklist.RecordFailure(conv, 20);
  EXPECT_TRUE(blacklist.IsBlacklisted(conv, 20));
}

TEST(ConvBlacklistTest, BlacklistExpiresAfterTtl) {
  ConvBlacklist::Config config{};
  config.max_failures = 1;
  config.blacklist_ttl_ms = 100;
  config.failure_ttl_ms = 100;
  config.cleanup_interval_ms = 0;

  ConvBlacklist blacklist(config);
  const uint32_t conv = 7;

  blacklist.RecordFailure(conv, 0);
  EXPECT_TRUE(blacklist.IsBlacklisted(conv, 0));
  EXPECT_FALSE(blacklist.IsBlacklisted(conv, 200));
}

TEST(ConvBlacklistTest, FailureCountResetsAfterTtl) {
  ConvBlacklist::Config config{};
  config.max_failures = 3;
  config.blacklist_ttl_ms = 1000;
  config.failure_ttl_ms = 50;
  config.cleanup_interval_ms = 0;

  ConvBlacklist blacklist(config);
  const uint32_t conv = 99;

  blacklist.RecordFailure(conv, 0);
  blacklist.Cleanup(100);

  blacklist.RecordFailure(conv, 110);
  blacklist.RecordFailure(conv, 120);
  EXPECT_FALSE(blacklist.IsBlacklisted(conv, 120));
}
