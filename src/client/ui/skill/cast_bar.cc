// =============================================================================
// Legend2 Skill UI - Cast Bar
// =============================================================================

#include "ui/skill/cast_bar.h"

#include <algorithm>

#include "common/types.h"

namespace mir2::ui::skill {

namespace {
using mir2::common::Color;
using mir2::common::Rect;

constexpr Color kBarBackground{20, 20, 20, 200};
constexpr Color kBarFill{80, 140, 240, 220};
constexpr Color kBarBorder{200, 200, 200, 255};
} // namespace

CastBar::CastBar() = default;

void CastBar::start_cast(const std::string& skill_name, int64_t start_ms, int64_t end_ms) {
    active_ = true;
    skill_name_ = skill_name;
    start_ms_ = start_ms;
    end_ms_ = end_ms;
    progress_ = 0.0f;
}

void CastBar::cancel() {
    active_ = false;
    skill_name_.clear();
    start_ms_ = 0;
    end_ms_ = 0;
    progress_ = 0.0f;
}

void CastBar::update(int64_t now_ms) {
    if (!active_) {
        return;
    }

    if (end_ms_ <= start_ms_) {
        progress_ = 0.0f;
        return;
    }

    if (now_ms <= start_ms_) {
        progress_ = 0.0f;
        return;
    }

    if (now_ms >= end_ms_) {
        progress_ = 1.0f;
        active_ = false;
        return;
    }

    const int64_t total = end_ms_ - start_ms_;
    const int64_t elapsed = now_ms - start_ms_;
    const float ratio = static_cast<float>(elapsed) / static_cast<float>(total);
    progress_ = std::clamp(ratio, 0.0f, 1.0f);
}

void CastBar::render(render::IRenderer& renderer, int center_x, int y) {
    if (!active_) {
        return;
    }

    const int x = center_x - BAR_WIDTH / 2;
    const Rect bar_rect{x, y, BAR_WIDTH, BAR_HEIGHT};
    renderer.draw_rect(bar_rect, kBarBackground);

    const int fill_width = static_cast<int>(BAR_WIDTH * progress_);
    if (fill_width > 0) {
        const Rect fill_rect{x, y, fill_width, BAR_HEIGHT};
        renderer.draw_rect(fill_rect, kBarFill);
    }

    renderer.draw_rect_outline(bar_rect, kBarBorder);
}

bool CastBar::is_active() const {
    return active_;
}

float CastBar::get_progress() const {
    return progress_;
}

} // namespace mir2::ui::skill
