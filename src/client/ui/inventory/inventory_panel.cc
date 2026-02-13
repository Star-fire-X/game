// =============================================================================
// Legend2 Inventory Panel - Implementation
// =============================================================================

#include "ui/inventory/inventory_panel.h"

#include <algorithm>
#include <string>

namespace mir2::ui::inventory {

// ---- Anonymous namespace for local colour constants ----
namespace {
using mir2::common::Color;
using mir2::common::Rect;

constexpr Color kPanelBg       {20, 20, 25, 220};
constexpr Color kPanelBorder   {130, 120, 100, 255};
constexpr Color kTitleColor    {230, 210, 160, 255};
constexpr Color kSlotEmpty     {35, 35, 40, 200};
constexpr Color kSlotEmptyBorder{70, 70, 75, 255};
constexpr Color kSlotFilled    {55, 55, 70, 220};
constexpr Color kSlotFilledBorder{100, 100, 110, 255};
constexpr Color kSlotSelected  {220, 180, 60, 255};
constexpr Color kSlotHovered   {160, 160, 180, 255};
constexpr Color kItemIdColor   {200, 200, 210, 255};
constexpr Color kCountColor    {255, 255, 200, 255};
} // namespace

// =============================================================================
// Construction
// =============================================================================

InventoryPanel::InventoryPanel() {
    set_id("inventory_panel");
    set_visible(false); // hidden by default; toggle with 'I' / 'B'
    recalculate_bounds();

    // Initialise every slot to empty (item_id == 0).
    for (auto& item : items_) {
        item = {};
    }
}

// =============================================================================
// Public API
// =============================================================================

void InventoryPanel::update_items(
        const std::vector<mir2::game::handlers::ClientInventoryItem>& items) {
    // Reset all slots first.
    for (auto& stored : items_) {
        stored = {};
    }
    // Copy incoming items into the matching slot positions.
    for (const auto& item : items) {
        if (item.slot < MAX_SLOTS) {
            items_[item.slot] = item;
        }
    }
}

int InventoryPanel::get_slot_at(int mx, int my) const {
    for (int i = 0; i < MAX_SLOTS; ++i) {
        if (slot_rect(i).contains(mx, my)) {
            return i;
        }
    }
    return -1;
}

// =============================================================================
// UIWidget overrides
// =============================================================================

void InventoryPanel::update(float delta_time) {
    (void)delta_time;
}

void InventoryPanel::render(UIRenderer& renderer) {
    if (!is_visible()) {
        return;
    }

    const Rect& bounds = get_bounds();

    // --- Panel background and border ---
    renderer.draw_panel(bounds, kPanelBg, kPanelBorder);

    // --- Title ---
    renderer.draw_text("Inventory",
                       bounds.x + PANEL_PADDING,
                       bounds.y + 4,
                       kTitleColor);

    // --- Slot grid ---
    for (int i = 0; i < MAX_SLOTS; ++i) {
        const Rect sr = slot_rect(i);
        const bool has_item = (items_[i].item_id != 0);
        const bool is_selected = (i == selected_slot_);
        const bool is_hovered  = (i == hovered_slot_);

        // Slot background
        const Color& bg = has_item ? kSlotFilled : kSlotEmpty;
        Color border = has_item ? kSlotFilledBorder : kSlotEmptyBorder;
        if (is_selected) {
            border = kSlotSelected;
        } else if (is_hovered) {
            border = kSlotHovered;
        }

        renderer.draw_panel(sr, bg, border);

        if (!has_item) {
            continue;
        }

        // Draw item_id as a numeric placeholder centred in the slot.
        const std::string id_str = std::to_string(items_[i].item_id);
        renderer.draw_text(id_str,
                           sr.x + 2,
                           sr.y + 2,
                           kItemIdColor);

        // Draw stack count in the bottom-right corner (only when count > 1).
        if (items_[i].count > 1) {
            const std::string count_str = std::to_string(items_[i].count);
            // Approximate right-align: each character ~7px wide
            const int text_w = static_cast<int>(count_str.size()) * 7;
            renderer.draw_text(count_str,
                               sr.x + sr.width - text_w - 1,
                               sr.y + sr.height - 13,
                               kCountColor);
        }
    }
}

bool InventoryPanel::handle_event(const SDL_Event& event) {
    // --- Toggle visibility on key press ---
    if (event.type == SDL_KEYDOWN && event.key.repeat == 0) {
        const SDL_Keycode key = event.key.keysym.sym;
        if (key == SDLK_i || key == SDLK_b) {
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
            return true; // consume when inside panel
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
            // Right-click: use item
            if (slot >= 0 && items_[slot].item_id != 0 && on_item_use) {
                on_item_use(slot);
            }
            return true;
        }

        return true; // consume click inside panel regardless
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

void InventoryPanel::recalculate_bounds() {
    const int grid_width  = COLS * SLOT_SIZE + (COLS - 1) * SLOT_PADDING;
    const int grid_height = ROWS * SLOT_SIZE + (ROWS - 1) * SLOT_PADDING;
    const int panel_width  = grid_width  + PANEL_PADDING * 2;
    const int panel_height = grid_height + PANEL_PADDING * 2 + TITLE_HEIGHT;
    set_size(panel_width, panel_height);
}

Rect InventoryPanel::slot_rect(int index) const {
    const int col = index % COLS;
    const int row = index / COLS;
    const Rect& bounds = get_bounds();
    const int x = bounds.x + PANEL_PADDING + col * (SLOT_SIZE + SLOT_PADDING);
    const int y = bounds.y + PANEL_PADDING + TITLE_HEIGHT
                  + row * (SLOT_SIZE + SLOT_PADDING);
    return {x, y, SLOT_SIZE, SLOT_SIZE};
}

} // namespace mir2::ui::inventory
