#ifndef LEGEND2_LOGIN_SCREEN_STATE_H
#define LEGEND2_LOGIN_SCREEN_STATE_H

namespace mir2::ui::screens {

/// @brief Login screen flow states.
enum class LoginScreenState {
    LOGIN,
    CHARACTER_SELECT,
    CHARACTER_CREATE,
    CONNECTING,
    ERROR
};

} // namespace mir2::ui::screens

#endif // LEGEND2_LOGIN_SCREEN_STATE_H
