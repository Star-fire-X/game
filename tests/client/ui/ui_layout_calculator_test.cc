#include <gtest/gtest.h>

#include "ui/ui_layout_calculator.h"

using namespace mir2::ui;

TEST(UILayoutCalculator, ScaleRect800to1920) {
    UILayoutCalculator calc(800, 600, 1920, 1080);
    Rect input = {100, 100, 200, 150};
    Rect output = calc.scale_rect(input);

    EXPECT_EQ(output.x, 240);      // 100 * 1920/800 = 240
    EXPECT_EQ(output.y, 180);      // 100 * 1080/600 = 180
    EXPECT_EQ(output.width, 480);  // 200 * 1920/800 = 480
    EXPECT_EQ(output.height, 270); // 150 * 1080/600 = 270
}

TEST(UILayoutCalculator, ScaleCoordinates) {
    UILayoutCalculator calc(800, 600, 1600, 1200);
    EXPECT_EQ(calc.scale_x(400), 800);
    EXPECT_EQ(calc.scale_y(300), 600);
}

TEST(UILayoutCalculator, ScaleDimensions) {
    UILayoutCalculator calc(800, 600, 1600, 1200);
    EXPECT_EQ(calc.scale_w(200), 400);
    EXPECT_EQ(calc.scale_h(100), 200);
}

TEST(UILayoutCalculator, MinimumSizeOne) {
    UILayoutCalculator calc(800, 600, 400, 300);
    Rect input = {0, 0, 1, 1};
    Rect output = calc.scale_rect(input);
    EXPECT_GE(output.width, 1);
    EXPECT_GE(output.height, 1);
}

TEST(UILayoutCalculator, ZeroTargetSize) {
    UILayoutCalculator calc(800, 600, 0, 0);
    Rect input = {100, 100, 200, 150};
    Rect output = calc.scale_rect(input);
    // 应该使用设计分辨率作为fallback
    EXPECT_EQ(output.x, 100);
    EXPECT_EQ(output.y, 100);
}
