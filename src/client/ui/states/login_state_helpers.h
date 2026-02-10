#ifndef LEGEND2_LOGIN_STATE_HELPERS_H
#define LEGEND2_LOGIN_STATE_HELPERS_H

#include "ui/states/login_state_context.h"

namespace mir2::ui::screens {

void render_field_text(IFieldRenderContext& context, const TextInputField& field, bool mask, uint8_t alpha = 255);
void render_login_field_text(IFieldRenderContext& context, const TextInputField& field, uint8_t alpha = 255);
void render_text_field(IFieldRenderContext& context, const TextInputField& field);
void render_button(IRenderProvider& context, const Button& button);
void render_common_background(IBackgroundRenderContext& context);
void render_login_background(ILoginAnimationRenderContext& context);
void render_character_create_screen(ICharacterCreateStateContext& context);
bool handle_character_create_click(ICharacterCreateStateContext& context, int x, int y);

void update_button_hover(Button& button, int mouse_x, int mouse_y);
void update_slot_hover(CharacterSlot& slot, int mouse_x, int mouse_y);

void load_character_preview_on_demand(ICharacterCreateStateContext& context, CharacterClass char_class, Gender gender);
const char* class_display_name(CharacterClass char_class);

} // namespace mir2::ui::screens

#endif // LEGEND2_LOGIN_STATE_HELPERS_H
