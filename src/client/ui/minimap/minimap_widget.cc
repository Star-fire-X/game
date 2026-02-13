// =============================================================================
// Legend2 UI - Minimap Widget Implementation
// =============================================================================

#include "ui/minimap/minimap_widget.h"

#include <algorithm>
#include <cmath>

#include "common/types.h"

namespace mir2::ui::minimap {

using mir2::common::Color;
using mir2::common::Rect;

// =============================================================================
// Color constants
// =============================================================================

namespace {

constexpr Color kPanelBackground{10, 10, 10, 180};
constexpr Color kPanelBorder{60, 60, 60, 200};
constexpr Color kMapOutline{80, 80, 80, 200};
constexpr Color kPlayerDot{255, 255, 255, 255};      // White (local player)

// Marker colors matching EntityMarkerType
constexpr Color kMarkerPlayer{80, 220, 80, 255};     // Green (other players)
constexpr Color kMarkerMonster{220, 60, 60, 255};    // Red
constexpr Color kMarkerNpc{240, 220, 60, 255};       // Yellow
constexpr Color kMarkerPortal{80, 130, 240, 255};    // Blue

constexpr int kPlayerDotSize = 3;   // pixels
constexpr int kMarkerDotSize = 2;   // pixels
constexpr int kPadding = 4;

} // namespace

// =============================================================================
// Construction
// =============================================================================

MinimapWidget::MinimapWidget() {
    set_id("minimap");
    // Default position: top-right corner placeholder.
    // The caller should reposition based on actual screen width, e.g.:
    //   minimap.set_bounds({screen_w - 164, 4, 160, 160});
    set_bounds({636, 4, kDefaultSize, kDefaultSize});
}

// =============================================================================
// Map configuration
// =============================================================================

void MinimapWidget::set_map_size(int width, int height) {
    map_width_ = width;
    map_height_ = height;
}

void MinimapWidget::set_player_position(const Position& pos) {
    player_pos_ = pos;
}

// =============================================================================
// Entity markers
// =============================================================================

void MinimapWidget::add_entity_marker(uint64_t id, const Position& pos, EntityMarkerType type) {
    markers_[id] = EntityMarker{pos, type};
}

void MinimapWidget::clear_markers() {
    markers_.clear();
}

// =============================================================================
// Zoom
// =============================================================================

void MinimapWidget::set_zoom_level(float zoom) {
    zoom_level_ = std::clamp(zoom, kMinZoom, kMaxZoom);
}

// =============================================================================
// Rendering
// =============================================================================

void MinimapWidget::render(UIRenderer& renderer) {
    if (!is_visible()) {
        return;
    }

    const Rect& bounds = get_bounds();

    // -- Background panel -----------------------------------------------------
    renderer.draw_panel(bounds, kPanelBackground, kPanelBorder);

    // The drawable area inside the panel (excluding padding)
    const int draw_x = bounds.x + kPadding;
    const int draw_y = bounds.y + kPadding;
    const int draw_w = bounds.width - kPadding * 2;
    const int draw_h = bounds.height - kPadding * 2;

    // -- Map boundary outline -------------------------------------------------
    // If map size is configured, draw a proportional outline of the full map
    // centered within the drawable area.
    if (map_width_ > 0 && map_height_ > 0) {
        // Map outline: scale the full map into the drawable area, preserving
        // aspect ratio, so the user can see where they are relative to the
        // whole map.
        const float map_aspect = static_cast<float>(map_width_) / static_cast<float>(map_height_);
        int outline_w, outline_h;
        if (map_aspect >= 1.0f) {
            outline_w = draw_w;
            outline_h = static_cast<int>(static_cast<float>(draw_w) / map_aspect);
        } else {
            outline_h = draw_h;
            outline_w = static_cast<int>(static_cast<float>(draw_h) * map_aspect);
        }
        // Center the outline
        const int outline_x = draw_x + (draw_w - outline_w) / 2;
        const int outline_y = draw_y + (draw_h - outline_h) / 2;

        const Rect outline_rect{outline_x, outline_y, outline_w, outline_h};
        renderer.draw_panel(outline_rect, Color{0, 0, 0, 0}, kMapOutline);

        // -- Entity markers ---------------------------------------------------
        // Only draw markers that fall within the visible minimap area.
        for (const auto& [id, marker] : markers_) {
            int mx, my;
            if (world_to_minimap(marker.pos, mx, my)) {
                const int px = draw_x + mx;
                const int py = draw_y + my;

                // Cull markers outside the drawable area
                if (px < draw_x || px >= draw_x + draw_w ||
                    py < draw_y || py >= draw_y + draw_h) {
                    continue;
                }

                const Color color = marker_color(marker.type);
                const Rect dot{px - kMarkerDotSize / 2,
                               py - kMarkerDotSize / 2,
                               kMarkerDotSize,
                               kMarkerDotSize};
                renderer.draw_panel(dot, color, color);
            }
        }

        // -- Player dot (drawn last so it is on top) --------------------------
        {
            // Player is always at the center of the minimap viewport.
            const int center_x = draw_x + draw_w / 2;
            const int center_y = draw_y + draw_h / 2;

            const Rect player_rect{center_x - kPlayerDotSize / 2,
                                   center_y - kPlayerDotSize / 2,
                                   kPlayerDotSize,
                                   kPlayerDotSize};
            renderer.draw_panel(player_rect, kPlayerDot, kPlayerDot);
        }

        // -- Coordinate label -------------------------------------------------
        // Show the player tile coordinate at the bottom of the minimap.
        const std::string coord_text =
            "(" + std::to_string(player_pos_.x) + ", " + std::to_string(player_pos_.y) + ")";
        renderer.draw_text(coord_text,
                           draw_x + 2,
                           draw_y + draw_h - 14,
                           Color{180, 180, 180, 200});
    } else {
        // No map loaded -- show placeholder text.
        renderer.draw_text("No Map",
                           draw_x + draw_w / 2 - 18,
                           draw_y + draw_h / 2 - 6,
                           Color{120, 120, 120, 180});
    }
}

// =============================================================================
// Event handling
// =============================================================================

bool MinimapWidget::handle_event(const SDL_Event& event) {
    if (!is_enabled()) {
        return false;
    }
    return dispatch_event(event);
}

bool MinimapWidget::on_key_down(SDL_Scancode /*scancode*/, SDL_Keycode keycode, bool /*repeat*/) {
    // 'M' toggles minimap visibility
    if (keycode == SDLK_m) {
        set_visible(!is_visible());
        return true;
    }
    return false;
}

bool MinimapWidget::on_mouse_wheel(int /*x*/, int y) {
    if (!is_visible()) {
        return false;
    }

    // Scroll up = zoom in (larger zoom_level_ means more pixels per tile),
    // scroll down = zoom out.
    const float new_zoom = zoom_level_ + static_cast<float>(y) * kZoomStep;
    set_zoom_level(new_zoom);
    return true;
}

// =============================================================================
// Helpers
// =============================================================================

Color MinimapWidget::marker_color(EntityMarkerType type) {
    switch (type) {
        case EntityMarkerType::kPlayer:  return kMarkerPlayer;
        case EntityMarkerType::kMonster: return kMarkerMonster;
        case EntityMarkerType::kNpc:     return kMarkerNpc;
        case EntityMarkerType::kPortal:  return kMarkerPortal;
    }
    return kMarkerPlayer;
}

bool MinimapWidget::world_to_minimap(const Position& world_pos, int& out_x, int& out_y) const {
    const Rect& bounds = get_bounds();
    const int draw_w = bounds.width - kPadding * 2;
    const int draw_h = bounds.height - kPadding * 2;

    // The minimap is centered on the player.  Compute the tile offset from
    // the player and convert to pixel offset using zoom_level_.
    const float dx = static_cast<float>(world_pos.x - player_pos_.x);
    const float dy = static_cast<float>(world_pos.y - player_pos_.y);

    // Pixel offset from the center of the drawable area
    const float px = dx * zoom_level_;
    const float py = dy * zoom_level_;

    out_x = draw_w / 2 + static_cast<int>(std::round(px));
    out_y = draw_h / 2 + static_cast<int>(std::round(py));

    // Return true if the position is within the visible minimap area
    // (with a small margin for the dot size).
    const int margin = kMarkerDotSize;
    return out_x >= -margin && out_x < draw_w + margin &&
           out_y >= -margin && out_y < draw_h + margin;
}

} // namespace mir2::ui::minimap
