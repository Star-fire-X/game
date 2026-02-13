// =============================================================================
// Legend2 HUD - Player Info Panel (Implementation)
//
// Renders a semi-transparent panel with the player's name, level, and gold.
// =============================================================================

#include "ui/hud/player_info_panel.h"

#include <string>

namespace mir2::ui::hud {

namespace {

/// Panel background: semi-transparent dark.
constexpr Color kPanelBg{20, 20, 20, 180};

/// Panel border: subtle gray.
constexpr Color kPanelBorder{100, 100, 100, 200};

/// Name text: bright white.
constexpr Color kNameColor{255, 255, 255, 255};

/// Level text: light cyan for distinction.
constexpr Color kLevelColor{180, 220, 255, 255};

/// Gold text: warm gold tone.
constexpr Color kGoldColor{255, 215, 80, 255};

/// Shadow behind text for contrast.
constexpr Color kTextShadow{0, 0, 0, 160};

} // namespace

PlayerInfoPanel::PlayerInfoPanel() {
    set_id("player_info_panel");
}

void PlayerInfoPanel::set_player_info(const std::string& name, int level, int gold) {
    name_ = name;
    level_ = level;
    gold_ = gold;
}

void PlayerInfoPanel::render(UIRenderer& renderer) {
    if (!is_visible()) {
        return;
    }

    const Rect& bounds = get_bounds();

    // Draw panel background with border.
    renderer.draw_panel(bounds, kPanelBg, kPanelBorder);

    const int text_h = renderer.get_text_height();
    int y_cursor = bounds.y + PADDING;
    const int text_x = bounds.x + PADDING;

    // Row 1: Player name.
    renderer.draw_text(name_, text_x + 1, y_cursor + 1, kTextShadow);
    renderer.draw_text(name_, text_x, y_cursor, kNameColor);
    y_cursor += text_h + LINE_SPACING;

    // Row 2: Level display.
    const std::string level_text = "Lv." + std::to_string(level_);
    renderer.draw_text(level_text, text_x + 1, y_cursor + 1, kTextShadow);
    renderer.draw_text(level_text, text_x, y_cursor, kLevelColor);
    y_cursor += text_h + LINE_SPACING;

    // Row 3: Gold amount.
    const std::string gold_text = "Gold: " + std::to_string(gold_);
    renderer.draw_text(gold_text, text_x + 1, y_cursor + 1, kTextShadow);
    renderer.draw_text(gold_text, text_x, y_cursor, kGoldColor);
}

} // namespace mir2::ui::hud
