// Legend2 Login Screen Implementation
// Login and character selection/creation UI

#include "ui/login_screen.h"
#include "ui/states/login_input_state.h"
#include "ui/states/character_select_state.h"
#include "ui/states/character_create_state.h"
#include "ui/states/connecting_state.h"
#include "ui/states/error_state.h"
#include "ui/states/login_state_helpers.h"
#include "ui/states/login_state_interface.h"
#include "common/utf8_utils.h"
#include "common/types/constants.h"
#include "client/resource/resource_loader.h"
#include "scene/scene_manager.h"
#include <algorithm>
#include <cstring>
#include <iostream>

namespace mir2::ui::screens {

LoginScreen::~LoginScreen() = default;

namespace {
constexpr const char* kUILayoutPath = "Data/ui_layout.json";
constexpr int kClassSelectCount = 5;

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

    cursor_pos = mir2::common::clamp_cursor_to_boundary(text, cursor_pos);

    const size_t current_len = utf8_length(text.c_str());
    const size_t insert_len = utf8_length(text_input);
    if (insert_len == 0) {
        return;
    }
    if (current_len >= static_cast<size_t>(max_length)) {
        return;
    }
    const size_t remaining = static_cast<size_t>(max_length) - current_len;
    const std::string insert_text = (insert_len > remaining) ? mir2::common::utf8_prefix(text_input, remaining) : std::string(text_input);
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
    cursor_pos = mir2::common::clamp_cursor_to_boundary(text, cursor_pos);
    if (cursor_pos <= 0) {
        return;
    }
    const int prev_pos = mir2::common::previous_utf8_boundary(text, cursor_pos);
    const int erase_len = cursor_pos - prev_pos;
    if (erase_len <= 0) {
        return;
    }
    text.erase(static_cast<size_t>(prev_pos), static_cast<size_t>(erase_len));
    cursor_pos = prev_pos;
}

void TextInputField::delete_char() {
    cursor_pos = mir2::common::clamp_cursor_to_boundary(text, cursor_pos);
    if (cursor_pos >= static_cast<int>(text.length())) {
        return;
    }
    const int next_pos = mir2::common::next_utf8_boundary(text, cursor_pos);
    const int erase_len = next_pos - cursor_pos;
    if (erase_len <= 0) {
        return;
    }
    text.erase(static_cast<size_t>(cursor_pos), static_cast<size_t>(erase_len));
}

void TextInputField::cursor_left() {
    cursor_pos = mir2::common::previous_utf8_boundary(text, cursor_pos);
}

void TextInputField::cursor_right() {
    cursor_pos = mir2::common::next_utf8_boundary(text, cursor_pos);
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
    username_field_.max_length = static_cast<int>(mir2::common::constants::LOGIN_USERNAME_MAX_LENGTH);
    
    password_field_.placeholder = "Password";
    password_field_.password = true;
    password_field_.max_length = static_cast<int>(mir2::common::constants::LOGIN_PASSWORD_MAX_LENGTH);
    
    create_name_field_.placeholder = "Character Name";
    create_name_field_.max_length = 12;
    
    // Initialize buttons
    offline_button_.text = "Offline Play";
    confirm_create_button_.text = "Create";
    cancel_create_button_.text = "Cancel";

    if (!layout_loader_.load_from_file(kUILayoutPath)) {
        std::cerr << "[LoginScreen] Falling back to built-in layout for login screen.\n";
    }

    transition_to(build_state(LoginScreenState::LOGIN));
}

SDLRenderer& LoginScreen::get_renderer() const {
    return renderer_;
}

UIRenderer& LoginScreen::get_ui_renderer() const {
    return ui_renderer_;
}

UILayoutLoader& LoginScreen::get_layout_loader() const {
    return layout_loader_;
}

int LoginScreen::get_screen_width() const {
    return width_;
}

int LoginScreen::get_screen_height() const {
    return height_;
}

LoginStateEnterReason LoginScreen::get_enter_reason() const {
    return enter_reason_;
}

const std::string& LoginScreen::get_status_text() const {
    return status_text_;
}

const std::string& LoginScreen::get_error_message() const {
    return error_message_;
}

TextInputField& LoginScreen::get_username_field() {
    return username_field_;
}

TextInputField& LoginScreen::get_password_field() {
    return password_field_;
}

Rect& LoginScreen::get_login_confirm_bounds() {
    return login_confirm_bounds_;
}

Button& LoginScreen::get_offline_button() {
    return offline_button_;
}

std::vector<CharacterSlot>& LoginScreen::get_character_slots() {
    return character_slots_;
}

void LoginScreen::set_selected_character_index(int index) {
    selected_character_index_ = index;
}

TextInputField& LoginScreen::get_create_name_field() {
    return create_name_field_;
}

void LoginScreen::set_create_class(CharacterClass c) {
    create_class_ = c;
}

void LoginScreen::set_create_gender(Gender g) {
    create_gender_ = g;
}

Button& LoginScreen::get_confirm_create_button() {
    return confirm_create_button_;
}

Button& LoginScreen::get_cancel_create_button() {
    return cancel_create_button_;
}

int LoginScreen::get_selected_class_index() const {
    return selected_create_class_index_;
}

void LoginScreen::set_selected_class_index(int idx) {
    selected_create_class_index_ = idx;
}

int LoginScreen::get_selected_gender_index() const {
    return selected_create_gender_index_;
}

void LoginScreen::set_selected_gender_index(int idx) {
    selected_create_gender_index_ = idx;
}

bool LoginScreen::is_class_panel_visible() const {
    return class_panel_visible_;
}

void LoginScreen::set_class_panel_visible(bool visible) {
    class_panel_visible_ = visible;
}

float LoginScreen::get_class_panel_visibility() const {
    return class_panel_visibility_;
}

void LoginScreen::set_class_panel_visibility(float v) {
    class_panel_visibility_ = v;
}

Rect& LoginScreen::get_create_button_bounds() {
    return create_button_bounds_;
}

Rect& LoginScreen::get_class_panel_bounds() {
    return class_panel_bounds_;
}

Rect& LoginScreen::get_class_select_bounds(int index) {
    if (index < 0 || index >= kClassSelectCount) {
        return invalid_bounds_;
    }
    return class_select_bounds_[index];
}

Rect& LoginScreen::get_preview_area_bounds() {
    return preview_area_bounds_;
}

Rect& LoginScreen::get_start_game_bounds() {
    return start_game_bounds_;
}

Rect& LoginScreen::get_created_name_bounds() {
    return created_name_bounds_;
}

Rect& LoginScreen::get_created_level_bounds() {
    return created_level_bounds_;
}

Rect& LoginScreen::get_created_class_bounds() {
    return created_class_bounds_;
}

const std::string& LoginScreen::get_created_character_name() const {
    return created_character_name_;
}

CharacterClass LoginScreen::get_created_character_class() const {
    return created_character_class_;
}

Gender LoginScreen::get_created_character_gender() const {
    return created_character_gender_;
}

int LoginScreen::get_created_character_level() const {
    return created_character_level_;
}

bool LoginScreen::has_created_character() const {
    return has_created_character_;
}

bool LoginScreen::is_cursor_visible() const {
    return cursor_visible_;
}

bool LoginScreen::is_login_animation_playing() const {
    return login_animation_playing_;
}

void LoginScreen::set_login_animation_playing(bool playing) {
    login_animation_playing_ = playing;
}

void LoginScreen::start_login_animation(const std::string& username, const std::string& password) {
    pending_username_ = username;
    pending_password_ = password;
    login_animation_playing_ = true;
    login_animation_frame_ = 0;
    login_animation_timer_ = 0.0f;
}

int LoginScreen::get_login_animation_frame() const {
    return login_animation_frame_;
}

void LoginScreen::set_login_animation_frame(int frame) {
    login_animation_frame_ = frame;
}

float LoginScreen::get_login_animation_timer() const {
    return login_animation_timer_;
}

void LoginScreen::set_login_animation_timer(float timer) {
    login_animation_timer_ = timer;
}

float LoginScreen::get_login_animation_frame_time() const {
    return login_animation_frame_time_;
}

const std::string& LoginScreen::get_pending_username() const {
    return pending_username_;
}

const std::string& LoginScreen::get_pending_password() const {
    return pending_password_;
}

void LoginScreen::clear_pending_credentials() {
    if (!pending_username_.empty()) {
        std::fill(pending_username_.begin(), pending_username_.end(), '\0');
        pending_username_.clear();
        pending_username_.shrink_to_fit();
    }
    if (!pending_password_.empty()) {
        std::fill(pending_password_.begin(), pending_password_.end(), '\0');
        pending_password_.clear();
        pending_password_.shrink_to_fit();
    }
}

int LoginScreen::get_preview_animation_frame() const {
    return preview_animation_frame_;
}

void LoginScreen::set_preview_animation_frame(int frame) {
    preview_animation_frame_ = frame;
}

float LoginScreen::get_preview_animation_timer() const {
    return preview_animation_timer_;
}

void LoginScreen::set_preview_animation_timer(float timer) {
    preview_animation_timer_ = timer;
}

float LoginScreen::get_preview_animation_frame_time() const {
    return preview_animation_frame_time_;
}

ResourceManager& LoginScreen::get_resource_manager() {
    return resource_manager_;
}

std::shared_ptr<Texture> LoginScreen::get_background_texture() const {
    return background_texture_;
}

std::shared_ptr<Texture> LoginScreen::get_create_background_texture() const {
    return create_background_texture_;
}

std::shared_ptr<Texture> LoginScreen::get_class_panel_texture() const {
    return class_panel_texture_;
}

const std::vector<std::shared_ptr<Texture>>& LoginScreen::get_class_select_textures() const {
    return class_select_textures_;
}

const std::vector<std::shared_ptr<Texture>>& LoginScreen::get_class_highlight_textures() const {
    return class_highlight_textures_;
}

const std::vector<AnimationFrame>& LoginScreen::get_login_animation_frames() const {
    return login_animation_frames_;
}

std::vector<AnimationFrame>& LoginScreen::get_character_preview_frames(int index) {
    if (index < 0 || index >= layout::MAX_CHARACTER_PREVIEW_SLOTS) {
        return invalid_preview_frames_;
    }
    return character_preview_frames_[index];
}

const std::vector<AnimationFrame>& LoginScreen::get_character_preview_frames(int index) const {
    if (index < 0 || index >= layout::MAX_CHARACTER_PREVIEW_SLOTS) {
        return invalid_preview_frames_;
    }
    return character_preview_frames_[index];
}

void LoginScreen::transition_to(LoginScreenState state) {
    set_state(state);
}

void LoginScreen::invoke_login(const std::string& username, const std::string& password) {
    if (on_login_) {
        on_login_(username, password);
    }
}

void LoginScreen::invoke_character_select(uint32_t character_id) {
    if (on_character_select_) {
        on_character_select_(character_id);
    }
}

void LoginScreen::invoke_character_create(const std::string& name, CharacterClass c, Gender g) {
    if (on_character_create_) {
        on_character_create_(name, c, g);
    }
}

void LoginScreen::invoke_start_game() {
    if (on_start_game_) {
        on_start_game_();
    }
}

void LoginScreen::invoke_offline_play() {
    if (on_offline_play_) {
        on_offline_play_();
    }
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

    load_character_preview_on_demand(*this, char_class, gender);
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
    switch (state) {
        case LoginScreenState::LOGIN:
            return std::make_unique<LoginInputState>(*this);
        case LoginScreenState::CHARACTER_SELECT:
            return std::make_unique<CharacterSelectState>(*this);
        case LoginScreenState::CHARACTER_CREATE:
            return std::make_unique<CharacterCreateState>(*this);
        case LoginScreenState::CONNECTING:
            return std::make_unique<ConnectingState>(*this);
        case LoginScreenState::ERROR:
            return std::make_unique<ErrorState>(*this);
        default:
            break;
    }

    return std::make_unique<LoginInputState>(*this);
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
    enter_reason_ = LoginStateEnterReason::Transition;
    current_state_->on_enter();
}

void LoginScreen::refresh_layout() {
    if (!current_state_) {
        return;
    }
    enter_reason_ = LoginStateEnterReason::LayoutRefresh;
    current_state_->on_enter();
    enter_reason_ = LoginStateEnterReason::Transition;
}

} // namespace mir2::ui::screens
