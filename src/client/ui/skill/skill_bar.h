// =============================================================================
// Legend2 Skill UI - Skill Bar
// =============================================================================

#ifndef LEGEND2_UI_SKILL_SKILL_BAR_H
#define LEGEND2_UI_SKILL_SKILL_BAR_H

#include "client/game/skill/skill_manager.h"
#include "render/i_renderer.h"

#include <cstdint>

namespace mir2::ui::skill {

class SkillBar {
public:
    static constexpr int SLOT_COUNT = 8;

    explicit SkillBar(client::skill::SkillManager& skill_manager);

    // Update cooldown animations
    void update(int64_t now_ms);

    // Render the skill bar
    void render(render::IRenderer& renderer, int x, int y);

    // Handle click on skill bar
    bool handle_click(int mouse_x, int mouse_y, int bar_x, int bar_y);

    // Get slot at position (-1 if none)
    int get_slot_at(int mouse_x, int mouse_y, int bar_x, int bar_y) const;

    // Drag & drop support
    void start_drag(int slot);
    void end_drag(int target_slot);
    bool is_dragging() const;

private:
    client::skill::SkillManager& skill_manager_;
    int dragging_slot_ = -1;
    int64_t last_update_ms_ = 0;

    static constexpr int SLOT_SIZE = 40;
    static constexpr int SLOT_PADDING = 2;
};

} // namespace mir2::ui::skill

#endif // LEGEND2_UI_SKILL_SKILL_BAR_H
