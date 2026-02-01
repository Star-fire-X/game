// =============================================================================
// Legend2 Skill UI - Skill Target Indicator
// =============================================================================

#ifndef LEGEND2_UI_SKILL_SKILL_TARGET_INDICATOR_H
#define LEGEND2_UI_SKILL_SKILL_TARGET_INDICATOR_H

#include "render/camera.h"
#include "render/i_renderer.h"

namespace mir2::ui::skill {

class SkillTargetIndicator {
public:
    SkillTargetIndicator();

    void show(float radius, bool is_valid);
    void hide();
    void set_position(int world_x, int world_y);

    void render(render::IRenderer& renderer, const render::Camera& camera);

    bool is_visible() const;

private:
    bool visible_ = false;
    float radius_ = 0.0f;
    int world_x_ = 0;
    int world_y_ = 0;
    bool is_valid_ = true;  // green if valid, red if invalid
};

} // namespace mir2::ui::skill

#endif // LEGEND2_UI_SKILL_SKILL_TARGET_INDICATOR_H
