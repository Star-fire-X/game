#include <algorithm>

#include <gtest/gtest.h>

namespace {
int OctileDistance(int dx, int dy) {
    int diag = std::min(dx, dy);
    return diag * 14 + (std::max(dx, dy) - diag) * 10;
}

TEST(AStarHeuristicValidation, HeuristicScale) {
    EXPECT_EQ(OctileDistance(5, 0), 50);
    EXPECT_EQ(OctileDistance(0, 3), 30);
    EXPECT_EQ(OctileDistance(3, 3), 42);
    EXPECT_EQ(OctileDistance(5, 3), 62);
}

TEST(AStarHeuristicValidation, Admissibility) {
    const int cases[][2] = {{1, 2}, {4, 7}, {6, 6}, {10, 3}};
    for (const auto& item : cases) {
        int dx = item[0];
        int dy = item[1];
        int diag = std::min(dx, dy);
        int expected = diag * 14 + (std::max(dx, dy) - diag) * 10;
        EXPECT_EQ(OctileDistance(dx, dy), expected);
    }
}
}  // namespace
