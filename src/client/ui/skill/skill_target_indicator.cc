// =============================================================================
// Legend2 Skill UI - Skill Target Indicator
// =============================================================================

#include "ui/skill/skill_target_indicator.h"

#include <cmath>

#include "common/types.h"

namespace mir2::ui::skill {

namespace {
using mir2::common::Color;
using mir2::common::Position;

constexpr float kPi = 3.14159265f;
constexpr int kSegments = 32;

constexpr Color kValidColor{60, 220, 90, 180};
constexpr Color kInvalidColor{220, 70, 70, 180};
constexpr Color kCenterColor{255, 255, 255, 200};
} // namespace

SkillTargetIndicator::SkillTargetIndicator() = default;

void SkillTargetIndicator::show(float radius, bool is_valid) {
    visible_ = true;
    radius_ = radius;
    is_valid_ = is_valid;
}

void SkillTargetIndicator::hide() {
    visible_ = false;
}

void SkillTargetIndicator::set_position(int world_x, int world_y) {
    world_x_ = world_x;
    world_y_ = world_y;
}

void SkillTargetIndicator::render(render::IRenderer& renderer, const render::Camera& camera) {
    if (!visible_) {
        return;
    }

    const Position world_pos{world_x_, world_y_};
    const Position screen_pos = camera.world_to_screen(world_pos);

    const float radius_x = radius_ * static_cast<float>(mir2::common::constants::TILE_WIDTH) *
                           camera.zoom;
    const float radius_y = radius_ * static_cast<float>(mir2::common::constants::TILE_HEIGHT) *
                           camera.zoom;

    const Color color = is_valid_ ? kValidColor : kInvalidColor;

    if (radius_x <= 1.0f || radius_y <= 1.0f) {
        renderer.draw_point(screen_pos.x, screen_pos.y, color);
        return;
    }

    int prev_x = screen_pos.x + static_cast<int>(std::cos(0.0f) * radius_x);
    int prev_y = screen_pos.y + static_cast<int>(std::sin(0.0f) * radius_y);

    for (int i = 1; i <= kSegments; ++i) {
        const float angle = (2.0f * kPi * static_cast<float>(i)) / static_cast<float>(kSegments);
        const int x = screen_pos.x + static_cast<int>(std::cos(angle) * radius_x);
        const int y = screen_pos.y + static_cast<int>(std::sin(angle) * radius_y);
        renderer.draw_line(prev_x, prev_y, x, y, color);
        prev_x = x;
        prev_y = y;
    }

    renderer.draw_line(screen_pos.x - 4, screen_pos.y, screen_pos.x + 4, screen_pos.y, kCenterColor);
    renderer.draw_line(screen_pos.x, screen_pos.y - 4, screen_pos.x, screen_pos.y + 4, kCenterColor);
}

bool SkillTargetIndicator::is_visible() const {
    return visible_;
}

} // namespace mir2::ui::skill
