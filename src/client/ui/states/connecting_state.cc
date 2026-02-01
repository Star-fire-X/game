#include "ui/states/connecting_state.h"

namespace mir2::ui::screens {

ConnectingState::ConnectingState(LoginStateContext& context)
    : context_(context) {}

void ConnectingState::on_enter() {}

void ConnectingState::on_exit() {}

void ConnectingState::update(float delta_time) {
    (void)delta_time;
}

void ConnectingState::render(UIRenderer& renderer) {
    (void)renderer;
    render_background();

    const std::string msg = context_.status_text.empty() ? "Connecting..." : context_.status_text;
    int x = (context_.screen_width - context_.ui_renderer.get_text_width(msg)) / 2;
    int y = context_.screen_height / 2 - context_.ui_renderer.get_text_height() / 2;
    context_.ui_renderer.draw_text(msg, x, y, {255, 255, 255, 255});
}

bool ConnectingState::handle_event(const SDL_Event& event) {
    (void)event;
    return false;
}

void ConnectingState::render_background() {
    context_.renderer.clear(Color::black());

    if (context_.background_texture && context_.background_texture->valid()) {
        Rect bg_src = {0, 0, context_.background_texture->width(), context_.background_texture->height()};
        Rect bg_dst = {0, 0, context_.screen_width, context_.screen_height};
        context_.renderer.draw_texture(*context_.background_texture, bg_src, bg_dst);
    }
}

} // namespace mir2::ui::screens
