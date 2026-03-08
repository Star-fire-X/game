// =============================================================================
// Legend2 HUD - Status Bar Widget (Implementation)
//
// Draws a layered bar: background -> proportional fill -> border -> text.
// =============================================================================

#include "ui/hud/status_bar.h"

#include <algorithm>
#include <string>

namespace mir2::ui::hud {

namespace {

/// Text color for the value overlay displayed on top of the bar.
constexpr Color kValueTextColor{255, 255, 255, 255};

/// Shadow color drawn behind the text for readability against bright fills.
constexpr Color kValueTextShadow{0, 0, 0, 180};

} // namespace

StatusBar::StatusBar(const std::string& label,
                     const Color& fill_color,
                     const Color& bg_color,
                     const Color& border_color)
    : label_(label)
    , fill_color_(fill_color)
    , bg_color_(bg_color)
    , border_color_(border_color)
{
    set_id("status_bar_" + label);
}

void StatusBar::set_value(int current, int max) {
    max_ = std::max(0, max);
    current_ = std::clamp(current, 0, max_);
}

float StatusBar::get_ratio() const {
    if (max_ <= 0) {
        return 0.0f;
    }
    return std::clamp(static_cast<float>(current_) / static_cast<float>(max_),
                      0.0f, 1.0f);
}

void StatusBar::render(UIRenderer& renderer) {
    if (!is_visible()) {
        return;
    }

    const Rect& bounds = get_bounds();

    // Layer 1: Background fill (full bar width).
    // Using draw_panel with the same color for both bg and border draws a
    // solid filled rectangle; the border will be overdrawn in layer 3.
    renderer.draw_panel(bounds, bg_color_, bg_color_);

    // Layer 2: Proportional fill.
    const float ratio = get_ratio();
    const int fill_width = static_cast<int>(bounds.width * ratio);
    if (fill_width > 0) {
        const Rect fill_rect{bounds.x, bounds.y, fill_width, bounds.height};
        renderer.draw_panel(fill_rect, fill_color_, fill_color_);
    }

    // Layer 3: Border outline.
    renderer.draw_panel(bounds, Color{0, 0, 0, 0}, border_color_);

    // Layer 4: Centered text showing "label: current / max".
    const std::string value_text = label_ + ": " +
                                   std::to_string(current_) + " / " +
                                   std::to_string(max_);

    // Approximate text centering: use get_text_width for horizontal,
    // get_text_height for vertical centering.
    const int text_w = renderer.get_text_width(value_text);
    const int text_h = renderer.get_text_height();
    const int text_x = bounds.x + (bounds.width - text_w) / 2;
    const int text_y = bounds.y + (bounds.height - text_h) / 2;

    // Draw shadow offset by 1 pixel for readability.
    renderer.draw_text(value_text, text_x + 1, text_y + 1, kValueTextShadow);
    // Draw the actual text on top.
    renderer.draw_text(value_text, text_x, text_y, kValueTextColor);
}

} // namespace mir2::ui::hud
