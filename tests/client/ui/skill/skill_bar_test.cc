#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "client/ui/skill/mocks.h"
#include "ui/skill/skill_bar.h"

namespace mir2::ui::skill {
namespace {

constexpr int kSlotSize = 40;
constexpr int kSlotPadding = 2;

int SlotX(int bar_x, int slot_index) {
    return bar_x + slot_index * (kSlotSize + kSlotPadding) + 1;
}

int SlotY(int bar_y) {
    return bar_y + 1;
}

} // namespace

class SkillBarTest : public ::testing::Test {
protected:
    MockSkillManager skill_manager_;
    SkillBar skill_bar_{skill_manager_};
};

TEST_F(SkillBarTest, Constructor_DefaultState_NotDragging) {
    EXPECT_FALSE(skill_bar_.is_dragging());
}

TEST_F(SkillBarTest, GetSlotAt_InsideAndOutside_ReturnsExpectedIndex) {
    const int bar_x = 100;
    const int bar_y = 200;

    EXPECT_EQ(skill_bar_.get_slot_at(SlotX(bar_x, 0), SlotY(bar_y), bar_x, bar_y), 0);
    EXPECT_EQ(skill_bar_.get_slot_at(SlotX(bar_x, 3), SlotY(bar_y), bar_x, bar_y), 3);
    EXPECT_EQ(skill_bar_.get_slot_at(bar_x - 5, bar_y - 5, bar_x, bar_y), -1);
}

TEST_F(SkillBarTest, HandleClick_EmptySlot_DoesNotStartDragging) {
    const int bar_x = 50;
    const int bar_y = 60;

    EXPECT_TRUE(skill_bar_.handle_click(SlotX(bar_x, 0), SlotY(bar_y), bar_x, bar_y));
    EXPECT_FALSE(skill_bar_.is_dragging());
}

TEST_F(SkillBarTest, HandleClick_FilledSlot_StartsDragging) {
    const int bar_x = 50;
    const int bar_y = 60;

    skill_manager_.set_skill_by_hotkey(1, 100);
    EXPECT_TRUE(skill_bar_.handle_click(SlotX(bar_x, 0), SlotY(bar_y), bar_x, bar_y));
    EXPECT_TRUE(skill_bar_.is_dragging());
}

TEST_F(SkillBarTest, StartDrag_EndDrag_UpdatesBindingAndState) {
    skill_manager_.set_skill_by_hotkey(1, 200);
    skill_bar_.start_drag(0);
    ASSERT_TRUE(skill_bar_.is_dragging());

    skill_bar_.end_drag(1);
    EXPECT_FALSE(skill_bar_.is_dragging());
    EXPECT_EQ(skill_manager_.bind_hotkey_calls, 1);
    EXPECT_EQ(skill_manager_.last_bind_slot, 2);
    EXPECT_EQ(skill_manager_.last_bind_skill_id, 200u);
}

TEST_F(SkillBarTest, StartDrag_InvalidSlot_ClearsDragging) {
    skill_manager_.set_skill_by_hotkey(1, 200);
    skill_bar_.start_drag(-1);
    EXPECT_FALSE(skill_bar_.is_dragging());
}

TEST_F(SkillBarTest, Update_InvokesSkillManagerUpdate) {
    skill_bar_.update(12345);
    EXPECT_EQ(skill_manager_.update_calls, 1);
    EXPECT_EQ(skill_manager_.last_update_ms, 12345);
}

TEST_F(SkillBarTest, Render_EmptySlots_DrawsBarAndSlots) {
    ::testing::StrictMock<MockRenderer> renderer;

    EXPECT_CALL(renderer, draw_rect(::testing::_, ::testing::_))
        .Times(1 + SkillBar::SLOT_COUNT);
    EXPECT_CALL(renderer, draw_rect_outline(::testing::_, ::testing::_))
        .Times(1 + SkillBar::SLOT_COUNT);

    skill_bar_.render(renderer, 100, 200);
}

} // namespace mir2::ui::skill
