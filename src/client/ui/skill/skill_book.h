// =============================================================================
// Legend2 Skill UI - Skill Book
// =============================================================================

#ifndef LEGEND2_UI_SKILL_SKILL_BOOK_H
#define LEGEND2_UI_SKILL_SKILL_BOOK_H

#include "client/game/skill/skill_manager.h"
#include "render/i_renderer.h"

#include <cstdint>

namespace mir2::ui::skill {

class SkillBook {
public:
    explicit SkillBook(client::skill::SkillManager& skill_manager);

    void open();
    void close();
    void toggle();
    bool is_open() const;

    void update(int64_t now_ms);
    void render(render::IRenderer& renderer);

    // Returns skill_id for drag, 0 if none
    uint32_t handle_click(int mouse_x, int mouse_y);

    // Tooltip info
    const client::skill::ClientSkillTemplate* get_hovered_skill() const;

private:
    client::skill::SkillManager& skill_manager_;
    bool open_ = false;
    int scroll_offset_ = 0;
    uint32_t hovered_skill_id_ = 0;
};

} // namespace mir2::ui::skill

#endif // LEGEND2_UI_SKILL_SKILL_BOOK_H
