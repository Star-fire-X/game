// =============================================================================
// Legend2 UI - Minimap Widget
//
// Features:
//   - Overhead map view with zoom control
//   - Player position indicator (white dot)
//   - Color-coded entity markers (players, monsters, NPCs, portals)
//   - Mouse wheel zoom, 'M' key toggle visibility
// =============================================================================

#ifndef LEGEND2_UI_MINIMAP_MINIMAP_WIDGET_H
#define LEGEND2_UI_MINIMAP_MINIMAP_WIDGET_H

#include "ui/ui_manager.h"
#include "ui/ui_renderer.h"
#include "common/types.h"

#include <cstdint>
#include <unordered_map>

namespace mir2::ui::minimap {

using mir2::common::Position;

/// Types of entity markers shown on the minimap.
enum class EntityMarkerType : uint8_t {
    kPlayer  = 0,  // Other players  (green)
    kMonster = 1,  // Monsters       (red)
    kNpc     = 2,  // NPCs           (yellow)
    kPortal  = 3   // Portals        (blue)
};

/// @brief Minimap widget rendered in the top-right corner of the screen.
///
/// Displays a scaled overhead view of the current map with the local player
/// at the center, surrounded by nearby entity markers.  The zoom level can
/// be adjusted with the mouse wheel, and visibility can be toggled with
/// the 'M' key.
///
/// Usage:
///   - Call set_map_size() when the map changes.
///   - Call set_player_position() every frame to track the camera.
///   - Call add_entity_marker() / clear_markers() each frame from the
///     entity sync loop.
class MinimapWidget : public UIWidget {
public:
    /// Default minimap panel size in pixels.
    static constexpr int kDefaultSize = 160;

    MinimapWidget();
    ~MinimapWidget() override = default;

    // -- Map configuration ----------------------------------------------------

    /// @brief Set the full map dimensions in tile units.
    void set_map_size(int width, int height);

    /// @brief Update the local player position (tile coordinates).
    void set_player_position(const Position& pos);

    // -- Entity markers -------------------------------------------------------

    /// @brief Add or update an entity marker on the minimap.
    void add_entity_marker(uint64_t id, const Position& pos, EntityMarkerType type);

    /// @brief Remove all entity markers (call at the start of each frame
    ///        before re-populating).
    void clear_markers();

    // -- Zoom -----------------------------------------------------------------

    /// @brief Get the current zoom level.
    float get_zoom_level() const { return zoom_level_; }

    /// @brief Set the zoom level (clamped to [kMinZoom, kMaxZoom]).
    void set_zoom_level(float zoom);

    // -- Widget overrides -----------------------------------------------------

    void render(UIRenderer& renderer) override;
    bool handle_event(const SDL_Event& event) override;

protected:
    bool on_key_down(SDL_Scancode scancode, SDL_Keycode keycode, bool repeat) override;
    bool on_mouse_wheel(int x, int y) override;

private:
    /// Return the display color for a given marker type.
    static mir2::common::Color marker_color(EntityMarkerType type);

    /// Convert a world tile position to minimap pixel coordinates relative to
    /// the panel origin.  Returns false if the position is outside the visible
    /// minimap area.
    bool world_to_minimap(const Position& world_pos, int& out_x, int& out_y) const;

    // -- State ----------------------------------------------------------------

    struct EntityMarker {
        Position pos;
        EntityMarkerType type;
    };

    int map_width_  = 0;
    int map_height_ = 0;

    Position player_pos_{0, 0};

    std::unordered_map<uint64_t, EntityMarker> markers_;

    float zoom_level_ = 4.0f;

    static constexpr float kMinZoom = 1.0f;
    static constexpr float kMaxZoom = 8.0f;
    static constexpr float kZoomStep = 0.5f;
};

} // namespace mir2::ui::minimap

#endif // LEGEND2_UI_MINIMAP_MINIMAP_WIDGET_H
