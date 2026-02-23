/**
 * @file equipment_panel_test.cc
 * @brief Unit tests for EquipmentPanel widget.
 */

#include "client/ui/equipment/equipment_panel.h"
#include "client/handlers/item_handler.h"

#include <gtest/gtest.h>

namespace mir2::ui::equipment {
namespace {

using mir2::game::handlers::ClientInventoryItem;

class EquipmentPanelTest : public ::testing::Test {
protected:
    EquipmentPanel panel_;
};

// -- Constants --

TEST(EquipmentPanelConstantsTest, SlotCount_Is13) {
    EXPECT_EQ(EquipmentPanel::EQUIP_SLOT_COUNT, 13);
}

// -- Default state --

TEST_F(EquipmentPanelTest, Constructor_DefaultSelection) {
    EXPECT_EQ(panel_.get_selected_slot(), -1);
}

TEST_F(EquipmentPanelTest, Constructor_DefaultNotVisible) {
    EXPECT_FALSE(panel_.is_visible());
}

// -- update_equipment --

TEST_F(EquipmentPanelTest, UpdateEquipment_ValidSlot_NoFatalFailure) {
    ClientInventoryItem item{0, 5001, 1, 100};
    EXPECT_NO_FATAL_FAILURE(panel_.update_equipment(0, item));
}

TEST_F(EquipmentPanelTest, UpdateEquipment_AllSlots_NoFatalFailure) {
    for (uint8_t slot = 0; slot < 13; ++slot) {
        ClientInventoryItem item{slot, static_cast<uint32_t>(1000 + slot), 1, 50};
        EXPECT_NO_FATAL_FAILURE(panel_.update_equipment(slot, item));
    }
}

TEST_F(EquipmentPanelTest, UpdateEquipment_OutOfRange_Ignored) {
    ClientInventoryItem item{99, 5001, 1, 100};
    EXPECT_NO_FATAL_FAILURE(panel_.update_equipment(99, item));
    EXPECT_NO_FATAL_FAILURE(panel_.update_equipment(255, item));
}

// -- clear_slot --

TEST_F(EquipmentPanelTest, ClearSlot_ValidSlot_NoFatalFailure) {
    ClientInventoryItem item{0, 5001, 1, 100};
    panel_.update_equipment(0, item);
    EXPECT_NO_FATAL_FAILURE(panel_.clear_slot(0));
}

TEST_F(EquipmentPanelTest, ClearSlot_OutOfRange_Ignored) {
    EXPECT_NO_FATAL_FAILURE(panel_.clear_slot(99));
    EXPECT_NO_FATAL_FAILURE(panel_.clear_slot(255));
}

TEST_F(EquipmentPanelTest, ClearSlot_AlreadyEmpty_NoFatalFailure) {
    EXPECT_NO_FATAL_FAILURE(panel_.clear_slot(0));
}

// -- clear_selection --

TEST_F(EquipmentPanelTest, ClearSelection_ResetsToMinusOne) {
    panel_.clear_selection();
    EXPECT_EQ(panel_.get_selected_slot(), -1);
}

// -- Callback --

TEST_F(EquipmentPanelTest, OnUnequip_DefaultIsNull) {
    EXPECT_FALSE(panel_.on_unequip);
}

TEST_F(EquipmentPanelTest, OnUnequip_CanBeSet) {
    uint8_t called_slot = 255;
    panel_.on_unequip = [&](uint8_t slot) { called_slot = slot; };
    EXPECT_TRUE(panel_.on_unequip != nullptr);

    panel_.on_unequip(3);
    EXPECT_EQ(called_slot, 3);
}

}  // namespace
}  // namespace mir2::ui::equipment
