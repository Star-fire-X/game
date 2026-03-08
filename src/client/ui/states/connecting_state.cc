#include "ui/states/connecting_state.h"

#include "ui/states/login_state_helpers.h"
#include "ui/ui_renderer.h"

namespace mir2::ui::screens {

ConnectingState::ConnectingState(IConnectingStateContext& context)
    : context_(context) {}

void ConnectingState::on_enter() {}

void ConnectingState::on_exit() {}

void ConnectingState::update(float delta_time) {
    (void)delta_time;
}

void ConnectingState::render(UIRenderer& renderer) {
    (void)renderer;
    render_background();

    auto& ui_renderer = context_.get_ui_renderer();
    const std::string& status = context_.get_status_text();
    const std::string msg = status.empty() ? "Connecting..." : status;
    int x = (context_.get_screen_width() - ui_renderer.get_text_width(msg)) / 2;
    int y = context_.get_screen_height() / 2 - ui_renderer.get_text_height() / 2;
    ui_renderer.draw_text(msg, x, y, {255, 255, 255, 255});
}

bool ConnectingState::handle_event(const SDL_Event& event) {
    if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE) {
        context_.transition_to(LoginScreenState::LOGIN);
        return true;
    }
    return false;
}

void ConnectingState::render_background() {
    render_common_background(context_);
}

} // namespace mir2::ui::screens
