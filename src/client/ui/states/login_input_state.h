#ifndef LEGEND2_LOGIN_INPUT_STATE_H
#define LEGEND2_LOGIN_INPUT_STATE_H

#include "ui/states/login_state_interface.h"
#include "ui/login_screen.h"

namespace mir2::ui::screens {

class LoginInputState : public ILoginState {
public:
    explicit LoginInputState(LoginStateContext& context);

    void on_enter() override;
    void on_exit() override;
    void update(float delta_time) override;
    void render(UIRenderer& renderer) override;
    bool handle_event(const SDL_Event& event) override;
    LoginScreenState get_state_type() const override { return LoginScreenState::LOGIN; }

private:
    LoginStateContext& context_;

    void layout();
    void start_login();
    TextInputField* get_focused_field();
    bool handle_key_press(SDL_Keycode key);
    bool handle_mouse_click(int x, int y);
};

} // namespace mir2::ui::screens

#endif // LEGEND2_LOGIN_INPUT_STATE_H
