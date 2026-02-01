#include "ui/states/character_select_state.h"

#include "ui/states/login_state_helpers.h"

namespace mir2::ui::screens {

CharacterSelectState::CharacterSelectState(LoginStateContext& context)
    : context_(context) {}

void CharacterSelectState::on_enter() {
    layout();
}

void CharacterSelectState::on_exit() {}

void CharacterSelectState::update(float delta_time) {
    (void)delta_time;
}

void CharacterSelectState::render(UIRenderer& renderer) {
    (void)renderer;
    render_background();

    // TODO: implement rendering for character select UI assets.
}

bool CharacterSelectState::handle_event(const SDL_Event& event) {
    switch (event.type) {
        case SDL_MOUSEMOTION:
            for (auto& slot : context_.character_slots) {
                update_slot_hover(slot, event.motion.x, event.motion.y);
            }
            return false;
        case SDL_KEYDOWN:
            if (event.key.keysym.sym == SDLK_ESCAPE) {
                if (context_.transition_to) {
                    // Transition: character select -> login input.
                    context_.transition_to(LoginScreenState::LOGIN);
                }
                return true;
            }
            break;
        default:
            break;
    }

    return false;
}

void CharacterSelectState::layout() {
    if (context_.enter_reason == LoginStateEnterReason::LayoutRefresh) {
        return;
    }

    // TODO: layout based on resource assets.
}

void CharacterSelectState::render_background() {
    context_.renderer.clear(Color::black());

    if (context_.background_texture && context_.background_texture->valid()) {
        Rect bg_src = {0, 0, context_.background_texture->width(), context_.background_texture->height()};
        Rect bg_dst = {0, 0, context_.screen_width, context_.screen_height};
        context_.renderer.draw_texture(*context_.background_texture, bg_src, bg_dst);
    }
}

} // namespace mir2::ui::screens
