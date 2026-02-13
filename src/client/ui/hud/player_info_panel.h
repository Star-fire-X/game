// =============================================================================
// Legend2 HUD - Player Info Panel
//
// A compact panel that displays the player's name, level, and gold count.
// Typically positioned at the top-left corner of the screen.
// =============================================================================

#ifndef LEGEND2_UI_HUD_PLAYER_INFO_PANEL_H
#define LEGEND2_UI_HUD_PLAYER_INFO_PANEL_H

#include "ui/ui_manager.h"
#include "ui/ui_renderer.h"
#include "common/types.h"

#include <string>

namespace mir2::ui::hud {

using mir2::common::Color;
using mir2::common::Rect;

/// Panel widget that shows the player's identity and wealth at a glance.
///
/// Layout (top to bottom within the panel bounds):
///   - Player name (white, default font)
///   - "Lv. X" level indicator
///   - Gold amount with coin icon placeholder
class PlayerInfoPanel : public UIWidget {
public:
    PlayerInfoPanel();
    ~PlayerInfoPanel() override = default;

    /// Update the displayed player information.
    /// @param name  Player character name
    /// @param level Current character level
    /// @param gold  Current gold amount
    void set_player_info(const std::string& name, int level, int gold);

    /// Get the currently displayed player name.
    const std::string& get_name() const { return name_; }

    /// Get the currently displayed level.
    int get_level() const { return level_; }

    /// Get the currently displayed gold amount.
    int get_gold() const { return gold_; }

    // UIWidget interface
    void render(UIRenderer& renderer) override;

private:
    std::string name_;
    int level_ = 1;
    int gold_ = 0;

    // Visual constants
    static constexpr int PADDING = 8;
    static constexpr int LINE_SPACING = 4;
};

} // namespace mir2::ui::hud

#endif // LEGEND2_UI_HUD_PLAYER_INFO_PANEL_H
