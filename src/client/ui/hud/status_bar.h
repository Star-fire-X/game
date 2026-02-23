// =============================================================================
// Legend2 HUD - Status Bar Widget
//
// A reusable progress bar widget for displaying resource values such as
// HP, MP, and EXP. Renders a background rect, a proportionally filled
// rect, a border outline, and a centered "current/max" text overlay.
// =============================================================================

#ifndef LEGEND2_UI_HUD_STATUS_BAR_H
#define LEGEND2_UI_HUD_STATUS_BAR_H

#include "ui/ui_manager.h"
#include "ui/ui_renderer.h"
#include "common/types.h"

#include <string>

namespace mir2::ui::hud {

using mir2::common::Color;
using mir2::common::Rect;

/// A horizontal bar that displays a current/max resource value.
///
/// The bar renders four layers (back to front):
///   1. Background fill (full width)
///   2. Proportional fill (width scaled by current/max ratio)
///   3. Border outline (full width)
///   4. Centered text label showing "current / max"
class StatusBar : public UIWidget {
public:
    /// Construct a status bar with visual styling.
    /// @param label     Display label (e.g. "HP", "MP", "EXP")
    /// @param fill_color Color for the filled portion
    /// @param bg_color   Color for the background
    /// @param border_color Color for the border outline
    StatusBar(const std::string& label,
              const Color& fill_color,
              const Color& bg_color,
              const Color& border_color);

    ~StatusBar() override = default;

    /// Update the current and maximum values displayed by the bar.
    /// Values are clamped so that current never exceeds max and
    /// neither value goes below zero.
    /// @param current Current resource value
    /// @param max     Maximum resource value
    void set_value(int current, int max);

    /// Get the fill ratio as a float in the range [0.0, 1.0].
    /// Returns 0.0 when max is zero or negative.
    float get_ratio() const;

    /// Get the current value.
    int get_current() const { return current_; }

    /// Get the maximum value.
    int get_max() const { return max_; }

    /// Get the display label.
    const std::string& get_label() const { return label_; }

    // UIWidget interface
    void render(UIRenderer& renderer) override;

private:
    std::string label_;
    Color fill_color_;
    Color bg_color_;
    Color border_color_;

    int current_ = 0;
    int max_ = 0;
};

} // namespace mir2::ui::hud

#endif // LEGEND2_UI_HUD_STATUS_BAR_H
