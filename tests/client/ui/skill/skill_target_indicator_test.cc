#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "client/ui/skill/mocks.h"
#include "ui/skill/skill_target_indicator.h"

namespace mir2::ui::skill {
namespace {

constexpr int kSegments = 32;

} // namespace

class SkillTargetIndicatorTest : public ::testing::Test {
protected:
    SkillTargetIndicator indicator_;
};

TEST_F(SkillTargetIndicatorTest, ShowHide_IsVisibleReflectsState) {
    EXPECT_FALSE(indicator_.is_visible());

    indicator_.show(1.0f, true);
    EXPECT_TRUE(indicator_.is_visible());

    indicator_.hide();
    EXPECT_FALSE(indicator_.is_visible());
}

TEST_F(SkillTargetIndicatorTest, Render_NotVisible_NoDrawCalls) {
    ::testing::StrictMock<MockRenderer> renderer;
    MockCamera camera;
    indicator_.render(renderer, camera);
}

TEST_F(SkillTargetIndicatorTest, Render_Visible_DrawsCircleAndCross) {
    ::testing::StrictMock<MockRenderer> renderer;
    MockCamera camera;
    camera.zoom = 1.0f;
    camera.viewport_width = 0;
    camera.viewport_height = 0;
    camera.x = 0.0f;
    camera.y = 0.0f;

    indicator_.set_position(0, 0);
    indicator_.show(1.0f, true);

    EXPECT_CALL(renderer, draw_point(::testing::_, ::testing::_, ::testing::_)).Times(0);
    EXPECT_CALL(renderer, draw_line(::testing::_, ::testing::_, ::testing::_, ::testing::_,
                                    ::testing::_))
        .Times(kSegments + 2);

    indicator_.render(renderer, camera);
}

} // namespace mir2::ui::skill
