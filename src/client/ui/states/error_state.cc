#include "ui/states/error_state.h"

#include "ui/states/login_state_helpers.h"
#include "ui/ui_renderer.h"

namespace mir2::ui::screens {

ErrorState::ErrorState(IErrorStateContext& context)
    : context_(context) {}

void ErrorState::on_enter() {}

void ErrorState::on_exit() {}

void ErrorState::update(float delta_time) {
    (void)delta_time;
}

void ErrorState::render(UIRenderer& renderer) {
    (void)renderer;
    render_background();

    auto& ui_renderer = context_.get_ui_renderer();
    const std::string& error = context_.get_error_message();
    const std::string& msg = error.empty() ? "Unknown Error" : error;
    int x = (context_.get_screen_width() - ui_renderer.get_text_width(msg)) / 2;
    int y = context_.get_screen_height() / 2 - ui_renderer.get_text_height() / 2;
    ui_renderer.draw_text(msg, x, y, {255, 100, 100, 255});
}

bool ErrorState::handle_event(const SDL_Event& event) {
    if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE) {
        context_.clear_error();
        context_.transition_to(LoginScreenState::LOGIN);
        return true;
    }
    return false;
}

void ErrorState::render_background() {
    render_common_background(context_);
}

} // namespace mir2::ui::screens
