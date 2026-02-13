// =============================================================================
// Legend2 HUD - HUD Container
//
// Master container that owns and positions all HUD elements:
//   - HP bar (red)    -- bottom center, top of the bar stack
//   - MP bar (blue)   -- bottom center, middle of the bar stack
//   - EXP bar (yellow)-- bottom center, bottom of the bar stack
//   - Player info     -- top-left corner
//
// Call update_from_stats() each frame (or on stat change) to refresh
// all child widget values from the latest CharacterStats snapshot.
// =============================================================================

#ifndef LEGEND2_UI_HUD_HUD_CONTAINER_H
#define LEGEND2_UI_HUD_HUD_CONTAINER_H

#include "ui/ui_manager.h"
#include "ui/hud/status_bar.h"
#include "ui/hud/player_info_panel.h"
#include "common/types.h"

#include <memory>
#include <string>

namespace mir2::ui::hud {

/// Top-level container for all in-game HUD elements.
///
/// Positioning is calculated relative to a screen size that defaults
/// to 800x600 and can be updated via set_screen_size(). The layout
/// recalculates whenever the screen dimensions change.
class HudContainer : public UIContainer {
public:
    /// Construct the HUD and create all child widgets.
    /// @param screen_width  Initial screen width in pixels
    /// @param screen_height Initial screen height in pixels
    explicit HudContainer(int screen_width = 800, int screen_height = 600);

    ~HudContainer() override = default;

    /// Push new character stats and player name into all HUD widgets.
    /// Call this whenever the local player's stats are received or updated.
    /// @param stats Current character stats snapshot
    /// @param name  Player character name
    void update_from_stats(const mir2::common::CharacterStats& stats,
                           const std::string& name);

    /// Update the screen dimensions and recalculate widget positions.
    /// @param width  New screen width in pixels
    /// @param height New screen height in pixels
    void set_screen_size(int width, int height);

    /// Direct access to individual HUD elements for fine-grained control.
    StatusBar* get_hp_bar() const { return hp_bar_; }
    StatusBar* get_mp_bar() const { return mp_bar_; }
    StatusBar* get_exp_bar() const { return exp_bar_; }
    PlayerInfoPanel* get_player_info() const { return player_info_; }

private:
    /// Recalculate all child widget positions based on current screen size.
    void recalculate_layout();

    int screen_width_;
    int screen_height_;

    // Raw pointers for convenient access; ownership is held by
    // UIContainer::children_ via shared_ptr.
    StatusBar* hp_bar_ = nullptr;
    StatusBar* mp_bar_ = nullptr;
    StatusBar* exp_bar_ = nullptr;
    PlayerInfoPanel* player_info_ = nullptr;

    // Layout constants
    static constexpr int BAR_WIDTH = 300;
    static constexpr int BAR_HEIGHT = 22;
    static constexpr int BAR_SPACING = 2;
    static constexpr int BAR_BOTTOM_MARGIN = 20;
    static constexpr int INFO_PANEL_WIDTH = 160;
    static constexpr int INFO_PANEL_HEIGHT = 72;
    static constexpr int INFO_PANEL_MARGIN = 10;
};

} // namespace mir2::ui::hud

#endif // LEGEND2_UI_HUD_HUD_CONTAINER_H
