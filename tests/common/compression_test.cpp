#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

#include "common/compression.h"

namespace {

TEST(CompressionTest, DecompressExpandsBufferIncrementally) {
  std::vector<uint8_t> payload(1024 * 1024, 0x5A);
  const auto compressed = mir2::common::CompressLZ4(payload.data(), payload.size());
  ASSERT_FALSE(compressed.empty());
  ASSERT_LT(compressed.size(), payload.size());

  std::vector<uint8_t> decoded;
  ASSERT_TRUE(mir2::common::DecompressLZ4(compressed.data(),
                                          compressed.size(),
                                          &decoded,
                                          payload.size()));
  EXPECT_EQ(decoded, payload);
}

TEST(CompressionTest, DecompressFailsWhenCapTooSmall) {
  std::vector<uint8_t> payload(64 * 1024, 0x11);
  const auto compressed = mir2::common::CompressLZ4(payload.data(), payload.size());
  ASSERT_FALSE(compressed.empty());

  std::vector<uint8_t> decoded;
  EXPECT_FALSE(mir2::common::DecompressLZ4(compressed.data(),
                                           compressed.size(),
                                           &decoded,
                                           1024));
  EXPECT_TRUE(decoded.empty());
}

TEST(CompressionTest, DecompressRejectsInvalidData) {
  const std::vector<uint8_t> invalid_payload = {0x01, 0x02, 0x03, 0x04, 0x05};
  std::vector<uint8_t> decoded;
  EXPECT_FALSE(mir2::common::DecompressLZ4(invalid_payload.data(),
                                           invalid_payload.size(),
                                           &decoded,
                                           4096));
  EXPECT_TRUE(decoded.empty());
}

}  // namespace
