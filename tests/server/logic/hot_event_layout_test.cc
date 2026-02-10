#include <type_traits>

#include <gtest/gtest.h>

#include "logic/events/hot_event.h"

namespace mir2::logic::events {
namespace {

TEST(HotEventLayoutTest, FitsSingleCacheLine) {
  EXPECT_EQ(sizeof(HotEvent), 64U);
  EXPECT_EQ(alignof(HotEvent), 64U);
}

TEST(HotEventLayoutTest, IsTriviallyCopyable) {
  EXPECT_TRUE(std::is_trivially_copyable_v<HotEvent>);
}

TEST(HotEventLayoutTest, PayloadUnionStaysCompact) {
  EXPECT_EQ(sizeof(HotEventData), 16U);
}

}  // namespace
}  // namespace mir2::logic::events
