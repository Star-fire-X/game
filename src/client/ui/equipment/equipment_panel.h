// =============================================================================
// Legend2 Equipment Panel
//
// Features:
//   - Displays the 13 equipment slots arranged in a character-silhouette layout
//   - Supports slot selection and right-click unequip
//   - Toggle visibility with 'C' key
//   - Renders item_id as a numeric placeholder inside each slot
// =============================================================================

#ifndef LEGEND2_UI_EQUIPMENT_EQUIPMENT_PANEL_H
#define LEGEND2_UI_EQUIPMENT_EQUIPMENT_PANEL_H

#include "client/handlers/item_handler.h"
#include "common/enums.h"
#include "ui/ui_manager.h"
#include "ui/ui_renderer.h"
#include "common/types.h"

#include <array>
#include <cstdint>
#include <functional>
#include <string>

namespace mir2::ui::equipment {

/// Equipment panel widget.
///
/// Renders 13 equipment slots arranged around a character silhouette. Each
/// slot maps to a value of mir2::common::EquipSlot. Left-click selects a
/// slot and right-click fires the on_unequip callback. The panel is toggled
/// by pressing 'C'.
class EquipmentPanel : public mir2::ui::UIWidget {
public:
    static constexpr int EQUIP_SLOT_COUNT =
        static_cast<int>(mir2::common::EquipSlot::MAX_SLOTS);

    EquipmentPanel();
    ~EquipmentPanel() override = default;

    // Non-copyable
    EquipmentPanel(const EquipmentPanel&) = delete;
    EquipmentPanel& operator=(const EquipmentPanel&) = delete;

    /// Set or update the item in a single equipment slot.
    void update_equipment(uint8_t slot,
                          const mir2::game::handlers::ClientInventoryItem& item);

    /// Clear a single equipment slot (remove equipped item).
    void clear_slot(uint8_t slot);

    /// Get the currently selected slot index, or -1 if nothing selected.
    int get_selected_slot() const { return selected_slot_; }

    /// Clear the current selection.
    void clear_selection() { selected_slot_ = -1; }

    // -- Callback --

    /// Fired when the player right-clicks an occupied equipment slot.
    /// Parameter: EquipSlot index (0..12).
    std::function<void(uint8_t slot)> on_unequip;

    // -- UIWidget interface --
    void update(float delta_time) override;
    void render(UIRenderer& renderer) override;
    bool handle_event(const SDL_Event& event) override;

private:
    // Slot layout constants
    static constexpr int SLOT_SIZE    = 36;
    static constexpr int PANEL_WIDTH  = 220;
    static constexpr int PANEL_HEIGHT = 310;
    static constexpr int TITLE_HEIGHT = 22;

    /// Descriptor for one slot's position and label in the layout.
    struct SlotLayout {
        int offset_x;       // x offset from panel left
        int offset_y;       // y offset from panel top (below title)
        const char* label;  // short human-readable name
    };

    /// Static table of slot positions for the silhouette arrangement.
    static const std::array<SlotLayout, 13> kSlotLayouts;

    /// Return the screen-space rect for a given equipment slot.
    mir2::common::Rect slot_rect(int index) const;

    /// Return the slot index at the given screen coordinate, or -1.
    int get_slot_at(int mx, int my) const;

    // Item data
    std::array<mir2::game::handlers::ClientInventoryItem, 13> items_{};

    // Interaction state
    int selected_slot_ = -1;
    int hovered_slot_  = -1;
};

} // namespace mir2::ui::equipment

#endif // LEGEND2_UI_EQUIPMENT_EQUIPMENT_PANEL_H
