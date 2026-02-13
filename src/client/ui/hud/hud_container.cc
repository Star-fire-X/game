// =============================================================================
// Legend2 HUD - HUD Container (Implementation)
//
// Creates HP (red), MP (blue), and EXP (yellow) status bars stacked
// vertically at the bottom center of the screen, and a player info
// panel at the top-left corner.
// =============================================================================

#include "ui/hud/hud_container.h"

#include <memory>

namespace mir2::ui::hud {

HudContainer::HudContainer(int screen_width, int screen_height)
    : screen_width_(screen_width)
    , screen_height_(screen_height)
{
    set_id("hud_container");

    // -- Create HP bar (red) --
    auto hp = std::make_shared<StatusBar>(
        "HP",
        ui_colors::HP_BAR_FILL,
        ui_colors::HP_BAR_BG,
        ui_colors::HP_BAR_BORDER);
    hp_bar_ = hp.get();
    add_child(hp);

    // -- Create MP bar (blue) --
    auto mp = std::make_shared<StatusBar>(
        "MP",
        ui_colors::MP_BAR_FILL,
        ui_colors::MP_BAR_BG,
        ui_colors::MP_BAR_BORDER);
    mp_bar_ = mp.get();
    add_child(mp);

    // -- Create EXP bar (yellow) --
    auto exp = std::make_shared<StatusBar>(
        "EXP",
        ui_colors::EXP_BAR_FILL,
        ui_colors::EXP_BAR_BG,
        ui_colors::EXP_BAR_BORDER);
    exp_bar_ = exp.get();
    add_child(exp);

    // -- Create player info panel (top-left) --
    auto info = std::make_shared<PlayerInfoPanel>();
    player_info_ = info.get();
    add_child(info);

    // Position everything according to current screen size.
    recalculate_layout();
}

void HudContainer::update_from_stats(const mir2::common::CharacterStats& stats,
                                     const std::string& name) {
    if (hp_bar_) {
        hp_bar_->set_value(stats.hp, stats.max_hp);
    }
    if (mp_bar_) {
        mp_bar_->set_value(stats.mp, stats.max_mp);
    }
    if (exp_bar_) {
        // Experience bar: current experience towards an implicit max.
        // A common convention is that the experience field stores
        // progress within the current level. If the caller provides
        // a meaningful max through the stats.experience field, we
        // show raw experience out of 100 as a percentage placeholder.
        // Game logic should provide the real next-level threshold;
        // for now we treat experience as a 0-100 percentage.
        exp_bar_->set_value(stats.experience, 100);
    }
    if (player_info_) {
        player_info_->set_player_info(name, stats.level, stats.gold);
    }
}

void HudContainer::set_screen_size(int width, int height) {
    if (width == screen_width_ && height == screen_height_) {
        return;
    }
    screen_width_ = width;
    screen_height_ = height;
    recalculate_layout();
}

void HudContainer::recalculate_layout() {
    // The container itself spans the full screen (non-interactive overlay).
    set_bounds({0, 0, screen_width_, screen_height_});

    // --- Status bars: stacked vertically, centered at the bottom ---
    //
    // Layout (bottom-up):
    //   [EXP bar]  <- closest to bottom edge
    //   [MP bar]
    //   [HP bar]   <- topmost bar

    const int bar_x = (screen_width_ - BAR_WIDTH) / 2;

    // EXP bar at the very bottom (above margin).
    const int exp_y = screen_height_ - BAR_BOTTOM_MARGIN - BAR_HEIGHT;
    if (exp_bar_) {
        exp_bar_->set_bounds({bar_x, exp_y, BAR_WIDTH, BAR_HEIGHT});
    }

    // MP bar above EXP bar.
    const int mp_y = exp_y - BAR_SPACING - BAR_HEIGHT;
    if (mp_bar_) {
        mp_bar_->set_bounds({bar_x, mp_y, BAR_WIDTH, BAR_HEIGHT});
    }

    // HP bar above MP bar.
    const int hp_y = mp_y - BAR_SPACING - BAR_HEIGHT;
    if (hp_bar_) {
        hp_bar_->set_bounds({bar_x, hp_y, BAR_WIDTH, BAR_HEIGHT});
    }

    // --- Player info panel: top-left corner ---
    if (player_info_) {
        player_info_->set_bounds({INFO_PANEL_MARGIN, INFO_PANEL_MARGIN,
                                  INFO_PANEL_WIDTH, INFO_PANEL_HEIGHT});
    }
}

} // namespace mir2::ui::hud
