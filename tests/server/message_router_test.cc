#include <gtest/gtest.h>

#include <type_traits>

#include "gateway/message_router.h"

namespace mir2::gateway {

TEST(MessageRouterTest, DeprecatedRouterIsEmptyAndNonCopyable) {
  EXPECT_TRUE(std::is_empty_v<MessageRouter>);
  EXPECT_FALSE(std::is_copy_constructible_v<MessageRouter>);
  EXPECT_FALSE(std::is_copy_assignable_v<MessageRouter>);
}

}  // namespace mir2::gateway
