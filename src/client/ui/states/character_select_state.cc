#include "ui/states/character_select_state.h"

#include "ui/login_screen.h"
#include "ui/states/login_state_helpers.h"

namespace mir2::ui::screens {

CharacterSelectState::CharacterSelectState(ICharacterSelectStateContext& context)
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
            for (auto& slot : context_.get_character_slots()) {
                update_slot_hover(slot, event.motion.x, event.motion.y);
            }
            return false;
        case SDL_KEYDOWN:
            if (event.key.keysym.sym == SDLK_ESCAPE) {
                context_.transition_to(LoginScreenState::LOGIN);
                return true;
            }
            break;
        default:
            break;
    }

    return false;
}

void CharacterSelectState::layout() {
    if (context_.get_enter_reason() == LoginStateEnterReason::LayoutRefresh) {
        return;
    }

    // TODO: layout based on resource assets.
}

void CharacterSelectState::render_background() {
    render_common_background(context_);
}

} // namespace mir2::ui::screens
