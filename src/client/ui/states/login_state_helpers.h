#ifndef LEGEND2_LOGIN_STATE_HELPERS_H
#define LEGEND2_LOGIN_STATE_HELPERS_H

#include "ui/login_screen.h"

namespace mir2::ui::screens {

void render_field_text(const LoginStateContext& context, const TextInputField& field, bool mask, uint8_t alpha = 255);
void render_login_field_text(const LoginStateContext& context, const TextInputField& field, uint8_t alpha = 255);
void render_text_field(const LoginStateContext& context, const TextInputField& field);
void render_button(const LoginStateContext& context, const Button& button);
void render_login_background(const LoginStateContext& context);
void render_character_create_screen(const LoginStateContext& context);
bool handle_character_create_click(LoginStateContext& context, int x, int y);

void update_button_hover(Button& button, int mouse_x, int mouse_y);
void update_slot_hover(CharacterSlot& slot, int mouse_x, int mouse_y);

void load_character_preview_on_demand(LoginStateContext& context, CharacterClass char_class, Gender gender);
const char* class_display_name(CharacterClass char_class);

} // namespace mir2::ui::screens

#endif // LEGEND2_LOGIN_STATE_HELPERS_H
