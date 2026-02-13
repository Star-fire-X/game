// =============================================================================
// Legend2 Inventory Panel
//
// Features:
//   - Displays player inventory as an 8x5 slot grid (40 slots total)
//   - Supports item selection, use, and drop via mouse interaction
//   - Toggle visibility with 'I' or 'B' key
//   - Renders item_id as a numeric placeholder and item count overlay
// =============================================================================

#ifndef LEGEND2_UI_INVENTORY_INVENTORY_PANEL_H
#define LEGEND2_UI_INVENTORY_INVENTORY_PANEL_H

#include "client/handlers/item_handler.h"
#include "ui/ui_manager.h"
#include "ui/ui_renderer.h"
#include "common/types.h"

#include <array>
#include <cstdint>
#include <functional>
#include <vector>

namespace mir2::ui::inventory {

/// Inventory panel widget.
///
/// Displays a grid of item slots (8 columns x 5 rows) where the player can
/// view, select, use, and drop items. Left-click selects a slot, right-click
/// triggers the on_item_use callback, and the panel visibility is toggled by
/// pressing 'I' or 'B'.
class InventoryPanel : public UIWidget {
public:
    // Layout constants
    static constexpr int MAX_SLOTS   = 40;
    static constexpr int COLS        = 8;
    static constexpr int ROWS        = 5;
    static constexpr int SLOT_SIZE   = 36;
    static constexpr int SLOT_PADDING = 2;

    InventoryPanel();
    ~InventoryPanel() override = default;

    // Non-copyable
    InventoryPanel(const InventoryPanel&) = delete;
    InventoryPanel& operator=(const InventoryPanel&) = delete;

    /// Replace all inventory item data with a fresh snapshot from the server.
    void update_items(const std::vector<mir2::game::handlers::ClientInventoryItem>& items);

    /// Return the slot index at the given screen coordinate, or -1 if none.
    int get_slot_at(int mx, int my) const;

    /// Get the currently selected slot index (-1 if nothing selected).
    int get_selected_slot() const { return selected_slot_; }

    /// Clear the current selection.
    void clear_selection() { selected_slot_ = -1; }

    // -- Callbacks --

    /// Called when the player right-clicks a slot that contains an item.
    /// Parameter: slot index.
    std::function<void(int slot)> on_item_use;

    /// Called when the player requests to drop an item.
    /// Parameter: slot index.
    std::function<void(int slot)> on_item_drop;

    // -- UIWidget interface --
    void update(float delta_time) override;
    void render(UIRenderer& renderer) override;
    bool handle_event(const SDL_Event& event) override;

private:
    // Padding inside the panel border around the slot grid
    static constexpr int PANEL_PADDING = 8;
    // Title bar height above the grid
    static constexpr int TITLE_HEIGHT = 22;

    /// Compute the bounds of the panel from the current position.
    void recalculate_bounds();

    /// Get the screen-space rectangle for a given slot index.
    mir2::common::Rect slot_rect(int index) const;

    // Item data
    std::array<mir2::game::handlers::ClientInventoryItem, MAX_SLOTS> items_{};

    // Interaction state
    int selected_slot_ = -1;
    int hovered_slot_  = -1;
};

} // namespace mir2::ui::inventory

#endif // LEGEND2_UI_INVENTORY_INVENTORY_PANEL_H
