#include <gtest/gtest.h>

#include <algorithm>
#include <vector>

#include "ecs/systems/pathfinding_helper.h"

namespace {

using mir2::common::Position;
using mir2::ecs::PathfindingHelper;

TEST(PathfindingHelperTest, FindPathStraightLineReachesTarget) {
  const auto path = PathfindingHelper::FindPathStraightLine(0, 0, 3, 2, 10);
  ASSERT_FALSE(path.empty());
  EXPECT_EQ(path.back().x, 3);
  EXPECT_EQ(path.back().y, 2);
  EXPECT_LE(static_cast<int32_t>(path.size()), 10);
}

TEST(PathfindingHelperTest, FindPathWithCheckerReturnsEmptyWhenBlocked) {
  const auto path = PathfindingHelper::FindPath(
      0,
      0,
      2,
      0,
      [](int32_t x, int32_t y) {
        return y == 0 && x >= 0 && x <= 2 && x != 1;
      },
      20);
  EXPECT_TRUE(path.empty());
}

TEST(PathfindingHelperTest, DeprecatedFindPathMatchesStraightLineBehavior) {
  const auto legacy_path = PathfindingHelper::FindPath(1, 1, 4, 1, 10);
  const auto straight_path = PathfindingHelper::FindPathStraightLine(1, 1, 4, 1, 10);
  ASSERT_EQ(legacy_path.size(), straight_path.size());
  for (size_t i = 0; i < legacy_path.size(); ++i) {
    EXPECT_EQ(legacy_path[i].x, straight_path[i].x);
    EXPECT_EQ(legacy_path[i].y, straight_path[i].y);
  }
}

}  // namespace
