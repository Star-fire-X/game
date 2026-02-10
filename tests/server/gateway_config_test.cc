#include <gtest/gtest.h>

#include <type_traits>

#include "gateway/message_router.h"

namespace mir2::gateway {

class GatewayConfigTest : public ::testing::Test {};

TEST_F(GatewayConfigTest, RouterDeprecatedInUniversalForwardMode) {
  // Deprecated router should remain a lightweight placeholder.
  EXPECT_TRUE(std::is_default_constructible_v<MessageRouter>);
  EXPECT_TRUE(std::is_empty_v<MessageRouter>);
}

}  // namespace mir2::gateway
