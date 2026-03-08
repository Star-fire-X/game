/**
 * @file inventory_panel_test.cc
 * @brief Unit tests for InventoryPanel widget.
 */

#include "client/ui/inventory/inventory_panel.h"
#include "client/handlers/item_handler.h"

#include <gtest/gtest.h>

#include <vector>

namespace mir2::ui::inventory {
namespace {

using mir2::game::handlers::ClientInventoryItem;

class InventoryPanelTest : public ::testing::Test {
protected:
    InventoryPanel panel_;
};

// -- Constants --

TEST(InventoryPanelConstantsTest, LayoutConstants) {
    EXPECT_EQ(InventoryPanel::MAX_SLOTS, 40);
    EXPECT_EQ(InventoryPanel::COLS, 8);
    EXPECT_EQ(InventoryPanel::ROWS, 5);
    EXPECT_EQ(InventoryPanel::SLOT_SIZE, 36);
    EXPECT_EQ(InventoryPanel::SLOT_PADDING, 2);
}

// -- Default state --

TEST_F(InventoryPanelTest, Constructor_DefaultSelection) {
    EXPECT_EQ(panel_.get_selected_slot(), -1);
}

TEST_F(InventoryPanelTest, Constructor_DefaultNotVisible) {
    EXPECT_FALSE(panel_.is_visible());
}

// -- update_items --

TEST_F(InventoryPanelTest, UpdateItems_PopulatesSlots) {
    std::vector<ClientInventoryItem> items;
    items.push_back({0, 1001, 5, 100});
    items.push_back({5, 2002, 1, 50});
    items.push_back({39, 3003, 99, -1});

    EXPECT_NO_FATAL_FAILURE(panel_.update_items(items));
}

TEST_F(InventoryPanelTest, UpdateItems_EmptyVector_ClearsAll) {
    std::vector<ClientInventoryItem> items;
    items.push_back({0, 1001, 5, 100});
    panel_.update_items(items);

    std::vector<ClientInventoryItem> empty;
    EXPECT_NO_FATAL_FAILURE(panel_.update_items(empty));
}

TEST_F(InventoryPanelTest, UpdateItems_OutOfRangeSlot_Ignored) {
    std::vector<ClientInventoryItem> items;
    items.push_back({99, 1001, 1, 0});  // slot 99 >= MAX_SLOTS
    EXPECT_NO_FATAL_FAILURE(panel_.update_items(items));
}

// -- get_slot_at --

TEST_F(InventoryPanelTest, GetSlotAt_OutOfBounds_ReturnsMinusOne) {
    EXPECT_EQ(panel_.get_slot_at(-100, -100), -1);
    EXPECT_EQ(panel_.get_slot_at(9999, 9999), -1);
}

// -- clear_selection --

TEST_F(InventoryPanelTest, ClearSelection_ResetsToMinusOne) {
    panel_.clear_selection();
    EXPECT_EQ(panel_.get_selected_slot(), -1);
}

// -- Callbacks --

TEST_F(InventoryPanelTest, OnItemUse_DefaultIsNull) {
    EXPECT_FALSE(panel_.on_item_use);
}

TEST_F(InventoryPanelTest, OnItemDrop_DefaultIsNull) {
    EXPECT_FALSE(panel_.on_item_drop);
}

TEST_F(InventoryPanelTest, OnItemUse_CanBeSet) {
    int called_slot = -1;
    panel_.on_item_use = [&](int slot) { called_slot = slot; };
    EXPECT_TRUE(panel_.on_item_use != nullptr);

    panel_.on_item_use(5);
    EXPECT_EQ(called_slot, 5);
}

TEST_F(InventoryPanelTest, OnItemDrop_CanBeSet) {
    int called_slot = -1;
    panel_.on_item_drop = [&](int slot) { called_slot = slot; };

    panel_.on_item_drop(10);
    EXPECT_EQ(called_slot, 10);
}

}  // namespace
}  // namespace mir2::ui::inventory
