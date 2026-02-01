#include "ui/states/error_state.h"

namespace mir2::ui::screens {

ErrorState::ErrorState(LoginStateContext& context)
    : context_(context) {}

void ErrorState::on_enter() {}

void ErrorState::on_exit() {}

void ErrorState::update(float delta_time) {
    (void)delta_time;
}

void ErrorState::render(UIRenderer& renderer) {
    (void)renderer;
    render_background();

    const std::string msg = "Error";
    int x = (context_.screen_width - context_.ui_renderer.get_text_width(msg)) / 2;
    int y = context_.screen_height / 2 - context_.ui_renderer.get_text_height() / 2;
    context_.ui_renderer.draw_text(msg, x, y, {255, 100, 100, 255});
}

bool ErrorState::handle_event(const SDL_Event& event) {
    (void)event;
    return false;
}

void ErrorState::render_background() {
    context_.renderer.clear(Color::black());

    if (context_.background_texture && context_.background_texture->valid()) {
        Rect bg_src = {0, 0, context_.background_texture->width(), context_.background_texture->height()};
        Rect bg_dst = {0, 0, context_.screen_width, context_.screen_height};
        context_.renderer.draw_texture(*context_.background_texture, bg_src, bg_dst);
    }
}

} // namespace mir2::ui::screens
