// =============================================================================
// Legend2 Skill UI - Skill Book
// =============================================================================

#include "ui/skill/skill_book.h"

#include <algorithm>
#include <vector>

#include "common/types.h"

namespace mir2::ui::skill {

namespace {
using mir2::common::Color;
using mir2::common::Rect;

constexpr int kPanelX = 20;
constexpr int kPanelY = 20;
constexpr int kPanelWidth = 320;
constexpr int kPanelHeight = 300;
constexpr int kTitleHeight = 24;
constexpr int kSlotSize = 40;
constexpr int kSlotPadding = 6;
constexpr int kColumns = 4;
constexpr int kRows = 5;
constexpr int kContentPadding = 12;

constexpr Color kPanelBg{15, 15, 20, 200};
constexpr Color kPanelBorder{90, 90, 110, 220};
constexpr Color kSlotEmpty{30, 30, 40, 180};
constexpr Color kSlotFilled{60, 80, 120, 220};
constexpr Color kSlotBorder{120, 120, 140, 255};
constexpr Color kSlotHighlight{220, 200, 120, 255};

Rect slot_rect_for_index(int index, int scroll_offset) {
    const int row = index / kColumns;
    const int col = index % kColumns;
    const int x = kPanelX + kContentPadding + col * (kSlotSize + kSlotPadding);
    const int y = kPanelY + kTitleHeight + kContentPadding +
                  row * (kSlotSize + kSlotPadding) - scroll_offset;
    return {x, y, kSlotSize, kSlotSize};
}
} // namespace

SkillBook::SkillBook(client::skill::SkillManager& skill_manager)
    : skill_manager_(skill_manager) {}

void SkillBook::open() {
    open_ = true;
}

void SkillBook::close() {
    open_ = false;
    hovered_skill_id_ = 0;
}

void SkillBook::toggle() {
    open_ = !open_;
    if (!open_) {
        hovered_skill_id_ = 0;
    }
}

bool SkillBook::is_open() const {
    return open_;
}

void SkillBook::update(int64_t now_ms) {
    (void)now_ms;
    scroll_offset_ = std::max(0, scroll_offset_);
}

void SkillBook::render(render::IRenderer& renderer) {
    if (!open_) {
        return;
    }

    const Rect panel_rect{kPanelX, kPanelY, kPanelWidth, kPanelHeight};
    renderer.draw_rect(panel_rect, kPanelBg);
    renderer.draw_rect_outline(panel_rect, kPanelBorder);

    std::vector<uint32_t> skill_ids;
    skill_ids.reserve(client::skill::SkillManager::kMaxSkills);
    for (const auto& slot : skill_manager_.get_learned_skills()) {
        if (slot.has_value()) {
            skill_ids.push_back(slot->skill_id);
        }
    }

    const int max_slots = kColumns * kRows;
    for (int i = 0; i < max_slots; ++i) {
        const Rect slot_rect = slot_rect_for_index(i, scroll_offset_);
        if (slot_rect.y + slot_rect.height < panel_rect.y + kTitleHeight ||
            slot_rect.y > panel_rect.y + panel_rect.height) {
            continue;
        }

        const bool has_skill = i < static_cast<int>(skill_ids.size());
        renderer.draw_rect(slot_rect, has_skill ? kSlotFilled : kSlotEmpty);

        const bool is_hovered = has_skill && skill_ids[i] == hovered_skill_id_;
        renderer.draw_rect_outline(slot_rect, is_hovered ? kSlotHighlight : kSlotBorder);
    }
}

uint32_t SkillBook::handle_click(int mouse_x, int mouse_y) {
    if (!open_) {
        return 0;
    }

    const Rect panel_rect{kPanelX, kPanelY, kPanelWidth, kPanelHeight};
    if (!panel_rect.contains(mouse_x, mouse_y)) {
        hovered_skill_id_ = 0;
        return 0;
    }

    std::vector<uint32_t> skill_ids;
    skill_ids.reserve(client::skill::SkillManager::kMaxSkills);
    for (const auto& slot : skill_manager_.get_learned_skills()) {
        if (slot.has_value()) {
            skill_ids.push_back(slot->skill_id);
        }
    }

    const int max_slots = kColumns * kRows;
    for (int i = 0; i < max_slots; ++i) {
        const Rect slot_rect = slot_rect_for_index(i, scroll_offset_);
        if (!slot_rect.contains(mouse_x, mouse_y)) {
            continue;
        }

        if (i < static_cast<int>(skill_ids.size())) {
            hovered_skill_id_ = skill_ids[i];
            return skill_ids[i];
        }

        hovered_skill_id_ = 0;
        return 0;
    }

    hovered_skill_id_ = 0;
    return 0;
}

const client::skill::ClientSkillTemplate* SkillBook::get_hovered_skill() const {
    if (hovered_skill_id_ == 0) {
        return nullptr;
    }
    return skill_manager_.get_template(hovered_skill_id_);
}

} // namespace mir2::ui::skill
