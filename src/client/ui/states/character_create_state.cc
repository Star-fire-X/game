#include "ui/states/character_create_state.h"

#include "ui/login_screen.h"
#include "ui/states/login_state_helpers.h"
#include "ui/ui_layout_calculator.h"
#include "ui/ui_layout_constants.h"

#include <algorithm>
#include <cmath>

namespace mir2::ui::screens {

namespace {
constexpr float kClassPanelTransitionDuration = 0.2f;
} // namespace

CharacterCreateState::CharacterCreateState(ICharacterCreateStateContext& context)
    : context_(context) {}

void CharacterCreateState::on_enter() {
    layout();

    if (context_.get_enter_reason() == LoginStateEnterReason::LayoutRefresh) {
        return;
    }

    auto& name_field = context_.get_create_name_field();
    name_field.clear();
    name_field.focused = false;
    context_.set_create_class(CharacterClass::WARRIOR);
    context_.set_create_gender(Gender::MALE);
    context_.set_class_panel_visible(false);
    context_.set_class_panel_visibility(0.0f);
    context_.set_selected_class_index(-1);
    context_.set_selected_gender_index(-1);
    context_.set_preview_animation_frame(0);
    context_.set_preview_animation_timer(0.0f);

    if (context_.has_created_character()) {
        load_character_preview_on_demand(context_, context_.get_created_character_class(), context_.get_created_character_gender());
    }
}

void CharacterCreateState::on_exit() {}

void CharacterCreateState::update(float delta_time) {
    const float target_visibility = context_.is_class_panel_visible() ? 1.0f : 0.0f;
    float current_visibility = context_.get_class_panel_visibility();
    if (current_visibility < target_visibility) {
        context_.set_class_panel_visibility(std::min(
            target_visibility,
            current_visibility + (delta_time / kClassPanelTransitionDuration)));
    } else if (current_visibility > target_visibility) {
        context_.set_class_panel_visibility(std::max(
            target_visibility,
            current_visibility - (delta_time / kClassPanelTransitionDuration)));
    }

    bool has_preview = false;
    CharacterClass preview_class = context_.get_create_class();
    Gender preview_gender = context_.get_create_gender();

    if (context_.is_class_panel_visible() && context_.get_selected_class_index() >= 0 &&
        context_.get_selected_gender_index() >= 0) {
        has_preview = true;
    } else if (context_.has_created_character()) {
        has_preview = true;
        preview_class = context_.get_created_character_class();
        preview_gender = context_.get_created_character_gender();
    }

    if (!has_preview) {
        return;
    }

    const int class_idx = static_cast<int>(preview_class);
    const int gender_idx = static_cast<int>(preview_gender);
    const int index = class_idx * 2 + gender_idx;

    if (index < 0 || index >= layout::MAX_CHARACTER_PREVIEW_SLOTS) {
        return;
    }

    const auto& frames = context_.get_character_preview_frames(index);
    if (frames.empty()) {
        return;
    }

    const float updated_timer = context_.get_preview_animation_timer() + delta_time;
    if (updated_timer >= context_.get_preview_animation_frame_time()) {
        context_.set_preview_animation_timer(0.0f);
        context_.set_preview_animation_frame(
            (context_.get_preview_animation_frame() + 1) % static_cast<int>(frames.size()));
    } else {
        context_.set_preview_animation_timer(updated_timer);
    }
}

void CharacterCreateState::render(UIRenderer& renderer) {
    (void)renderer;
    render_character_create_screen(context_);
}

bool CharacterCreateState::handle_event(const SDL_Event& event) {
    switch (event.type) {
        case SDL_MOUSEBUTTONDOWN:
            if (event.button.button != SDL_BUTTON_LEFT) {
                return false;
            }
            return handle_character_create_click(context_, event.button.x, event.button.y);
        case SDL_KEYDOWN:
            return handle_key_press(event.key.keysym.sym);
        case SDL_TEXTINPUT: {
            auto& name_field = context_.get_create_name_field();
            if (!name_field.focused) {
                return false;
            }
            if (!event.text.text[0]) {
                return false;
            }
            if (!is_valid_utf8(event.text.text)) {
                return true;
            }
            if (contains_control_characters(event.text.text)) {
                return true;
            }
            name_field.input_text(event.text.text);
            return true;
        }
        default:
            break;
    }

    return false;
}

void CharacterCreateState::layout() {
    auto create_bg = context_.get_create_background_texture();
    const int bg_w = (create_bg && create_bg->valid())
                     ? create_bg->width()
                     : layout::character_create::DESIGN_WIDTH;
    const int bg_h = (create_bg && create_bg->valid())
                     ? create_bg->height()
                     : layout::character_create::DESIGN_HEIGHT;

    mir2::ui::UILayoutCalculator calc(bg_w, bg_h, context_.get_screen_width(), context_.get_screen_height());

    context_.get_create_button_bounds() = calc.scale_rect(layout::character_create::CREATE_BUTTON);
    context_.get_class_panel_bounds() = calc.scale_rect(layout::character_create::CLASS_PANEL);

    for (int i = 0; i < 5; ++i) {
        context_.get_class_select_bounds(i) = calc.scale_rect(layout::character_create::CLASS_ICONS[i]);
    }

    context_.get_preview_area_bounds() = calc.scale_rect(layout::character_create::PREVIEW_AREA);
    context_.get_create_name_field().bounds = calc.scale_rect(layout::character_create::NAME_FIELD);
    context_.get_confirm_create_button().bounds = calc.scale_rect(layout::character_create::CONFIRM_BUTTON);
    context_.get_cancel_create_button().bounds = calc.scale_rect(layout::character_create::CANCEL_BUTTON);
    context_.get_start_game_bounds() = calc.scale_rect(layout::character_create::START_GAME);
    context_.get_created_name_bounds() = calc.scale_rect(layout::character_create::CREATED_NAME);
    context_.get_created_level_bounds() = calc.scale_rect(layout::character_create::CREATED_LEVEL);
    context_.get_created_class_bounds() = calc.scale_rect(layout::character_create::CREATED_CLASS);
}


bool CharacterCreateState::handle_key_press(SDL_Keycode key) {
    auto& name_field = context_.get_create_name_field();

    switch (key) {
        case SDLK_ESCAPE:
            context_.transition_to(LoginScreenState::CHARACTER_SELECT);
            return true;
        case SDLK_BACKSPACE:
            if (name_field.focused) {
                name_field.backspace();
                return true;
            }
            break;
        case SDLK_DELETE:
            if (name_field.focused) {
                name_field.delete_char();
                return true;
            }
            break;
        case SDLK_LEFT:
            if (name_field.focused) {
                name_field.cursor_left();
                return true;
            }
            break;
        case SDLK_RIGHT:
            if (name_field.focused) {
                name_field.cursor_right();
                return true;
            }
            break;
        case SDLK_HOME:
            if (name_field.focused) {
                name_field.cursor_pos = 0;
                return true;
            }
            break;
        case SDLK_END:
            if (name_field.focused) {
                name_field.cursor_pos = static_cast<int>(name_field.text.length());
                return true;
            }
            break;
        default:
            break;
    }

    return false;
}

} // namespace mir2::ui::screens
