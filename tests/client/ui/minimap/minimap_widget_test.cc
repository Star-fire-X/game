/**
 * @file minimap_widget_test.cc
 * @brief Unit tests for MinimapWidget.
 */

#include "client/ui/minimap/minimap_widget.h"

#include <gtest/gtest.h>

namespace mir2::ui::minimap {
namespace {

class MinimapWidgetTest : public ::testing::Test {
protected:
    MinimapWidget minimap_;
};

// -- Constants --

TEST(MinimapWidgetConstantsTest, DefaultSize_Is160) {
    EXPECT_EQ(MinimapWidget::kDefaultSize, 160);
}

TEST(MinimapWidgetConstantsTest, EntityMarkerType_Values) {
    EXPECT_EQ(static_cast<uint8_t>(EntityMarkerType::kPlayer), 0);
    EXPECT_EQ(static_cast<uint8_t>(EntityMarkerType::kMonster), 1);
    EXPECT_EQ(static_cast<uint8_t>(EntityMarkerType::kNpc), 2);
    EXPECT_EQ(static_cast<uint8_t>(EntityMarkerType::kPortal), 3);
}

// -- Default state --

TEST_F(MinimapWidgetTest, Constructor_DefaultZoom_Is4) {
    EXPECT_FLOAT_EQ(minimap_.get_zoom_level(), 4.0f);
}

TEST_F(MinimapWidgetTest, Constructor_DefaultVisible) {
    EXPECT_TRUE(minimap_.is_visible());
}

// -- set_map_size --

TEST_F(MinimapWidgetTest, SetMapSize_NoFatalFailure) {
    EXPECT_NO_FATAL_FAILURE(minimap_.set_map_size(100, 200));
}

TEST_F(MinimapWidgetTest, SetMapSize_ZeroDimensions_NoFatalFailure) {
    EXPECT_NO_FATAL_FAILURE(minimap_.set_map_size(0, 0));
}

TEST_F(MinimapWidgetTest, SetMapSize_LargeDimensions_NoFatalFailure) {
    EXPECT_NO_FATAL_FAILURE(minimap_.set_map_size(10000, 10000));
}

// -- set_player_position --

TEST_F(MinimapWidgetTest, SetPlayerPosition_NoFatalFailure) {
    Position pos{50, 75};
    EXPECT_NO_FATAL_FAILURE(minimap_.set_player_position(pos));
}

TEST_F(MinimapWidgetTest, SetPlayerPosition_Origin_NoFatalFailure) {
    Position pos{0, 0};
    EXPECT_NO_FATAL_FAILURE(minimap_.set_player_position(pos));
}

// -- Entity markers --

TEST_F(MinimapWidgetTest, AddEntityMarker_NoFatalFailure) {
    Position pos{10, 20};
    EXPECT_NO_FATAL_FAILURE(
        minimap_.add_entity_marker(1, pos, EntityMarkerType::kPlayer));
}

TEST_F(MinimapWidgetTest, AddEntityMarker_MultipleTypes_NoFatalFailure) {
    minimap_.add_entity_marker(1, {10, 10}, EntityMarkerType::kPlayer);
    minimap_.add_entity_marker(2, {20, 20}, EntityMarkerType::kMonster);
    minimap_.add_entity_marker(3, {30, 30}, EntityMarkerType::kNpc);
    minimap_.add_entity_marker(4, {40, 40}, EntityMarkerType::kPortal);
}

TEST_F(MinimapWidgetTest, AddEntityMarker_OverwritesSameId_NoFatalFailure) {
    minimap_.add_entity_marker(42, {10, 10}, EntityMarkerType::kPlayer);
    minimap_.add_entity_marker(42, {20, 20}, EntityMarkerType::kMonster);
}

TEST_F(MinimapWidgetTest, ClearMarkers_NoFatalFailure) {
    minimap_.add_entity_marker(1, {10, 10}, EntityMarkerType::kPlayer);
    minimap_.add_entity_marker(2, {20, 20}, EntityMarkerType::kMonster);
    EXPECT_NO_FATAL_FAILURE(minimap_.clear_markers());
}

TEST_F(MinimapWidgetTest, ClearMarkers_WhenEmpty_NoFatalFailure) {
    EXPECT_NO_FATAL_FAILURE(minimap_.clear_markers());
}

// -- Zoom level --

TEST_F(MinimapWidgetTest, SetZoomLevel_ValidValue) {
    minimap_.set_zoom_level(3.0f);
    EXPECT_FLOAT_EQ(minimap_.get_zoom_level(), 3.0f);
}

TEST_F(MinimapWidgetTest, SetZoomLevel_MinClamp) {
    minimap_.set_zoom_level(0.0f);
    EXPECT_FLOAT_EQ(minimap_.get_zoom_level(), 1.0f);
}

TEST_F(MinimapWidgetTest, SetZoomLevel_MaxClamp) {
    minimap_.set_zoom_level(20.0f);
    EXPECT_FLOAT_EQ(minimap_.get_zoom_level(), 8.0f);
}

TEST_F(MinimapWidgetTest, SetZoomLevel_NegativeClamp) {
    minimap_.set_zoom_level(-5.0f);
    EXPECT_FLOAT_EQ(minimap_.get_zoom_level(), 1.0f);
}

TEST_F(MinimapWidgetTest, SetZoomLevel_ExactMin) {
    minimap_.set_zoom_level(1.0f);
    EXPECT_FLOAT_EQ(minimap_.get_zoom_level(), 1.0f);
}

TEST_F(MinimapWidgetTest, SetZoomLevel_ExactMax) {
    minimap_.set_zoom_level(8.0f);
    EXPECT_FLOAT_EQ(minimap_.get_zoom_level(), 8.0f);
}

TEST_F(MinimapWidgetTest, SetZoomLevel_HalfStep) {
    minimap_.set_zoom_level(2.5f);
    EXPECT_FLOAT_EQ(minimap_.get_zoom_level(), 2.5f);
}

}  // namespace
}  // namespace mir2::ui::minimap
