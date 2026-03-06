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
  const uint32_t source_ip = 0x7F000001;  // 127.0.0.1

  blacklist.RecordFailure(conv, source_ip, 0);
  EXPECT_FALSE(blacklist.IsBlacklisted(conv, source_ip, 0));

  blacklist.RecordFailure(conv, source_ip, 10);
  EXPECT_FALSE(blacklist.IsBlacklisted(conv, source_ip, 10));

  blacklist.RecordFailure(conv, source_ip, 20);
  EXPECT_TRUE(blacklist.IsBlacklisted(conv, source_ip, 20));
}

TEST(ConvBlacklistTest, BlacklistExpiresAfterTtl) {
  ConvBlacklist::Config config{};
  config.max_failures = 1;
  config.blacklist_ttl_ms = 100;
  config.failure_ttl_ms = 100;
  config.cleanup_interval_ms = 0;

  ConvBlacklist blacklist(config);
  const uint32_t conv = 7;
  const uint32_t source_ip = 0x7F000001;  // 127.0.0.1

  blacklist.RecordFailure(conv, source_ip, 0);
  EXPECT_TRUE(blacklist.IsBlacklisted(conv, source_ip, 0));
  EXPECT_FALSE(blacklist.IsBlacklisted(conv, source_ip, 200));
}

TEST(ConvBlacklistTest, FailureCountResetsAfterTtl) {
  ConvBlacklist::Config config{};
  config.max_failures = 3;
  config.blacklist_ttl_ms = 1000;
  config.failure_ttl_ms = 50;
  config.cleanup_interval_ms = 0;

  ConvBlacklist blacklist(config);
  const uint32_t conv = 99;
  const uint32_t source_ip = 0x7F000001;  // 127.0.0.1

  blacklist.RecordFailure(conv, source_ip, 0);
  blacklist.Cleanup(100);

  blacklist.RecordFailure(conv, source_ip, 110);
  blacklist.RecordFailure(conv, source_ip, 120);
  EXPECT_FALSE(blacklist.IsBlacklisted(conv, source_ip, 120));
}

TEST(ConvBlacklistTest, DifferentSourceIpDoesNotShareBlacklistState) {
  ConvBlacklist::Config config{};
  config.max_failures = 1;
  config.blacklist_ttl_ms = 1000;
  config.failure_ttl_ms = 1000;
  config.cleanup_interval_ms = 0;

  ConvBlacklist blacklist(config);
  const uint32_t conv = 123;
  const uint32_t source_ip_a = 0x7F000001;  // 127.0.0.1
  const uint32_t source_ip_b = 0x7F000002;  // 127.0.0.2

  blacklist.RecordFailure(conv, source_ip_a, 0);
  EXPECT_TRUE(blacklist.IsBlacklisted(conv, source_ip_a, 0));
  EXPECT_FALSE(blacklist.IsBlacklisted(conv, source_ip_b, 0));
}
