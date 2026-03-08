#ifndef LEGEND2_ERROR_STATE_H
#define LEGEND2_ERROR_STATE_H

#include "ui/states/login_state_interface.h"
#include "ui/states/login_state_context.h"

namespace mir2::ui::screens {

class ErrorState : public ILoginState {
public:
    explicit ErrorState(IErrorStateContext& context);

    void on_enter() override;
    void on_exit() override;
    void update(float delta_time) override;
    void render(UIRenderer& renderer) override;
    bool handle_event(const SDL_Event& event) override;
    LoginScreenState get_state_type() const override { return LoginScreenState::ERROR; }

private:
    IErrorStateContext& context_;

    void render_background();
};

} // namespace mir2::ui::screens

#endif // LEGEND2_ERROR_STATE_H
