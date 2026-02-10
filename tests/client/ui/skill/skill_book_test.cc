#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "client/ui/skill/mocks.h"
#include "ui/skill/skill_book.h"

namespace mir2::ui::skill {
namespace {

constexpr int kPanelX = 20;
constexpr int kPanelY = 20;
constexpr int kTitleHeight = 24;
constexpr int kSlotSize = 40;
constexpr int kSlotPadding = 6;
constexpr int kColumns = 4;
constexpr int kRows = 5;
constexpr int kContentPadding = 12;

int SlotX(int index) {
    const int col = index % kColumns;
    return kPanelX + kContentPadding + col * (kSlotSize + kSlotPadding) + 1;
}

int SlotY(int index) {
    const int row = index / kColumns;
    return kPanelY + kTitleHeight + kContentPadding + row * (kSlotSize + kSlotPadding) + 1;
}

} // namespace

class SkillBookTest : public ::testing::Test {
protected:
    MockSkillManager skill_manager_;
    SkillBook skill_book_{skill_manager_};
};

TEST_F(SkillBookTest, OpenCloseToggle_StateTransitions) {
    EXPECT_FALSE(skill_book_.is_open());

    skill_book_.open();
    EXPECT_TRUE(skill_book_.is_open());

    skill_book_.toggle();
    EXPECT_FALSE(skill_book_.is_open());

    skill_book_.toggle();
    EXPECT_TRUE(skill_book_.is_open());

    skill_book_.close();
    EXPECT_FALSE(skill_book_.is_open());
}

TEST_F(SkillBookTest, HandleClick_ClosedOrOutside_ReturnsZero) {
    EXPECT_EQ(skill_book_.handle_click(0, 0), 0u);

    skill_book_.open();
    EXPECT_EQ(skill_book_.handle_click(0, 0), 0u);
    EXPECT_EQ(skill_book_.get_hovered_skill(), nullptr);
}

TEST_F(SkillBookTest, HandleClick_SlotWithSkill_ReturnsSkillId) {
    client::skill::ClientLearnedSkill learned{};
    learned.skill_id = 101;
    skill_manager_.set_learned_skill(0, learned);

    client::skill::ClientSkillTemplate tmpl{};
    tmpl.id = 101;
    tmpl.name = "Test Skill";
    skill_manager_.set_template(tmpl);

    skill_book_.open();
    const uint32_t result = skill_book_.handle_click(SlotX(0), SlotY(0));
    EXPECT_EQ(result, 101u);

    const auto* hovered = skill_book_.get_hovered_skill();
    ASSERT_NE(hovered, nullptr);
    EXPECT_EQ(hovered->id, 101u);
}

TEST_F(SkillBookTest, Render_Closed_DoesNotDraw) {
    ::testing::StrictMock<MockRenderer> renderer;
    skill_book_.render(renderer);
}

TEST_F(SkillBookTest, Render_Open_DrawsPanelAndSlots) {
    ::testing::StrictMock<MockRenderer> renderer;
    skill_book_.open();

    const int slot_count = kColumns * kRows;
    EXPECT_CALL(renderer, draw_rect(::testing::_, ::testing::_))
        .Times(1 + slot_count);
    EXPECT_CALL(renderer, draw_rect_outline(::testing::_, ::testing::_))
        .Times(1 + slot_count);

    skill_book_.render(renderer);
}

} // namespace mir2::ui::skill
