// Legend2 Login Screen Implementation
// Login and character selection/creation UI

#include "ui/login_screen.h"
#include "ui/states/login_input_state.h"
#include "ui/states/character_select_state.h"
#include "ui/states/character_create_state.h"
#include "ui/states/connecting_state.h"
#include "ui/states/error_state.h"
#include "ui/states/login_state_helpers.h"
#include "client/resource/resource_loader.h"
#include "scene/scene_manager.h"
#include <algorithm>
#include <cstring>
#include <iostream>

namespace mir2::ui::screens {

namespace {
constexpr const char* kUILayoutPath = "Data/ui_layout.json";

int utf8_sequence_length(unsigned char lead) {
    if (lead <= 0x7F) {
        return 1;
    }
    if ((lead & 0xE0) == 0xC0) {
        return 2;
    }
    if ((lead & 0xF0) == 0xE0) {
        return 3;
    }
    if ((lead & 0xF8) == 0xF0) {
        return 4;
    }
    return 0;
}

int clamp_cursor_to_boundary(const std::string& text, int pos) {
    const int size = static_cast<int>(text.size());
    if (pos < 0) pos = 0;
    if (pos > size) pos = size;
    while (pos > 0 && pos < size &&
           (static_cast<unsigned char>(text[static_cast<size_t>(pos)]) & 0xC0) == 0x80) {
        --pos;
    }
    return pos;
}

int previous_utf8_boundary(const std::string& text, int pos) {
    pos = clamp_cursor_to_boundary(text, pos);
    if (pos <= 0) {
        return 0;
    }
    --pos;
    while (pos > 0 &&
           (static_cast<unsigned char>(text[static_cast<size_t>(pos)]) & 0xC0) == 0x80) {
        --pos;
    }
    return pos;
}

int next_utf8_boundary(const std::string& text, int pos) {
    pos = clamp_cursor_to_boundary(text, pos);
    const int size = static_cast<int>(text.size());
    if (pos >= size) {
        return size;
    }
    const unsigned char lead = static_cast<unsigned char>(text[static_cast<size_t>(pos)]);
    int len = utf8_sequence_length(lead);
    if (len <= 0) {
        return std::min(size, pos + 1);
    }
    if (pos + len > size) {
        return size;
    }
    return pos + len;
}

std::string utf8_prefix(const char* text, size_t max_chars) {
    std::string result;
    if (!text || max_chars == 0) {
        return result;
    }
    const unsigned char* p = reinterpret_cast<const unsigned char*>(text);
    size_t count = 0;
    while (*p && count < max_chars) {
        const int len = utf8_sequence_length(*p);
        if (len <= 0) {
            break;
        }
        result.append(reinterpret_cast<const char*>(p), static_cast<size_t>(len));
        p += len;
        ++count;
    }
    return result;
}
} // namespace

// =============================================================================
// TextInputField Implementation
// =============================================================================

void TextInputField::input_char(char c) {
    char buffer[2] = {c, '\0'};
    input_text(buffer);
}

void TextInputField::input_text(const char* text_input) {
    if (!text_input || !text_input[0]) {
        return;
    }
    if (!is_valid_utf8(text_input)) {
        return;
    }
    if (!is_valid_utf8(text.c_str())) {
        return;
    }

    cursor_pos = clamp_cursor_to_boundary(text, cursor_pos);

    const size_t current_len = utf8_length(text.c_str());
    const size_t insert_len = utf8_length(text_input);
    if (insert_len == 0) {
        return;
    }
    if (current_len >= static_cast<size_t>(max_length)) {
        return;
    }
    const size_t remaining = static_cast<size_t>(max_length) - current_len;
    const std::string insert_text = (insert_len > remaining) ? utf8_prefix(text_input, remaining) : std::string(text_input);
    if (insert_text.empty()) {
        return;
    }

    std::string candidate = text;
    candidate.insert(static_cast<size_t>(cursor_pos), insert_text);
    if (!is_valid_utf8(candidate.c_str())) {
        return;
    }

    text = std::move(candidate);
    cursor_pos += static_cast<int>(insert_text.size());
}

void TextInputField::backspace() {
    cursor_pos = clamp_cursor_to_boundary(text, cursor_pos);
    if (cursor_pos <= 0) {
        return;
    }
    const int prev_pos = previous_utf8_boundary(text, cursor_pos);
    const int erase_len = cursor_pos - prev_pos;
    if (erase_len <= 0) {
        return;
    }
    text.erase(static_cast<size_t>(prev_pos), static_cast<size_t>(erase_len));
    cursor_pos = prev_pos;
}

void TextInputField::delete_char() {
    cursor_pos = clamp_cursor_to_boundary(text, cursor_pos);
    if (cursor_pos >= static_cast<int>(text.length())) {
        return;
    }
    const int next_pos = next_utf8_boundary(text, cursor_pos);
    const int erase_len = next_pos - cursor_pos;
    if (erase_len <= 0) {
        return;
    }
    text.erase(static_cast<size_t>(cursor_pos), static_cast<size_t>(erase_len));
}

void TextInputField::cursor_left() {
    cursor_pos = previous_utf8_boundary(text, cursor_pos);
}

void TextInputField::cursor_right() {
    cursor_pos = next_utf8_boundary(text, cursor_pos);
}

void TextInputField::clear() {
    text.clear();
    cursor_pos = 0;
}

// =============================================================================
// Button Implementation
// =============================================================================

bool Button::contains_point(int x, int y) const {
    return x >= bounds.x && x < bounds.x + bounds.width &&
           y >= bounds.y && y < bounds.y + bounds.height;
}

// =============================================================================
// LoginScreen Implementation
// =============================================================================

LoginScreen::LoginScreen(SDLRenderer& renderer, UIRenderer& ui_renderer, ResourceManager& resource_manager)
    : renderer_(renderer)
    , ui_renderer_(ui_renderer)
    , resource_manager_(resource_manager)
{
    // Initialize fields
    username_field_.placeholder = "Username";
    username_field_.max_length = 20;
    
    password_field_.placeholder = "Password";
    password_field_.password = true;
    password_field_.max_length = 20;
    
    create_name_field_.placeholder = "Character Name";
    create_name_field_.max_length = 12;
    
    // Initialize buttons
    offline_button_.text = "Offline Play";
    confirm_create_button_.text = "Create";
    cancel_create_button_.text = "Cancel";

    if (!layout_loader_.load_from_file(kUILayoutPath)) {
        std::cerr << "[LoginScreen] Falling back to built-in layout for login screen.\n";
    }

    state_context_ = std::make_unique<LoginStateContext>(LoginStateContext{
        renderer_,
        ui_renderer_,
        resource_manager_,
        layout_loader_,
        width_,
        height_,
        LoginStateEnterReason::Transition,
        username_field_,
        password_field_,
        login_confirm_bounds_,
        offline_button_,
        character_slots_,
        selected_character_index_,
        create_name_field_,
        create_class_,
        create_gender_,
        confirm_create_button_,
        cancel_create_button_,
        selected_create_class_index_,
        selected_create_gender_index_,
        class_panel_visible_,
        class_panel_visibility_,
        create_button_bounds_,
        class_panel_bounds_,
        class_select_bounds_,
        preview_area_bounds_,
        start_game_bounds_,
        created_name_bounds_,
        created_level_bounds_,
        created_class_bounds_,
        cursor_blink_timer_,
        cursor_visible_,
        login_animation_playing_,
        login_animation_frame_,
        login_animation_timer_,
        login_animation_frame_time_,
        pending_username_,
        pending_password_,
        preview_animation_frame_,
        preview_animation_timer_,
        preview_animation_frame_time_,
        background_texture_,
        create_background_texture_,
        class_panel_texture_,
        class_select_textures_,
        class_highlight_textures_,
        login_animation_frames_,
        character_preview_frames_,
        on_login_,
        on_character_select_,
        on_character_create_,
        on_start_game_,
        on_offline_play_,
        created_character_name_,
        created_character_class_,
        created_character_gender_,
        created_character_level_,
        has_created_character_,
        error_message_,
        error_timer_,
        status_text_,
        [this](LoginScreenState state) { set_state(state); },
        [this](const std::string& msg) { set_error(msg); }
    });

    transition_to(build_state(LoginScreenState::LOGIN));
}

void LoginScreen::set_state_machine(const SceneStateMachine* state_machine) {
    state_machine_ = state_machine;
}

bool LoginScreen::is_active() const {
    return state_machine_ ? state_machine_->is_in_login_flow() : true;
}

void LoginScreen::set_background_texture(std::shared_ptr<Texture> texture) {
    background_texture_ = std::move(texture);
    refresh_layout();
}

void LoginScreen::set_login_animation_frames(std::vector<AnimationFrame> frames) {
    login_animation_frames_ = std::move(frames);
}

void LoginScreen::set_create_background_texture(std::shared_ptr<Texture> texture) {
    create_background_texture_ = std::move(texture);
    refresh_layout();
}


void LoginScreen::set_class_panel_texture(std::shared_ptr<Texture> texture) {
    class_panel_texture_ = std::move(texture);
}

void LoginScreen::set_class_select_textures(std::vector<std::shared_ptr<Texture>> textures) {
    class_select_textures_ = std::move(textures);
}

void LoginScreen::set_class_highlight_textures(std::vector<std::shared_ptr<Texture>> textures) {
    class_highlight_textures_ = std::move(textures);
}

void LoginScreen::set_created_character_info(const std::string& name, CharacterClass char_class, Gender gender, int level) {
    if (name.empty()) {
        created_character_name_.clear();
        created_character_level_ = 0;
        created_character_class_ = CharacterClass::WARRIOR;
        created_character_gender_ = Gender::MALE;
        has_created_character_ = false;
        return;
    }

    created_character_name_ = name;
    created_character_class_ = char_class;
    created_character_gender_ = gender;
    created_character_level_ = level;
    has_created_character_ = true;

    if (state_context_) {
        load_character_preview_on_demand(*state_context_, char_class, gender);
    }
    preview_animation_frame_ = 0;
    preview_animation_timer_ = 0.0f;
}

void LoginScreen::set_character_preview_frames(CharacterClass char_class, Gender gender, std::vector<AnimationFrame> frames) {
    int class_idx = static_cast<int>(char_class);
    int gender_idx = static_cast<int>(gender);
    if (class_idx >= 0 && class_idx < 3 && gender_idx >= 0 && gender_idx < 2) {
        int index = class_idx * 2 + gender_idx;
        character_preview_frames_[index] = std::move(frames);
    }
}

void LoginScreen::set_dimensions(int width, int height) {
    width_ = width;
    height_ = height;
    refresh_layout();
}

void LoginScreen::set_state(LoginScreenState state) {
    clear_error();
    transition_to(build_state(state));
}

void LoginScreen::set_character_list(const std::vector<CharacterData>& characters) {
    character_slots_.clear();
    selected_character_index_ = -1;
    
    for (const auto& char_data : characters) {
        CharacterSlot slot;
        slot.data = char_data;
        slot.empty = false;
        character_slots_.push_back(slot);
    }
    
    // Add empty slots up to max
    while (character_slots_.size() < 3) {
        CharacterSlot slot;
        slot.empty = true;
        character_slots_.push_back(slot);
    }

    if (current_state_ && current_state_->get_state_type() == LoginScreenState::CHARACTER_SELECT) {
        refresh_layout();
    }
}

void LoginScreen::set_error(const std::string& error) {
    error_message_ = error;
    error_timer_ = 5.0f;  // Show for 5 seconds
}

void LoginScreen::set_status(const char* status) {
    status_text_ = status ? status : "";
}

void LoginScreen::clear_error() {
    error_message_.clear();
    error_timer_ = 0.0f;
}

bool LoginScreen::on_mouse_move(int x, int y) {
    if (!current_state_) {
        return false;
    }
    SDL_Event event{};
    event.type = SDL_MOUSEMOTION;
    event.motion.x = x;
    event.motion.y = y;
    return current_state_->handle_event(event);
}

bool LoginScreen::on_mouse_button_down(int button, int x, int y) {
    return on_mouse_click(x, y, button);
}

bool LoginScreen::on_mouse_button_up(int button, int x, int y) {
    if (!current_state_) {
        return false;
    }
    SDL_Event event{};
    event.type = SDL_MOUSEBUTTONUP;
    event.button.button = static_cast<Uint8>(button);
    event.button.x = x;
    event.button.y = y;
    return current_state_->handle_event(event);
}

bool LoginScreen::on_mouse_wheel(int x, int y) {
    if (!current_state_) {
        return false;
    }
    SDL_Event event{};
    event.type = SDL_MOUSEWHEEL;
    event.wheel.x = x;
    event.wheel.y = y;
    return current_state_->handle_event(event);
}

bool LoginScreen::on_mouse_click(int x, int y, int button) {
    if (button != SDL_BUTTON_LEFT) {
        return false;
    }
    if (!current_state_) {
        return false;
    }
    SDL_Event event{};
    event.type = SDL_MOUSEBUTTONDOWN;
    event.button.button = static_cast<Uint8>(button);
    event.button.x = x;
    event.button.y = y;
    return current_state_->handle_event(event);
}

bool LoginScreen::on_key_down(SDL_Scancode key, SDL_Keycode keycode, bool repeat) {
    (void)key;
    (void)repeat;
    return on_key_press(keycode);
}

bool LoginScreen::on_key_up(SDL_Scancode key, SDL_Keycode keycode) {
    if (!current_state_) {
        return false;
    }
    SDL_Event event{};
    event.type = SDL_KEYUP;
    event.key.keysym.scancode = key;
    event.key.keysym.sym = keycode;
    return current_state_->handle_event(event);
}

bool LoginScreen::on_key_press(SDL_Keycode key) {
    if (!current_state_) {
        return false;
    }
    SDL_Event event{};
    event.type = SDL_KEYDOWN;
    event.key.keysym.sym = key;
    return current_state_->handle_event(event);
}

bool LoginScreen::on_text_input(const char* text) {
    if (!current_state_ || !text || !text[0]) {
        return false;
    }
    SDL_Event event{};
    event.type = SDL_TEXTINPUT;
    std::strncpy(event.text.text, text, sizeof(event.text.text) - 1);
    event.text.text[sizeof(event.text.text) - 1] = '\0';
    return current_state_->handle_event(event);
}

void LoginScreen::update(float delta_time) {
    cursor_blink_timer_ += delta_time;
    if (cursor_blink_timer_ >= 0.5f) {
        cursor_blink_timer_ = 0.0f;
        cursor_visible_ = !cursor_visible_;
    }

    if (error_timer_ > 0.0f) {
        error_timer_ -= delta_time;
        if (error_timer_ <= 0.0f) {
            clear_error();
        }
    }

    if (current_state_) {
        current_state_->update(delta_time);
    }
}

void LoginScreen::render() {
    if (current_state_) {
        current_state_->render(ui_renderer_);
    }

    // Error overlay (used even if we're not in ERROR state).
    if (!error_message_.empty() && error_timer_ > 0.0f) {
        const int padding = 10;
        const int text_w = ui_renderer_.get_text_width(error_message_);
        const int text_h = ui_renderer_.get_text_height();

        const int box_w = std::min(width_ - 20, text_w + padding * 2);
        const int box_h = text_h + padding * 2;
        const int box_x = (width_ - box_w) / 2;
        const int box_y = std::max(10, height_ - box_h - 20);

        ui_renderer_.draw_panel({box_x, box_y, box_w, box_h}, {80, 20, 20, 200}, {200, 80, 80, 255});
        ui_renderer_.draw_text(error_message_, box_x + padding, box_y + padding, {255, 220, 220, 255});
    }
}

std::unique_ptr<ILoginState> LoginScreen::build_state(LoginScreenState state) {
    if (!state_context_) {
        return nullptr;
    }

    switch (state) {
        case LoginScreenState::LOGIN:
            return std::make_unique<LoginInputState>(*state_context_);
        case LoginScreenState::CHARACTER_SELECT:
            return std::make_unique<CharacterSelectState>(*state_context_);
        case LoginScreenState::CHARACTER_CREATE:
            return std::make_unique<CharacterCreateState>(*state_context_);
        case LoginScreenState::CONNECTING:
            return std::make_unique<ConnectingState>(*state_context_);
        case LoginScreenState::ERROR:
            return std::make_unique<ErrorState>(*state_context_);
        default:
            break;
    }

    return std::make_unique<LoginInputState>(*state_context_);
}

void LoginScreen::transition_to(std::unique_ptr<ILoginState> new_state) {
    if (!new_state) {
        return;
    }

    // Exit old state, then enter the new state (state type updated before on_enter).
    if (current_state_) {
        current_state_->on_exit();
    }

    current_state_ = std::move(new_state);
    state_ = current_state_->get_state_type();
    if (state_context_) {
        state_context_->enter_reason = LoginStateEnterReason::Transition;
    }
    current_state_->on_enter();
}

void LoginScreen::refresh_layout() {
    if (!current_state_ || !state_context_) {
        return;
    }
    state_context_->enter_reason = LoginStateEnterReason::LayoutRefresh;
    current_state_->on_enter();
    state_context_->enter_reason = LoginStateEnterReason::Transition;
}

} // namespace mir2::ui::screens
