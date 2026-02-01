#include "ui/states/character_create_state.h"

#include "ui/states/login_state_helpers.h"
#include "ui/ui_layout_calculator.h"
#include "ui/ui_layout_constants.h"

#include <algorithm>
#include <cmath>

namespace mir2::ui::screens {

namespace {
constexpr float kClassPanelTransitionDuration = 0.2f;
} // namespace

CharacterCreateState::CharacterCreateState(LoginStateContext& context)
    : context_(context) {}

void CharacterCreateState::on_enter() {
    layout();

    if (context_.enter_reason == LoginStateEnterReason::LayoutRefresh) {
        return;
    }

    context_.create_name_field.clear();
    context_.create_name_field.focused = false;
    context_.create_class = CharacterClass::WARRIOR;
    context_.create_gender = Gender::MALE;
    context_.class_panel_visible = false;
    context_.class_panel_visibility = 0.0f;
    context_.selected_create_class_index = -1;
    context_.selected_create_gender_index = -1;
    context_.preview_animation_frame = 0;
    context_.preview_animation_timer = 0.0f;

    if (context_.has_created_character) {
        load_character_preview_on_demand(context_, context_.created_character_class, context_.created_character_gender);
    }
}

void CharacterCreateState::on_exit() {}

void CharacterCreateState::update(float delta_time) {
    const float target_visibility = context_.class_panel_visible ? 1.0f : 0.0f;
    if (context_.class_panel_visibility < target_visibility) {
        context_.class_panel_visibility = std::min(
            target_visibility,
            context_.class_panel_visibility + (delta_time / kClassPanelTransitionDuration));
    } else if (context_.class_panel_visibility > target_visibility) {
        context_.class_panel_visibility = std::max(
            target_visibility,
            context_.class_panel_visibility - (delta_time / kClassPanelTransitionDuration));
    }

    bool has_preview = false;
    CharacterClass preview_class = context_.create_class;
    Gender preview_gender = context_.create_gender;

    if (context_.class_panel_visible && context_.selected_create_class_index >= 0 &&
        context_.selected_create_gender_index >= 0) {
        has_preview = true;
    } else if (context_.has_created_character) {
        has_preview = true;
        preview_class = context_.created_character_class;
        preview_gender = context_.created_character_gender;
    }

    if (!has_preview) {
        return;
    }

    const int class_idx = static_cast<int>(preview_class);
    const int gender_idx = static_cast<int>(preview_gender);
    const int index = class_idx * 2 + gender_idx;

    if (index < 0 || index >= 6) {
        return;
    }

    auto& frames = context_.character_preview_frames[index];
    if (frames.empty()) {
        return;
    }

    context_.preview_animation_timer += delta_time;
    if (context_.preview_animation_timer >= context_.preview_animation_frame_time) {
        context_.preview_animation_timer = 0.0f;
        context_.preview_animation_frame = (context_.preview_animation_frame + 1) % static_cast<int>(frames.size());
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
        case SDL_TEXTINPUT:
            if (!context_.create_name_field.focused) {
                return false;
            }
            if (!event.text.text[0]) {
                return false;
            }
            if (!is_valid_utf8(event.text.text)) {
                return true;
            }
            {
                const unsigned char* p = reinterpret_cast<const unsigned char*>(event.text.text);
                while (*p) {
                    if (*p < 0x20 || *p == 0x7F) {
                        return true;
                    }
                    ++p;
                }
            }
            context_.create_name_field.input_text(event.text.text);
            return true;
        default:
            break;
    }

    return false;
}

void CharacterCreateState::layout() {
    const int bg_w = (context_.create_background_texture && context_.create_background_texture->valid())
                     ? context_.create_background_texture->width()
                     : layout::character_create::DESIGN_WIDTH;
    const int bg_h = (context_.create_background_texture && context_.create_background_texture->valid())
                     ? context_.create_background_texture->height()
                     : layout::character_create::DESIGN_HEIGHT;

    mir2::ui::UILayoutCalculator calc(bg_w, bg_h, context_.screen_width, context_.screen_height);

    context_.create_button_bounds = calc.scale_rect(layout::character_create::CREATE_BUTTON);
    context_.class_panel_bounds = calc.scale_rect(layout::character_create::CLASS_PANEL);

    for (int i = 0; i < 5; ++i) {
        context_.class_select_bounds[i] = calc.scale_rect(layout::character_create::CLASS_ICONS[i]);
    }

    context_.preview_area_bounds = calc.scale_rect(layout::character_create::PREVIEW_AREA);
    context_.create_name_field.bounds = calc.scale_rect(layout::character_create::NAME_FIELD);
    context_.confirm_create_button.bounds = calc.scale_rect(layout::character_create::CONFIRM_BUTTON);
    context_.cancel_create_button.bounds = calc.scale_rect(layout::character_create::CANCEL_BUTTON);
    context_.start_game_bounds = calc.scale_rect(layout::character_create::START_GAME);
    context_.created_name_bounds = calc.scale_rect(layout::character_create::CREATED_NAME);
    context_.created_level_bounds = calc.scale_rect(layout::character_create::CREATED_LEVEL);
    context_.created_class_bounds = calc.scale_rect(layout::character_create::CREATED_CLASS);
}


bool CharacterCreateState::handle_key_press(SDL_Keycode key) {
    switch (key) {
        case SDLK_ESCAPE:
            if (context_.transition_to) {
                // Transition: character create -> character select.
                context_.transition_to(LoginScreenState::CHARACTER_SELECT);
            }
            return true;
        case SDLK_BACKSPACE:
            if (context_.create_name_field.focused) {
                context_.create_name_field.backspace();
                return true;
            }
            break;
        case SDLK_DELETE:
            if (context_.create_name_field.focused) {
                context_.create_name_field.delete_char();
                return true;
            }
            break;
        case SDLK_LEFT:
            if (context_.create_name_field.focused) {
                context_.create_name_field.cursor_left();
                return true;
            }
            break;
        case SDLK_RIGHT:
            if (context_.create_name_field.focused) {
                context_.create_name_field.cursor_right();
                return true;
            }
            break;
        case SDLK_HOME:
            if (context_.create_name_field.focused) {
                context_.create_name_field.cursor_pos = 0;
                return true;
            }
            break;
        case SDLK_END:
            if (context_.create_name_field.focused) {
                context_.create_name_field.cursor_pos = static_cast<int>(context_.create_name_field.text.length());
                return true;
            }
            break;
        default:
            break;
    }

    return false;
}

} // namespace mir2::ui::screens
