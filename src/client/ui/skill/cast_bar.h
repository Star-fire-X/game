// =============================================================================
// Legend2 Skill UI - Cast Bar
// =============================================================================

#ifndef LEGEND2_UI_SKILL_CAST_BAR_H
#define LEGEND2_UI_SKILL_CAST_BAR_H

#include "render/i_renderer.h"

#include <cstdint>
#include <string>

namespace mir2::ui::skill {

class CastBar {
public:
    CastBar();

    void start_cast(const std::string& skill_name, int64_t start_ms, int64_t end_ms);
    void cancel();

    void update(int64_t now_ms);
    void render(render::IRenderer& renderer, int center_x, int y);

    bool is_active() const;
    float get_progress() const;

private:
    bool active_ = false;
    std::string skill_name_;
    int64_t start_ms_ = 0;
    int64_t end_ms_ = 0;
    float progress_ = 0.0f;

    static constexpr int BAR_WIDTH = 200;
    static constexpr int BAR_HEIGHT = 20;
};

} // namespace mir2::ui::skill

#endif // LEGEND2_UI_SKILL_CAST_BAR_H
