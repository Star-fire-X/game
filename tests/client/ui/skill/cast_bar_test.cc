#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "client/ui/skill/mocks.h"
#include "ui/skill/cast_bar.h"

namespace mir2::ui::skill {

class CastBarTest : public ::testing::Test {
protected:
    CastBar cast_bar_;
};

TEST_F(CastBarTest, StartCastCancel_StateTransitions) {
    cast_bar_.start_cast("Fireball", 100, 200);
    EXPECT_TRUE(cast_bar_.is_active());
    EXPECT_FLOAT_EQ(cast_bar_.get_progress(), 0.0f);

    cast_bar_.cancel();
    EXPECT_FALSE(cast_bar_.is_active());
    EXPECT_FLOAT_EQ(cast_bar_.get_progress(), 0.0f);
}

TEST_F(CastBarTest, Update_ProgressCalculations) {
    cast_bar_.start_cast("Heal", 1000, 2000);

    cast_bar_.update(1000);
    EXPECT_FLOAT_EQ(cast_bar_.get_progress(), 0.0f);
    EXPECT_TRUE(cast_bar_.is_active());

    cast_bar_.update(1500);
    EXPECT_NEAR(cast_bar_.get_progress(), 0.5f, 0.01f);
    EXPECT_TRUE(cast_bar_.is_active());

    cast_bar_.update(2000);
    EXPECT_FLOAT_EQ(cast_bar_.get_progress(), 1.0f);
    EXPECT_FALSE(cast_bar_.is_active());
}

TEST_F(CastBarTest, Render_Inactive_NoDrawCalls) {
    ::testing::StrictMock<MockRenderer> renderer;
    cast_bar_.render(renderer, 100, 50);
}

TEST_F(CastBarTest, Render_Active_DrawsBackgroundFillAndBorder) {
    ::testing::StrictMock<MockRenderer> renderer;
    cast_bar_.start_cast("Shield", 100, 200);
    cast_bar_.update(150);

    EXPECT_CALL(renderer, draw_rect(::testing::_, ::testing::_)).Times(2);
    EXPECT_CALL(renderer, draw_rect_outline(::testing::_, ::testing::_)).Times(1);

    cast_bar_.render(renderer, 100, 50);
}

} // namespace mir2::ui::skill
