// =============================================================================
// Legend2 Equipment Panel - Implementation
//
// Slot layout (character-silhouette arrangement):
//
//              [Helmet]
//   [Necklace]  [Armor]   [Amulet]
//   [BraceletL] [Belt]    [BraceletR]
//   [RingL]     [Weapon]  [RingR]
//              [Boots]    [Talisman]
//                         [Charm]
// =============================================================================

#include "ui/equipment/equipment_panel.h"

#include <string>

namespace mir2::ui::equipment {

// ---- Anonymous namespace for local colour constants ----
namespace {
using mir2::common::Color;
using mir2::common::Rect;

constexpr Color kPanelBg       {20, 22, 28, 220};
constexpr Color kPanelBorder   {120, 110, 90, 255};
constexpr Color kTitleColor    {230, 210, 160, 255};
constexpr Color kSlotEmpty     {40, 40, 48, 200};
constexpr Color kSlotEmptyBorder{80, 80, 85, 255};
constexpr Color kSlotFilled    {60, 65, 80, 220};
constexpr Color kSlotFilledBorder{110, 110, 120, 255};
constexpr Color kSlotSelected  {220, 180, 60, 255};
constexpr Color kSlotHovered   {160, 160, 180, 255};
constexpr Color kItemIdColor   {200, 200, 210, 255};
constexpr Color kLabelColor    {140, 140, 150, 200};
constexpr Color kSilhouetteColor{30, 30, 38, 180};
constexpr Color kSilhouetteBorder{50, 50, 58, 200};
} // namespace

// =============================================================================
// Slot layout table
//
// Offsets are relative to panel top-left. The Y values include room for a
// title bar (TITLE_HEIGHT). The layout visually centres slots around a
// character silhouette.
//
// Column centres (approximate): left=18, centre=92, right=166
// Row starts (below title): row0=30, row1=74, row2=118, row3=162, row4=206,
//                            row5=250
// =============================================================================

// static
const std::array<EquipmentPanel::SlotLayout, 13> EquipmentPanel::kSlotLayouts = {{
    // EquipSlot::WEAPON (0)       -- centre, row 3
    { 92,  162, "Weapon" },
    // EquipSlot::ARMOR (1)        -- centre, row 1
    { 92,   74, "Armor" },
    // EquipSlot::HELMET (2)       -- centre, row 0
    { 92,   30, "Helmet" },
    // EquipSlot::BOOTS (3)        -- centre, row 4
    { 92,  206, "Boots" },
    // EquipSlot::RING_LEFT (4)    -- left, row 3
    { 18,  162, "RingL" },
    // EquipSlot::RING_RIGHT (5)   -- right, row 3
    {166,  162, "RingR" },
    // EquipSlot::NECKLACE (6)     -- left, row 1
    { 18,   74, "Necklace" },
    // EquipSlot::BRACELET_LEFT (7) -- left, row 2
    { 18,  118, "BraceletL" },
    // EquipSlot::BRACELET_RIGHT (8)-- right, row 2
    {166,  118, "BraceletR" },
    // EquipSlot::BELT (9)         -- centre, row 2
    { 92,  118, "Belt" },
    // EquipSlot::AMULET (10)      -- right, row 1
    {166,   74, "Amulet" },
    // EquipSlot::TALISMAN (11)    -- right, row 4
    {166,  206, "Talisman" },
    // EquipSlot::CHARM (12)       -- right, row 5
    {166,  250, "Charm" },
}};

// =============================================================================
// Construction
// =============================================================================

EquipmentPanel::EquipmentPanel() {
    set_id("equipment_panel");
    set_visible(false); // hidden by default; toggle with 'C'
    set_size(PANEL_WIDTH, PANEL_HEIGHT);

    // Initialise every slot to empty (item_id == 0).
    for (auto& item : items_) {
        item = {};
    }
}

// =============================================================================
// Public API
// =============================================================================

void EquipmentPanel::update_equipment(
        uint8_t slot,
        const mir2::game::handlers::ClientInventoryItem& item) {
    if (slot < EQUIP_SLOT_COUNT) {
        items_[slot] = item;
    }
}

void EquipmentPanel::clear_slot(uint8_t slot) {
    if (slot < EQUIP_SLOT_COUNT) {
        items_[slot] = {};
    }
}

// =============================================================================
// UIWidget overrides
// =============================================================================

void EquipmentPanel::update(float delta_time) {
    (void)delta_time;
}

void EquipmentPanel::render(UIRenderer& renderer) {
    if (!is_visible()) {
        return;
    }

    const Rect& bounds = get_bounds();

    // --- Panel background and border ---
    renderer.draw_panel(bounds, kPanelBg, kPanelBorder);

    // --- Title ---
    renderer.draw_text("Equipment",
                       bounds.x + 8,
                       bounds.y + 4,
                       kTitleColor);

    // --- Silhouette placeholder (simple filled rectangle behind the grid) ---
    {
        const Rect sil = {
            bounds.x + 70,
            bounds.y + TITLE_HEIGHT + 26,
            80,
            230
        };
        renderer.draw_panel(sil, kSilhouetteColor, kSilhouetteBorder);
    }

    // --- Equipment slots ---
    for (int i = 0; i < EQUIP_SLOT_COUNT; ++i) {
        const Rect sr = slot_rect(i);
        const bool has_item   = (items_[i].item_id != 0);
        const bool is_selected = (i == selected_slot_);
        const bool is_hovered  = (i == hovered_slot_);

        // Slot background and border
        const Color& bg = has_item ? kSlotFilled : kSlotEmpty;
        Color border = has_item ? kSlotFilledBorder : kSlotEmptyBorder;
        if (is_selected) {
            border = kSlotSelected;
        } else if (is_hovered) {
            border = kSlotHovered;
        }

        renderer.draw_panel(sr, bg, border);

        if (has_item) {
            // Draw item_id as a numeric placeholder centred in the slot.
            const std::string id_str = std::to_string(items_[i].item_id);
            renderer.draw_text(id_str,
                               sr.x + 2,
                               sr.y + 2,
                               kItemIdColor);
        } else {
            // Draw abbreviated slot label for empty slots.
            const char* label = kSlotLayouts[i].label;
            renderer.draw_text(label,
                               sr.x + 2,
                               sr.y + SLOT_SIZE / 2 - 5,
                               kLabelColor);
        }
    }
}

bool EquipmentPanel::handle_event(const SDL_Event& event) {
    // --- Toggle visibility on 'C' key ---
    if (event.type == SDL_KEYDOWN && event.key.repeat == 0) {
        if (event.key.keysym.sym == SDLK_c) {
            set_visible(!is_visible());
            if (!is_visible()) {
                selected_slot_ = -1;
                hovered_slot_  = -1;
            }
            return true;
        }
    }

    // All remaining interactions require the panel to be visible.
    if (!is_visible()) {
        return false;
    }

    // --- Mouse motion: update hovered slot ---
    if (event.type == SDL_MOUSEMOTION) {
        const int mx = event.motion.x;
        const int my = event.motion.y;
        if (contains_point(mx, my)) {
            hovered_slot_ = get_slot_at(mx, my);
            return true;
        }
        hovered_slot_ = -1;
        return false;
    }

    // --- Mouse button down ---
    if (event.type == SDL_MOUSEBUTTONDOWN) {
        const int mx = event.button.x;
        const int my = event.button.y;

        if (!contains_point(mx, my)) {
            return false;
        }

        const int slot = get_slot_at(mx, my);

        if (event.button.button == SDL_BUTTON_LEFT) {
            // Left-click: select / deselect
            if (slot >= 0 && items_[slot].item_id != 0) {
                selected_slot_ = (selected_slot_ == slot) ? -1 : slot;
            } else {
                selected_slot_ = -1;
            }
            return true;
        }

        if (event.button.button == SDL_BUTTON_RIGHT) {
            // Right-click: unequip item
            if (slot >= 0 && items_[slot].item_id != 0 && on_unequip) {
                on_unequip(static_cast<uint8_t>(slot));
            }
            return true;
        }

        return true; // consume click inside panel
    }

    // --- Escape key: close panel ---
    if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE) {
        set_visible(false);
        selected_slot_ = -1;
        hovered_slot_  = -1;
        return true;
    }

    return false;
}

// =============================================================================
// Private helpers
// =============================================================================

Rect EquipmentPanel::slot_rect(int index) const {
    if (index < 0 || index >= EQUIP_SLOT_COUNT) {
        return {0, 0, 0, 0};
    }

    const Rect& bounds = get_bounds();
    const auto& layout = kSlotLayouts[index];
    return {
        bounds.x + layout.offset_x,
        bounds.y + TITLE_HEIGHT + layout.offset_y,
        SLOT_SIZE,
        SLOT_SIZE
    };
}

int EquipmentPanel::get_slot_at(int mx, int my) const {
    for (int i = 0; i < EQUIP_SLOT_COUNT; ++i) {
        if (slot_rect(i).contains(mx, my)) {
            return i;
        }
    }
    return -1;
}

} // namespace mir2::ui::equipment
