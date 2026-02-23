// =============================================================================
// Legend2 HUD - Unit Tests
//
// Tests for the HUD widget hierarchy:
//   - StatusBar:       value management, ratio calculation, edge cases
//   - PlayerInfoPanel: player info storage and defaults
//   - HudContainer:    child creation, stat propagation, layout positioning
//
// These tests verify widget state only (bounds, visibility, ratios).
// No rendering calls are exercised; UIRenderer is not instantiated.
// =============================================================================

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "client/ui/hud/status_bar.h"
#include "client/ui/hud/player_info_panel.h"
#include "client/ui/hud/hud_container.h"
#include "common/types/character_stats.h"

namespace mir2::ui::hud {

// =============================================================================
// StatusBar Tests
// =============================================================================

class StatusBarTest : public ::testing::Test {
protected:
    // Red fill, dark background, gray border -- typical HP bar colors.
    StatusBar bar_{"HP", {220, 50, 50, 255}, {40, 40, 40, 200}, {100, 100, 100, 255}};
};

// -- Construction and default state --

TEST_F(StatusBarTest, Constructor_SetsLabelAndId) {
    EXPECT_EQ(bar_.get_label(), "HP");
    EXPECT_EQ(bar_.get_id(), "status_bar_HP");
}

TEST_F(StatusBarTest, Constructor_DefaultValuesAreZero) {
    EXPECT_EQ(bar_.get_current(), 0);
    EXPECT_EQ(bar_.get_max(), 0);
}

TEST_F(StatusBarTest, Constructor_DefaultRatioIsZero) {
    EXPECT_FLOAT_EQ(bar_.get_ratio(), 0.0f);
}

TEST_F(StatusBarTest, Constructor_IsVisibleByDefault) {
    EXPECT_TRUE(bar_.is_visible());
}

TEST_F(StatusBarTest, Constructor_IsEnabledByDefault) {
    EXPECT_TRUE(bar_.is_enabled());
}

// -- set_value: normal cases --

TEST_F(StatusBarTest, SetValue_NormalValues_StoresCorrectly) {
    bar_.set_value(75, 100);
    EXPECT_EQ(bar_.get_current(), 75);
    EXPECT_EQ(bar_.get_max(), 100);
}

TEST_F(StatusBarTest, SetValue_FullBar_CurrentEqualsMax) {
    bar_.set_value(200, 200);
    EXPECT_EQ(bar_.get_current(), 200);
    EXPECT_EQ(bar_.get_max(), 200);
}

TEST_F(StatusBarTest, SetValue_EmptyBar_CurrentIsZero) {
    bar_.set_value(0, 100);
    EXPECT_EQ(bar_.get_current(), 0);
    EXPECT_EQ(bar_.get_max(), 100);
}

TEST_F(StatusBarTest, SetValue_OverwritesPreviousValues) {
    bar_.set_value(50, 100);
    bar_.set_value(30, 60);
    EXPECT_EQ(bar_.get_current(), 30);
    EXPECT_EQ(bar_.get_max(), 60);
}

// -- set_value: clamping behavior --

TEST_F(StatusBarTest, SetValue_CurrentExceedsMax_ClampedToMax) {
    bar_.set_value(150, 100);
    EXPECT_EQ(bar_.get_current(), 100);
    EXPECT_EQ(bar_.get_max(), 100);
}

TEST_F(StatusBarTest, SetValue_NegativeCurrent_ClampedToZero) {
    bar_.set_value(-10, 100);
    EXPECT_EQ(bar_.get_current(), 0);
    EXPECT_EQ(bar_.get_max(), 100);
}

TEST_F(StatusBarTest, SetValue_NegativeMax_ClampedToZero) {
    bar_.set_value(50, -20);
    EXPECT_EQ(bar_.get_max(), 0);
    // When max is 0, current is clamped to [0, 0].
    EXPECT_EQ(bar_.get_current(), 0);
}

TEST_F(StatusBarTest, SetValue_BothNegative_BothClampedToZero) {
    bar_.set_value(-5, -10);
    EXPECT_EQ(bar_.get_current(), 0);
    EXPECT_EQ(bar_.get_max(), 0);
}

TEST_F(StatusBarTest, SetValue_ZeroMax_CurrentClampedToZero) {
    bar_.set_value(50, 0);
    EXPECT_EQ(bar_.get_max(), 0);
    EXPECT_EQ(bar_.get_current(), 0);
}

// -- get_ratio: normal cases --

TEST_F(StatusBarTest, GetRatio_HalfFull_Returns0Point5) {
    bar_.set_value(50, 100);
    EXPECT_FLOAT_EQ(bar_.get_ratio(), 0.5f);
}

TEST_F(StatusBarTest, GetRatio_Full_Returns1Point0) {
    bar_.set_value(100, 100);
    EXPECT_FLOAT_EQ(bar_.get_ratio(), 1.0f);
}

TEST_F(StatusBarTest, GetRatio_Empty_Returns0Point0) {
    bar_.set_value(0, 100);
    EXPECT_FLOAT_EQ(bar_.get_ratio(), 0.0f);
}

TEST_F(StatusBarTest, GetRatio_QuarterFull_Returns0Point25) {
    bar_.set_value(25, 100);
    EXPECT_FLOAT_EQ(bar_.get_ratio(), 0.25f);
}

TEST_F(StatusBarTest, GetRatio_OneThird_IsApproximate) {
    bar_.set_value(1, 3);
    EXPECT_NEAR(bar_.get_ratio(), 1.0f / 3.0f, 0.001f);
}

// -- get_ratio: edge cases --

TEST_F(StatusBarTest, GetRatio_MaxIsZero_ReturnsZero) {
    bar_.set_value(0, 0);
    EXPECT_FLOAT_EQ(bar_.get_ratio(), 0.0f);
}

TEST_F(StatusBarTest, GetRatio_AfterNegativeMax_ReturnsZero) {
    bar_.set_value(50, -10);
    EXPECT_FLOAT_EQ(bar_.get_ratio(), 0.0f);
}

// -- Visibility control --

TEST_F(StatusBarTest, SetVisible_False_HidesBar) {
    bar_.set_visible(false);
    EXPECT_FALSE(bar_.is_visible());
}

TEST_F(StatusBarTest, SetVisible_TrueAfterHiding_ShowsBar) {
    bar_.set_visible(false);
    bar_.set_visible(true);
    EXPECT_TRUE(bar_.is_visible());
}

// -- Bounds management --

TEST_F(StatusBarTest, SetBounds_StoresRect) {
    bar_.set_bounds({10, 20, 300, 22});
    const auto& bounds = bar_.get_bounds();
    EXPECT_EQ(bounds.x, 10);
    EXPECT_EQ(bounds.y, 20);
    EXPECT_EQ(bounds.width, 300);
    EXPECT_EQ(bounds.height, 22);
}

TEST_F(StatusBarTest, SetPosition_UpdatesXY) {
    bar_.set_bounds({0, 0, 300, 22});
    bar_.set_position(50, 60);
    const auto& bounds = bar_.get_bounds();
    EXPECT_EQ(bounds.x, 50);
    EXPECT_EQ(bounds.y, 60);
    EXPECT_EQ(bounds.width, 300);
    EXPECT_EQ(bounds.height, 22);
}

TEST_F(StatusBarTest, SetSize_UpdatesWidthHeight) {
    bar_.set_bounds({10, 20, 0, 0});
    bar_.set_size(400, 30);
    const auto& bounds = bar_.get_bounds();
    EXPECT_EQ(bounds.x, 10);
    EXPECT_EQ(bounds.y, 20);
    EXPECT_EQ(bounds.width, 400);
    EXPECT_EQ(bounds.height, 30);
}

// -- Label with different names --

TEST(StatusBarLabelTest, DifferentLabels_ProduceDistinctIds) {
    StatusBar hp("HP", {220, 50, 50, 255}, {40, 40, 40, 200}, {100, 100, 100, 255});
    StatusBar mp("MP", {50, 100, 220, 255}, {40, 40, 40, 200}, {100, 100, 100, 255});
    StatusBar exp("EXP", {220, 180, 50, 255}, {40, 40, 40, 200}, {100, 100, 100, 255});

    EXPECT_EQ(hp.get_id(), "status_bar_HP");
    EXPECT_EQ(mp.get_id(), "status_bar_MP");
    EXPECT_EQ(exp.get_id(), "status_bar_EXP");
}

// -- Large values --

TEST_F(StatusBarTest, SetValue_LargeValues_ComputesRatioCorrectly) {
    bar_.set_value(500000, 1000000);
    EXPECT_FLOAT_EQ(bar_.get_ratio(), 0.5f);
}

// =============================================================================
// PlayerInfoPanel Tests
// =============================================================================

class PlayerInfoPanelTest : public ::testing::Test {
protected:
    PlayerInfoPanel panel_;
};

// -- Construction and default state --

TEST_F(PlayerInfoPanelTest, Constructor_SetsId) {
    EXPECT_EQ(panel_.get_id(), "player_info_panel");
}

TEST_F(PlayerInfoPanelTest, Constructor_DefaultName_IsEmpty) {
    EXPECT_TRUE(panel_.get_name().empty());
}

TEST_F(PlayerInfoPanelTest, Constructor_DefaultLevel_IsOne) {
    EXPECT_EQ(panel_.get_level(), 1);
}

TEST_F(PlayerInfoPanelTest, Constructor_DefaultGold_IsZero) {
    EXPECT_EQ(panel_.get_gold(), 0);
}

TEST_F(PlayerInfoPanelTest, Constructor_IsVisibleByDefault) {
    EXPECT_TRUE(panel_.is_visible());
}

// -- set_player_info --

TEST_F(PlayerInfoPanelTest, SetPlayerInfo_NormalValues_StoresCorrectly) {
    panel_.set_player_info("Warrior", 35, 12500);
    EXPECT_EQ(panel_.get_name(), "Warrior");
    EXPECT_EQ(panel_.get_level(), 35);
    EXPECT_EQ(panel_.get_gold(), 12500);
}

TEST_F(PlayerInfoPanelTest, SetPlayerInfo_EmptyName_StoredAsEmpty) {
    panel_.set_player_info("", 10, 500);
    EXPECT_TRUE(panel_.get_name().empty());
    EXPECT_EQ(panel_.get_level(), 10);
    EXPECT_EQ(panel_.get_gold(), 500);
}

TEST_F(PlayerInfoPanelTest, SetPlayerInfo_ZeroGold_Accepted) {
    panel_.set_player_info("Mage", 20, 0);
    EXPECT_EQ(panel_.get_gold(), 0);
}

TEST_F(PlayerInfoPanelTest, SetPlayerInfo_LevelOne_Accepted) {
    panel_.set_player_info("Taoist", 1, 100);
    EXPECT_EQ(panel_.get_level(), 1);
}

TEST_F(PlayerInfoPanelTest, SetPlayerInfo_OverwritesPreviousValues) {
    panel_.set_player_info("Alice", 10, 1000);
    panel_.set_player_info("Bob", 50, 99999);
    EXPECT_EQ(panel_.get_name(), "Bob");
    EXPECT_EQ(panel_.get_level(), 50);
    EXPECT_EQ(panel_.get_gold(), 99999);
}

TEST_F(PlayerInfoPanelTest, SetPlayerInfo_SpecialCharactersInName_Preserved) {
    panel_.set_player_info("Player_123!", 5, 0);
    EXPECT_EQ(panel_.get_name(), "Player_123!");
}

TEST_F(PlayerInfoPanelTest, SetPlayerInfo_UnicodeNamePreserved) {
    panel_.set_player_info("\xe6\x88\x98\xe5\xa3\xab", 10, 500);
    EXPECT_EQ(panel_.get_name(), "\xe6\x88\x98\xe5\xa3\xab");
}

// -- Bounds management --

TEST_F(PlayerInfoPanelTest, SetBounds_StoresCorrectly) {
    panel_.set_bounds({10, 10, 160, 72});
    const auto& bounds = panel_.get_bounds();
    EXPECT_EQ(bounds.x, 10);
    EXPECT_EQ(bounds.y, 10);
    EXPECT_EQ(bounds.width, 160);
    EXPECT_EQ(bounds.height, 72);
}

TEST_F(PlayerInfoPanelTest, ApplyBuff_AddsAndUpdatesStacks) {
    panel_.apply_buff(3001, 1);
    EXPECT_EQ(panel_.get_active_buff_count(), 1u);
    EXPECT_EQ(panel_.get_buff_stack(3001), 1u);

    panel_.apply_buff(3001, 3);
    EXPECT_EQ(panel_.get_active_buff_count(), 1u);
    EXPECT_EQ(panel_.get_buff_stack(3001), 3u);

    panel_.apply_buff(3002, 2);
    EXPECT_EQ(panel_.get_active_buff_count(), 2u);
    EXPECT_EQ(panel_.get_buff_stack(3002), 2u);
}

TEST_F(PlayerInfoPanelTest, RemoveAndClearBuffs_WorkAsExpected) {
    panel_.apply_buff(4001, 1);
    panel_.apply_buff(4002, 1);
    EXPECT_EQ(panel_.get_active_buff_count(), 2u);

    panel_.remove_buff(4001);
    EXPECT_EQ(panel_.get_active_buff_count(), 1u);
    EXPECT_EQ(panel_.get_buff_stack(4001), 0u);

    panel_.clear_buffs();
    EXPECT_EQ(panel_.get_active_buff_count(), 0u);
    EXPECT_EQ(panel_.get_buff_stack(4002), 0u);
}

// =============================================================================
// HudContainer Tests
// =============================================================================

class HudContainerTest : public ::testing::Test {
protected:
    // Use default 800x600 screen size.
    HudContainer hud_;

    // Layout constants matching HudContainer private statics.
    static constexpr int BAR_WIDTH = 300;
    static constexpr int BAR_HEIGHT = 22;
    static constexpr int BAR_SPACING = 2;
    static constexpr int BAR_BOTTOM_MARGIN = 20;
    static constexpr int INFO_PANEL_WIDTH = 160;
    static constexpr int INFO_PANEL_HEIGHT = 72;
    static constexpr int INFO_PANEL_MARGIN = 10;
};

// -- Construction and child creation --

TEST_F(HudContainerTest, Constructor_SetsId) {
    EXPECT_EQ(hud_.get_id(), "hud_container");
}

TEST_F(HudContainerTest, Constructor_CreatesAllChildren) {
    EXPECT_NE(hud_.get_hp_bar(), nullptr);
    EXPECT_NE(hud_.get_mp_bar(), nullptr);
    EXPECT_NE(hud_.get_exp_bar(), nullptr);
    EXPECT_NE(hud_.get_player_info(), nullptr);
}

TEST_F(HudContainerTest, Constructor_HasFourChildren) {
    EXPECT_EQ(hud_.get_children().size(), 4u);
}

TEST_F(HudContainerTest, Constructor_ChildrenAreVisible) {
    EXPECT_TRUE(hud_.get_hp_bar()->is_visible());
    EXPECT_TRUE(hud_.get_mp_bar()->is_visible());
    EXPECT_TRUE(hud_.get_exp_bar()->is_visible());
    EXPECT_TRUE(hud_.get_player_info()->is_visible());
}

TEST_F(HudContainerTest, Constructor_BarLabelsAreCorrect) {
    EXPECT_EQ(hud_.get_hp_bar()->get_label(), "HP");
    EXPECT_EQ(hud_.get_mp_bar()->get_label(), "MP");
    EXPECT_EQ(hud_.get_exp_bar()->get_label(), "EXP");
}

// -- Default layout at 800x600 --

TEST_F(HudContainerTest, DefaultLayout_ContainerSpansFullScreen) {
    const auto& bounds = hud_.get_bounds();
    EXPECT_EQ(bounds.x, 0);
    EXPECT_EQ(bounds.y, 0);
    EXPECT_EQ(bounds.width, 800);
    EXPECT_EQ(bounds.height, 600);
}

TEST_F(HudContainerTest, DefaultLayout_BarsAreCenteredHorizontally) {
    const int expected_x = (800 - BAR_WIDTH) / 2;
    EXPECT_EQ(hud_.get_hp_bar()->get_bounds().x, expected_x);
    EXPECT_EQ(hud_.get_mp_bar()->get_bounds().x, expected_x);
    EXPECT_EQ(hud_.get_exp_bar()->get_bounds().x, expected_x);
}

TEST_F(HudContainerTest, DefaultLayout_BarsHaveCorrectDimensions) {
    EXPECT_EQ(hud_.get_hp_bar()->get_bounds().width, BAR_WIDTH);
    EXPECT_EQ(hud_.get_hp_bar()->get_bounds().height, BAR_HEIGHT);
    EXPECT_EQ(hud_.get_mp_bar()->get_bounds().width, BAR_WIDTH);
    EXPECT_EQ(hud_.get_mp_bar()->get_bounds().height, BAR_HEIGHT);
    EXPECT_EQ(hud_.get_exp_bar()->get_bounds().width, BAR_WIDTH);
    EXPECT_EQ(hud_.get_exp_bar()->get_bounds().height, BAR_HEIGHT);
}

TEST_F(HudContainerTest, DefaultLayout_ExpBarIsAtBottom) {
    // EXP bar: screen_height - BAR_BOTTOM_MARGIN - BAR_HEIGHT
    const int expected_y = 600 - BAR_BOTTOM_MARGIN - BAR_HEIGHT;
    EXPECT_EQ(hud_.get_exp_bar()->get_bounds().y, expected_y);
}

TEST_F(HudContainerTest, DefaultLayout_MpBarIsAboveExpBar) {
    const int exp_y = 600 - BAR_BOTTOM_MARGIN - BAR_HEIGHT;
    const int expected_mp_y = exp_y - BAR_SPACING - BAR_HEIGHT;
    EXPECT_EQ(hud_.get_mp_bar()->get_bounds().y, expected_mp_y);
}

TEST_F(HudContainerTest, DefaultLayout_HpBarIsAboveMpBar) {
    const int exp_y = 600 - BAR_BOTTOM_MARGIN - BAR_HEIGHT;
    const int mp_y = exp_y - BAR_SPACING - BAR_HEIGHT;
    const int expected_hp_y = mp_y - BAR_SPACING - BAR_HEIGHT;
    EXPECT_EQ(hud_.get_hp_bar()->get_bounds().y, expected_hp_y);
}

TEST_F(HudContainerTest, DefaultLayout_BarsAreStackedInOrder) {
    // HP is above MP is above EXP (lower y = higher on screen).
    EXPECT_LT(hud_.get_hp_bar()->get_bounds().y, hud_.get_mp_bar()->get_bounds().y);
    EXPECT_LT(hud_.get_mp_bar()->get_bounds().y, hud_.get_exp_bar()->get_bounds().y);
}

TEST_F(HudContainerTest, DefaultLayout_BarSpacingIsConsistent) {
    const int hp_bottom = hud_.get_hp_bar()->get_bounds().y + BAR_HEIGHT;
    const int mp_top = hud_.get_mp_bar()->get_bounds().y;
    EXPECT_EQ(mp_top - hp_bottom, BAR_SPACING);

    const int mp_bottom = hud_.get_mp_bar()->get_bounds().y + BAR_HEIGHT;
    const int exp_top = hud_.get_exp_bar()->get_bounds().y;
    EXPECT_EQ(exp_top - mp_bottom, BAR_SPACING);
}

TEST_F(HudContainerTest, DefaultLayout_PlayerInfoIsTopLeft) {
    const auto& bounds = hud_.get_player_info()->get_bounds();
    EXPECT_EQ(bounds.x, INFO_PANEL_MARGIN);
    EXPECT_EQ(bounds.y, INFO_PANEL_MARGIN);
    EXPECT_EQ(bounds.width, INFO_PANEL_WIDTH);
    EXPECT_EQ(bounds.height, INFO_PANEL_HEIGHT);
}

// -- Custom screen size at construction --

TEST(HudContainerCustomSizeTest, CustomScreenSize_LayoutUsesNewDimensions) {
    HudContainer hud(1920, 1080);
    const auto& bounds = hud.get_bounds();
    EXPECT_EQ(bounds.width, 1920);
    EXPECT_EQ(bounds.height, 1080);

    // Bars should be centered for 1920 width.
    const int expected_x = (1920 - 300) / 2;
    EXPECT_EQ(hud.get_hp_bar()->get_bounds().x, expected_x);
    EXPECT_EQ(hud.get_mp_bar()->get_bounds().x, expected_x);
    EXPECT_EQ(hud.get_exp_bar()->get_bounds().x, expected_x);

    // EXP bar at bottom of 1080 screen.
    const int expected_exp_y = 1080 - 20 - 22;
    EXPECT_EQ(hud.get_exp_bar()->get_bounds().y, expected_exp_y);
}

// -- set_screen_size --

TEST_F(HudContainerTest, SetScreenSize_UpdatesContainerBounds) {
    hud_.set_screen_size(1024, 768);
    const auto& bounds = hud_.get_bounds();
    EXPECT_EQ(bounds.width, 1024);
    EXPECT_EQ(bounds.height, 768);
}

TEST_F(HudContainerTest, SetScreenSize_RecalculatesBarPositions) {
    hud_.set_screen_size(1024, 768);

    const int expected_x = (1024 - BAR_WIDTH) / 2;
    EXPECT_EQ(hud_.get_hp_bar()->get_bounds().x, expected_x);
    EXPECT_EQ(hud_.get_mp_bar()->get_bounds().x, expected_x);
    EXPECT_EQ(hud_.get_exp_bar()->get_bounds().x, expected_x);

    const int expected_exp_y = 768 - BAR_BOTTOM_MARGIN - BAR_HEIGHT;
    EXPECT_EQ(hud_.get_exp_bar()->get_bounds().y, expected_exp_y);
}

TEST_F(HudContainerTest, SetScreenSize_SameSize_NoChange) {
    // Capture original positions.
    const auto hp_bounds_before = hud_.get_hp_bar()->get_bounds();

    // Set to same size -- should be a no-op.
    hud_.set_screen_size(800, 600);

    const auto& hp_bounds_after = hud_.get_hp_bar()->get_bounds();
    EXPECT_EQ(hp_bounds_before.x, hp_bounds_after.x);
    EXPECT_EQ(hp_bounds_before.y, hp_bounds_after.y);
}

TEST_F(HudContainerTest, SetScreenSize_SmallScreen_BarsStillPositioned) {
    hud_.set_screen_size(320, 240);

    const int expected_x = (320 - BAR_WIDTH) / 2;
    EXPECT_EQ(hud_.get_hp_bar()->get_bounds().x, expected_x);

    const int expected_exp_y = 240 - BAR_BOTTOM_MARGIN - BAR_HEIGHT;
    EXPECT_EQ(hud_.get_exp_bar()->get_bounds().y, expected_exp_y);
}

TEST_F(HudContainerTest, SetScreenSize_PlayerInfoPanelPositionUnchanged) {
    // The info panel is always at (margin, margin), regardless of screen size.
    hud_.set_screen_size(1920, 1080);
    const auto& bounds = hud_.get_player_info()->get_bounds();
    EXPECT_EQ(bounds.x, INFO_PANEL_MARGIN);
    EXPECT_EQ(bounds.y, INFO_PANEL_MARGIN);
}

// -- update_from_stats --

TEST_F(HudContainerTest, UpdateFromStats_SetsHpBarValues) {
    mir2::common::CharacterStats stats;
    stats.hp = 80;
    stats.max_hp = 150;
    stats.mp = 50;
    stats.max_mp = 100;
    stats.experience = 45;
    stats.level = 10;
    stats.gold = 5000;

    hud_.update_from_stats(stats, "TestPlayer");

    EXPECT_EQ(hud_.get_hp_bar()->get_current(), 80);
    EXPECT_EQ(hud_.get_hp_bar()->get_max(), 150);
}

TEST_F(HudContainerTest, UpdateFromStats_SetsMpBarValues) {
    mir2::common::CharacterStats stats;
    stats.hp = 100;
    stats.max_hp = 100;
    stats.mp = 30;
    stats.max_mp = 80;
    stats.experience = 0;
    stats.level = 5;
    stats.gold = 100;

    hud_.update_from_stats(stats, "Mage");

    EXPECT_EQ(hud_.get_mp_bar()->get_current(), 30);
    EXPECT_EQ(hud_.get_mp_bar()->get_max(), 80);
}

TEST_F(HudContainerTest, UpdateFromStats_SetsExpBarValues) {
    mir2::common::CharacterStats stats;
    stats.hp = 100;
    stats.max_hp = 100;
    stats.mp = 50;
    stats.max_mp = 50;
    stats.experience = 75;
    stats.level = 1;
    stats.gold = 0;

    hud_.update_from_stats(stats, "Player");

    // EXP bar treats experience as percentage out of 100.
    EXPECT_EQ(hud_.get_exp_bar()->get_current(), 75);
    EXPECT_EQ(hud_.get_exp_bar()->get_max(), 100);
}

TEST_F(HudContainerTest, UpdateFromStats_SetsPlayerInfo) {
    mir2::common::CharacterStats stats;
    stats.level = 25;
    stats.gold = 50000;
    stats.hp = 100;
    stats.max_hp = 100;
    stats.mp = 50;
    stats.max_mp = 50;
    stats.experience = 0;

    hud_.update_from_stats(stats, "Champion");

    EXPECT_EQ(hud_.get_player_info()->get_name(), "Champion");
    EXPECT_EQ(hud_.get_player_info()->get_level(), 25);
    EXPECT_EQ(hud_.get_player_info()->get_gold(), 50000);
}

TEST_F(HudContainerTest, BuffOperationsForwardToPlayerInfoPanel) {
    hud_.apply_buff(9001, 2);
    EXPECT_EQ(hud_.get_player_info()->get_active_buff_count(), 1u);
    EXPECT_EQ(hud_.get_player_info()->get_buff_stack(9001), 2u);

    hud_.remove_buff(9001);
    EXPECT_EQ(hud_.get_player_info()->get_active_buff_count(), 0u);

    hud_.apply_buff(9002, 1);
    hud_.apply_buff(9003, 1);
    EXPECT_EQ(hud_.get_player_info()->get_active_buff_count(), 2u);
    hud_.clear_buffs();
    EXPECT_EQ(hud_.get_player_info()->get_active_buff_count(), 0u);
}

TEST_F(HudContainerTest, UpdateFromStats_HpRatioIsCorrect) {
    mir2::common::CharacterStats stats;
    stats.hp = 50;
    stats.max_hp = 200;
    stats.mp = 0;
    stats.max_mp = 100;
    stats.experience = 0;
    stats.level = 1;
    stats.gold = 0;

    hud_.update_from_stats(stats, "Player");

    EXPECT_FLOAT_EQ(hud_.get_hp_bar()->get_ratio(), 0.25f);
}

TEST_F(HudContainerTest, UpdateFromStats_MpRatioIsCorrect) {
    mir2::common::CharacterStats stats;
    stats.hp = 100;
    stats.max_hp = 100;
    stats.mp = 60;
    stats.max_mp = 80;
    stats.experience = 0;
    stats.level = 1;
    stats.gold = 0;

    hud_.update_from_stats(stats, "Player");

    EXPECT_FLOAT_EQ(hud_.get_mp_bar()->get_ratio(), 0.75f);
}

TEST_F(HudContainerTest, UpdateFromStats_MultipleUpdates_OverwritePrevious) {
    mir2::common::CharacterStats stats1;
    stats1.hp = 100;
    stats1.max_hp = 100;
    stats1.mp = 50;
    stats1.max_mp = 50;
    stats1.experience = 10;
    stats1.level = 1;
    stats1.gold = 0;

    hud_.update_from_stats(stats1, "Player1");

    mir2::common::CharacterStats stats2;
    stats2.hp = 30;
    stats2.max_hp = 80;
    stats2.mp = 20;
    stats2.max_mp = 60;
    stats2.experience = 90;
    stats2.level = 50;
    stats2.gold = 99999;

    hud_.update_from_stats(stats2, "Player2");

    EXPECT_EQ(hud_.get_hp_bar()->get_current(), 30);
    EXPECT_EQ(hud_.get_hp_bar()->get_max(), 80);
    EXPECT_EQ(hud_.get_mp_bar()->get_current(), 20);
    EXPECT_EQ(hud_.get_mp_bar()->get_max(), 60);
    EXPECT_EQ(hud_.get_exp_bar()->get_current(), 90);
    EXPECT_EQ(hud_.get_exp_bar()->get_max(), 100);
    EXPECT_EQ(hud_.get_player_info()->get_name(), "Player2");
    EXPECT_EQ(hud_.get_player_info()->get_level(), 50);
    EXPECT_EQ(hud_.get_player_info()->get_gold(), 99999);
}

TEST_F(HudContainerTest, UpdateFromStats_ZeroMaxHp_HpRatioIsZero) {
    mir2::common::CharacterStats stats;
    stats.hp = 0;
    stats.max_hp = 0;
    stats.mp = 50;
    stats.max_mp = 50;
    stats.experience = 0;
    stats.level = 1;
    stats.gold = 0;

    hud_.update_from_stats(stats, "Ghost");

    EXPECT_FLOAT_EQ(hud_.get_hp_bar()->get_ratio(), 0.0f);
}

// -- update_from_stats with CharacterStats from get_class_base_stats --

TEST_F(HudContainerTest, UpdateFromStats_WarriorBaseStats_CorrectValues) {
    auto stats = mir2::common::get_class_base_stats(mir2::common::CharacterClass::WARRIOR);
    hud_.update_from_stats(stats, "WarriorHero");

    EXPECT_EQ(hud_.get_hp_bar()->get_current(), 150);
    EXPECT_EQ(hud_.get_hp_bar()->get_max(), 150);
    EXPECT_FLOAT_EQ(hud_.get_hp_bar()->get_ratio(), 1.0f);

    EXPECT_EQ(hud_.get_mp_bar()->get_current(), 30);
    EXPECT_EQ(hud_.get_mp_bar()->get_max(), 30);
    EXPECT_FLOAT_EQ(hud_.get_mp_bar()->get_ratio(), 1.0f);

    EXPECT_EQ(hud_.get_player_info()->get_name(), "WarriorHero");
    EXPECT_EQ(hud_.get_player_info()->get_level(), 1);
    EXPECT_EQ(hud_.get_player_info()->get_gold(), 0);
}

TEST_F(HudContainerTest, UpdateFromStats_MageBaseStats_CorrectValues) {
    auto stats = mir2::common::get_class_base_stats(mir2::common::CharacterClass::MAGE);
    hud_.update_from_stats(stats, "MageHero");

    EXPECT_EQ(hud_.get_hp_bar()->get_current(), 80);
    EXPECT_EQ(hud_.get_hp_bar()->get_max(), 80);
    EXPECT_EQ(hud_.get_mp_bar()->get_current(), 100);
    EXPECT_EQ(hud_.get_mp_bar()->get_max(), 100);
}

// -- Layout recalculation after resize --

TEST_F(HudContainerTest, SetScreenSize_ThenUpdate_ValuesPreserved) {
    mir2::common::CharacterStats stats;
    stats.hp = 75;
    stats.max_hp = 100;
    stats.mp = 30;
    stats.max_mp = 50;
    stats.experience = 60;
    stats.level = 15;
    stats.gold = 2000;

    hud_.update_from_stats(stats, "Survivor");
    hud_.set_screen_size(1280, 720);

    // Values should be preserved after resize.
    EXPECT_EQ(hud_.get_hp_bar()->get_current(), 75);
    EXPECT_EQ(hud_.get_hp_bar()->get_max(), 100);
    EXPECT_EQ(hud_.get_player_info()->get_name(), "Survivor");

    // But positions should be recalculated.
    const int expected_x = (1280 - BAR_WIDTH) / 2;
    EXPECT_EQ(hud_.get_hp_bar()->get_bounds().x, expected_x);
}

// -- find_child integration --

TEST_F(HudContainerTest, FindChild_HpBar_ReturnsNonNull) {
    EXPECT_NE(hud_.find_child("status_bar_HP"), nullptr);
}

TEST_F(HudContainerTest, FindChild_MpBar_ReturnsNonNull) {
    EXPECT_NE(hud_.find_child("status_bar_MP"), nullptr);
}

TEST_F(HudContainerTest, FindChild_ExpBar_ReturnsNonNull) {
    EXPECT_NE(hud_.find_child("status_bar_EXP"), nullptr);
}

TEST_F(HudContainerTest, FindChild_PlayerInfo_ReturnsNonNull) {
    EXPECT_NE(hud_.find_child("player_info_panel"), nullptr);
}

TEST_F(HudContainerTest, FindChild_NonExistent_ReturnsNull) {
    EXPECT_EQ(hud_.find_child("does_not_exist"), nullptr);
}

} // namespace mir2::ui::hud
