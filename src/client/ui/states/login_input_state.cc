#include "ui/states/login_input_state.h"

#include "ui/login_screen.h"
#include "ui/states/login_state_helpers.h"
#include "ui/ui_layout_calculator.h"
#include "ui/ui_layout_constants.h"
#include "common/types/constants.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace mir2::ui::screens {

namespace {
constexpr const char* kLoginLayoutScreen = "login";
constexpr size_t kMinUsernameLength = mir2::common::constants::LOGIN_USERNAME_MIN_LENGTH;
constexpr size_t kMaxUsernameLength = mir2::common::constants::LOGIN_USERNAME_MAX_LENGTH;
constexpr size_t kMinPasswordLength = mir2::common::constants::LOGIN_PASSWORD_MIN_LENGTH;
constexpr size_t kMaxPasswordLength = mir2::common::constants::LOGIN_PASSWORD_MAX_LENGTH;
} // namespace

LoginInputState::LoginInputState(ILoginInputStateContext& context)
    : context_(context) {}

void LoginInputState::on_enter() {
    layout();

    if (context_.get_enter_reason() == LoginStateEnterReason::LayoutRefresh) {
        return;
    }

    auto& username_field = context_.get_username_field();
    auto& password_field = context_.get_password_field();
    username_field.focused = true;
    password_field.focused = false;
}

void LoginInputState::on_exit() {}

void LoginInputState::update(float delta_time) {
    const auto& login_frames = context_.get_login_animation_frames();
    if (!context_.is_login_animation_playing() || login_frames.empty()) {
        return;
    }

    const float updated_timer = context_.get_login_animation_timer() + delta_time;
    if (updated_timer < context_.get_login_animation_frame_time()) {
        context_.set_login_animation_timer(updated_timer);
        return;
    }

    context_.set_login_animation_timer(0.0f);
    const int next_frame = context_.get_login_animation_frame() + 1;
    context_.set_login_animation_frame(next_frame);

    if (next_frame < static_cast<int>(login_frames.size())) {
        return;
    }

    context_.set_login_animation_playing(false);
    context_.set_login_animation_frame(0);
    context_.invoke_login(context_.get_pending_username(), context_.get_pending_password());
    context_.clear_pending_credentials();
}

void LoginInputState::render(UIRenderer& renderer) {
    (void)renderer;
    render_login_background(context_);

    if (context_.is_login_animation_playing()) {
        return;
    }

    render_field_text(context_, context_.get_username_field(), false);
    render_field_text(context_, context_.get_password_field(), true);
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
            if (contains_control_characters(text)) {
                return true;
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
    auto background = context_.get_background_texture();
    const int bg_w = (background && background->valid())
                     ? background->width()
                     : layout::login::DESIGN_WIDTH;
    const int bg_h = (background && background->valid())
                     ? background->height()
                     : layout::login::DESIGN_HEIGHT;
    const auto* screen_layout = context_.get_layout_loader().get_screen(kLoginLayoutScreen);
    const int design_w = screen_layout ? screen_layout->design_width : bg_w;
    const int design_h = screen_layout ? screen_layout->design_height : bg_h;
    mir2::ui::UILayoutCalculator calc(design_w, design_h, context_.get_screen_width(), context_.get_screen_height());

    auto resolve_control = [&](const std::string& control_name, const Rect& fallback) -> Rect {
        if (auto rect = context_.get_layout_loader().resolve_control_rect(kLoginLayoutScreen,
                                                                          control_name,
                                                                          context_.get_screen_width(),
                                                                          context_.get_screen_height())) {
            return *rect;
        }
        return calc.scale_rect(fallback);
    };

    context_.get_username_field().bounds = resolve_control("username_field", layout::login::USERNAME_FIELD);
    context_.get_password_field().bounds = resolve_control("password_field", layout::login::PASSWORD_FIELD);
    context_.get_login_confirm_bounds() = resolve_control("login_confirm", layout::login::CONFIRM_BUTTON);

    auto& offline_button = context_.get_offline_button();
    offline_button.bounds = {0, 0, 0, 0};
    offline_button.enabled = false;
    if (auto offline_rect = context_.get_layout_loader().resolve_control_rect(kLoginLayoutScreen,
                                                                              "offline_play",
                                                                              context_.get_screen_width(),
                                                                              context_.get_screen_height())) {
        offline_button.bounds = *offline_rect;
        offline_button.enabled = true;
    }
}


void LoginInputState::start_login() {
    if (context_.is_login_animation_playing()) {
        return;
    }

    auto& username_field = context_.get_username_field();
    auto& password_field = context_.get_password_field();

    if (!is_valid_utf8(username_field.text.c_str()) ||
        !is_valid_utf8(password_field.text.c_str())) {
        context_.set_error("输入包含无效 UTF-8 字符");
        return;
    }

    const size_t username_len = utf8_length(username_field.text.c_str());
    const size_t password_len = utf8_length(password_field.text.c_str());
    if (username_len < kMinUsernameLength || username_len > kMaxUsernameLength) {
        context_.set_error("用户名长度必须在" + std::to_string(kMinUsernameLength) + "到" +
                           std::to_string(kMaxUsernameLength) + "之间");
        return;
    }
    if (password_len < kMinPasswordLength || password_len > kMaxPasswordLength) {
        context_.set_error("密码长度必须在" + std::to_string(kMinPasswordLength) + "到" +
                           std::to_string(kMaxPasswordLength) + "之间");
        return;
    }

    const auto& login_frames = context_.get_login_animation_frames();
    if (!login_frames.empty()) {
        context_.start_login_animation(username_field.text, password_field.text);
        return;
    }

    context_.invoke_login(username_field.text, password_field.text);
}

TextInputField* LoginInputState::get_focused_field() {
    auto& username_field = context_.get_username_field();
    auto& password_field = context_.get_password_field();
    if (username_field.focused) {
        return &username_field;
    }
    if (password_field.focused) {
        return &password_field;
    }
    return nullptr;
}

bool LoginInputState::handle_key_press(SDL_Keycode key) {
    TextInputField* focused = get_focused_field();
    auto& username_field = context_.get_username_field();
    auto& password_field = context_.get_password_field();

    switch (key) {
        case SDLK_TAB:
            if (username_field.focused) {
                username_field.focused = false;
                password_field.focused = true;
            } else {
                username_field.focused = true;
                password_field.focused = false;
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
    if (x < 0 || y < 0 || x >= context_.get_screen_width() || y >= context_.get_screen_height()) {
        return false;
    }

    auto& username_field = context_.get_username_field();
    auto& password_field = context_.get_password_field();

    if (username_field.bounds.contains(x, y)) {
        username_field.focused = true;
        password_field.focused = false;
        return true;
    }
    if (password_field.bounds.contains(x, y)) {
        username_field.focused = false;
        password_field.focused = true;
        return true;
    }

    if (context_.get_login_confirm_bounds().contains(x, y)) {
        start_login();
        return true;
    }

    return false;
}

} // namespace mir2::ui::screens
