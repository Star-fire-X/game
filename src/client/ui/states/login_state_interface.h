#ifndef LEGEND2_LOGIN_STATE_INTERFACE_H
#define LEGEND2_LOGIN_STATE_INTERFACE_H

#include <SDL.h>
#include "ui/states/login_screen_state.h"

namespace mir2::ui {
class UIRenderer;
}  // namespace mir2::ui

namespace mir2::ui::screens {

using mir2::ui::UIRenderer;

class ILoginState {
public:
    virtual ~ILoginState() = default;
    virtual void on_enter() = 0;
    virtual void on_exit() = 0;
    virtual void update(float delta_time) = 0;
    virtual void render(UIRenderer& renderer) = 0;
    virtual bool handle_event(const SDL_Event& event) = 0;
    virtual LoginScreenState get_state_type() const = 0;
};

} // namespace mir2::ui::screens

#endif // LEGEND2_LOGIN_STATE_INTERFACE_H
