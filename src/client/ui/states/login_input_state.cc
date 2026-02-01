#include "ui/states/login_input_state.h"

#include "ui/states/login_state_helpers.h"
#include "ui/ui_layout_calculator.h"
#include "ui/ui_layout_constants.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace mir2::ui::screens {

namespace {
constexpr const char* kLoginLayoutScreen = "login";
} // namespace

LoginInputState::LoginInputState(LoginStateContext& context)
    : context_(context) {}

void LoginInputState::on_enter() {
    layout();

    if (context_.enter_reason == LoginStateEnterReason::LayoutRefresh) {
        return;
    }

    context_.username_field.focused = true;
    context_.password_field.focused = false;
}

void LoginInputState::on_exit() {}

void LoginInputState::update(float delta_time) {
    if (!context_.login_animation_playing || context_.login_animation_frames.empty()) {
        return;
    }

    context_.login_animation_timer += delta_time;
    if (context_.login_animation_timer < context_.login_animation_frame_time) {
        return;
    }

    context_.login_animation_timer = 0.0f;
    context_.login_animation_frame++;

    if (context_.login_animation_frame < static_cast<int>(context_.login_animation_frames.size())) {
        return;
    }

    context_.login_animation_playing = false;
    context_.login_animation_frame = 0;

    if (context_.on_login) {
        context_.on_login(context_.pending_username, context_.pending_password);
    }

    context_.pending_username.clear();
    context_.pending_password.clear();
}

void LoginInputState::render(UIRenderer& renderer) {
    (void)renderer;
    render_login_background(context_);

    if (context_.login_animation_playing) {
        return;
    }

    render_field_text(context_, context_.username_field, false);
    render_field_text(context_, context_.password_field, true);
}

bool LoginInputState::handle_event(const SDL_Event& event) {
    switch (event.type) {
        case SDL_MOUSEBUTTONDOWN:
            return handle_mouse_click(event.button.x, event.button.y);
        case SDL_KEYDOWN:
            return handle_key_press(event.key.keysym.sym);
        case SDL_TEXTINPUT: {
            const char* text = event.text.text;
            TextInputField* focused = get_focused_field();
            if (!focused || !text || !text[0]) {
                return false;
            }
            if (!is_valid_utf8(text)) {
                return true;
            }
            const unsigned char* p = reinterpret_cast<const unsigned char*>(text);
            while (*p) {
                if (*p < 0x20 || *p == 0x7F) {
                    return true;
                }
                ++p;
            }
            focused->input_text(text);
            return true;
        }
        default:
            break;
    }

    return false;
}

void LoginInputState::layout() {
    const int bg_w = (context_.background_texture && context_.background_texture->valid())
                     ? context_.background_texture->width()
                     : layout::login::DESIGN_WIDTH;
    const int bg_h = (context_.background_texture && context_.background_texture->valid())
                     ? context_.background_texture->height()
                     : layout::login::DESIGN_HEIGHT;
    const auto* screen_layout = context_.layout_loader.get_screen(kLoginLayoutScreen);
    const int design_w = screen_layout ? screen_layout->design_width : bg_w;
    const int design_h = screen_layout ? screen_layout->design_height : bg_h;
    mir2::ui::UILayoutCalculator calc(design_w, design_h, context_.screen_width, context_.screen_height);

    auto resolve_control = [&](const std::string& control_name, const Rect& fallback) -> Rect {
        if (auto rect = context_.layout_loader.resolve_control_rect(kLoginLayoutScreen,
                                                                    control_name,
                                                                    context_.screen_width,
                                                                    context_.screen_height)) {
            return *rect;
        }
        return calc.scale_rect(fallback);
    };

    context_.username_field.bounds = resolve_control("username_field", layout::login::USERNAME_FIELD);
    context_.password_field.bounds = resolve_control("password_field", layout::login::PASSWORD_FIELD);
    context_.login_confirm_bounds = resolve_control("login_confirm", layout::login::CONFIRM_BUTTON);

    context_.offline_button.bounds = {0, 0, 0, 0};
    context_.offline_button.enabled = false;
    if (auto offline_rect = context_.layout_loader.resolve_control_rect(kLoginLayoutScreen,
                                                                        "offline_play",
                                                                        context_.screen_width,
                                                                        context_.screen_height)) {
        context_.offline_button.bounds = *offline_rect;
        context_.offline_button.enabled = true;
    }
}


void LoginInputState::start_login() {
    if (context_.login_animation_playing) {
        return;
    }

    if (!is_valid_utf8(context_.username_field.text.c_str()) ||
        !is_valid_utf8(context_.password_field.text.c_str())) {
        if (context_.set_error) {
            context_.set_error("Invalid UTF-8 input");
        }
        return;
    }

    const size_t username_len = utf8_length(context_.username_field.text.c_str());
    const size_t password_len = utf8_length(context_.password_field.text.c_str());
    if (username_len < 3 || username_len > 20) {
        if (context_.set_error) {
            context_.set_error("Username must be 3-20 characters");
        }
        return;
    }
    if (password_len < 6 || password_len > 20) {
        if (context_.set_error) {
            context_.set_error("Password must be 6-20 characters");
        }
        return;
    }

    if (!context_.login_animation_frames.empty()) {
        context_.pending_username = context_.username_field.text;
        context_.pending_password = context_.password_field.text;
        context_.login_animation_playing = true;
        context_.login_animation_frame = 0;
        context_.login_animation_timer = 0.0f;
        return;
    }

    if (context_.on_login) {
        context_.on_login(context_.username_field.text, context_.password_field.text);
    }
}

TextInputField* LoginInputState::get_focused_field() {
    if (context_.username_field.focused) {
        return &context_.username_field;
    }
    if (context_.password_field.focused) {
        return &context_.password_field;
    }
    return nullptr;
}

bool LoginInputState::handle_key_press(SDL_Keycode key) {
    TextInputField* focused = get_focused_field();

    switch (key) {
        case SDLK_TAB:
            if (context_.username_field.focused) {
                context_.username_field.focused = false;
                context_.password_field.focused = true;
            } else {
                context_.username_field.focused = true;
                context_.password_field.focused = false;
            }
            return true;
        case SDLK_RETURN:
        case SDLK_KP_ENTER:
            start_login();
            return true;
        case SDLK_BACKSPACE:
            if (focused) {
                focused->backspace();
                return true;
            }
            break;
        case SDLK_DELETE:
            if (focused) {
                focused->delete_char();
                return true;
            }
            break;
        case SDLK_LEFT:
            if (focused) {
                focused->cursor_left();
                return true;
            }
            break;
        case SDLK_RIGHT:
            if (focused) {
                focused->cursor_right();
                return true;
            }
            break;
        case SDLK_HOME:
            if (focused) {
                focused->cursor_pos = 0;
                return true;
            }
            break;
        case SDLK_END:
            if (focused) {
                focused->cursor_pos = static_cast<int>(focused->text.length());
                return true;
            }
            break;
        default:
            break;
    }

    return false;
}

bool LoginInputState::handle_mouse_click(int x, int y) {
    if (x < 0 || y < 0 || x >= context_.screen_width || y >= context_.screen_height) {
        return false;
    }

    if (context_.username_field.bounds.contains(x, y)) {
        context_.username_field.focused = true;
        context_.password_field.focused = false;
        return true;
    }
    if (context_.password_field.bounds.contains(x, y)) {
        context_.username_field.focused = false;
        context_.password_field.focused = true;
        return true;
    }

    if (context_.login_confirm_bounds.contains(x, y)) {
        start_login();
        return true;
    }

    return false;
}

} // namespace mir2::ui::screens
