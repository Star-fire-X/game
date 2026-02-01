// =============================================================================
// Legend2 Skill UI - Skill Bar
// =============================================================================

#include "ui/skill/skill_bar.h"

#include <algorithm>

#include "common/types.h"

namespace mir2::ui::skill {

namespace {
using mir2::common::Color;
using mir2::common::Rect;

constexpr Color kBarBackground{20, 20, 20, 200};
constexpr Color kBarBorder{90, 90, 90, 220};
constexpr Color kSlotEmpty{30, 30, 30, 180};
constexpr Color kSlotFilled{70, 70, 90, 220};
constexpr Color kSlotBorder{120, 120, 120, 255};
constexpr Color kSlotDragging{255, 200, 80, 255};
constexpr Color kSlotCooldown{0, 0, 0, 160};
} // namespace

SkillBar::SkillBar(client::skill::SkillManager& skill_manager)
    : skill_manager_(skill_manager) {}

void SkillBar::update(int64_t now_ms) {
    last_update_ms_ = now_ms;
    skill_manager_.update(now_ms);
}

void SkillBar::render(render::IRenderer& renderer, int x, int y) {
    const int width = SLOT_COUNT * SLOT_SIZE + (SLOT_COUNT - 1) * SLOT_PADDING;
    const Rect bar_rect{x, y, width, SLOT_SIZE};

    renderer.draw_rect(bar_rect, kBarBackground);
    renderer.draw_rect_outline(bar_rect, kBarBorder);

    for (int i = 0; i < SLOT_COUNT; ++i) {
        const int slot_x = x + i * (SLOT_SIZE + SLOT_PADDING);
        const Rect slot_rect{slot_x, y, SLOT_SIZE, SLOT_SIZE};
        const uint32_t skill_id =
            skill_manager_.get_skill_by_hotkey(static_cast<uint8_t>(i + 1));

        renderer.draw_rect(slot_rect, skill_id == 0 ? kSlotEmpty : kSlotFilled);
        const bool is_dragging_slot = (dragging_slot_ == i);
        renderer.draw_rect_outline(slot_rect, is_dragging_slot ? kSlotDragging : kSlotBorder);

        if (skill_id == 0) {
            continue;
        }

        const int64_t remaining =
            skill_manager_.get_remaining_cooldown_ms(skill_id, last_update_ms_);
        if (remaining <= 0) {
            continue;
        }

        int overlay_height = SLOT_SIZE;
        const client::skill::ClientSkillTemplate* tmpl = skill_manager_.get_template(skill_id);
        if (tmpl && tmpl->cooldown_ms > 0) {
            const float ratio = std::clamp(
                static_cast<float>(remaining) / static_cast<float>(tmpl->cooldown_ms),
                0.0f,
                1.0f);
            overlay_height = static_cast<int>(SLOT_SIZE * ratio);
        }

        if (overlay_height > 0) {
            const Rect overlay{slot_rect.x, slot_rect.y, slot_rect.width, overlay_height};
            renderer.draw_rect(overlay, kSlotCooldown);
        }
    }
}

bool SkillBar::handle_click(int mouse_x, int mouse_y, int bar_x, int bar_y) {
    const int slot = get_slot_at(mouse_x, mouse_y, bar_x, bar_y);
    if (slot < 0) {
        return false;
    }

    const uint32_t skill_id =
        skill_manager_.get_skill_by_hotkey(static_cast<uint8_t>(slot + 1));
    if (skill_id != 0) {
        dragging_slot_ = slot;
    }
    return true;
}

int SkillBar::get_slot_at(int mouse_x, int mouse_y, int bar_x, int bar_y) const {
    for (int i = 0; i < SLOT_COUNT; ++i) {
        const int slot_x = bar_x + i * (SLOT_SIZE + SLOT_PADDING);
        const Rect slot_rect{slot_x, bar_y, SLOT_SIZE, SLOT_SIZE};
        if (slot_rect.contains(mouse_x, mouse_y)) {
            return i;
        }
    }
    return -1;
}

void SkillBar::start_drag(int slot) {
    if (slot < 0 || slot >= SLOT_COUNT) {
        dragging_slot_ = -1;
        return;
    }

    const uint32_t skill_id =
        skill_manager_.get_skill_by_hotkey(static_cast<uint8_t>(slot + 1));
    dragging_slot_ = (skill_id == 0) ? -1 : slot;
}

void SkillBar::end_drag(int target_slot) {
    if (dragging_slot_ < 0) {
        return;
    }

    const int source_slot = dragging_slot_;
    dragging_slot_ = -1;

    if (target_slot < 0 || target_slot >= SLOT_COUNT) {
        return;
    }

    const uint32_t skill_id =
        skill_manager_.get_skill_by_hotkey(static_cast<uint8_t>(source_slot + 1));
    if (skill_id == 0) {
        return;
    }

    skill_manager_.bind_hotkey(static_cast<uint8_t>(target_slot + 1), skill_id);
}

bool SkillBar::is_dragging() const {
    return dragging_slot_ >= 0;
}

} // namespace mir2::ui::skill
