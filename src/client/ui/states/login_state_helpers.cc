#include "ui/states/login_state_helpers.h"

#include <algorithm>
#include <cmath>
#include <utility>
#include <SDL.h>

namespace mir2::ui::screens {

void render_login_field_text(const LoginStateContext& context, const TextInputField& field, uint8_t alpha) {
    render_field_text(context, field, field.password, alpha);
}

void render_field_text(const LoginStateContext& context, const TextInputField& field, bool mask, uint8_t alpha) {
    const float ui_scale = context.ui_renderer.get_ui_scale();
    const int padding_x = std::max(1, static_cast<int>(2 * ui_scale));

    int text_height = context.ui_renderer.get_text_height();
    int text_x = field.bounds.x + padding_x;
    int text_y = field.bounds.y + std::max(0, (field.bounds.height - text_height) / 2);

    if (field.text.empty() && !field.focused) {
        return;
    }

    std::string display_text = mask ? std::string(field.text.length(), '*') : field.text;
    context.ui_renderer.draw_text(display_text, text_x, text_y, {255, 255, 255, alpha});

    if (field.focused && context.cursor_visible) {
        std::string text_before_cursor = mask ? std::string(field.cursor_pos, '*') : field.text.substr(0, field.cursor_pos);
        int cursor_x = text_x + context.ui_renderer.get_text_width(text_before_cursor);
        int cursor_w = std::max(1, static_cast<int>(2 * ui_scale));
        context.ui_renderer.draw_panel({cursor_x, text_y, cursor_w, text_height},
                                       {255, 255, 255, alpha}, {255, 255, 255, alpha});
    }
}

void render_text_field(const LoginStateContext& context, const TextInputField& field) {
    Color bg_color = field.focused ? Color{50, 50, 70, 255} : Color{40, 40, 60, 255};
    Color border_color = field.focused ? Color{100, 150, 255, 255} : Color{80, 80, 120, 255};

    context.ui_renderer.draw_panel(field.bounds, bg_color, border_color);

    int text_x = field.bounds.x + 8;
    int text_height = context.ui_renderer.get_text_height();
    int text_y = field.bounds.y + (field.bounds.height - text_height) / 2;

    if (field.text.empty() && !field.focused) {
        context.ui_renderer.draw_text(field.placeholder, text_x, text_y, {100, 100, 120, 255});
        return;
    }

    std::string display_text = field.password ? std::string(field.text.length(), '*') : field.text;
    context.ui_renderer.draw_text(display_text, text_x, text_y, {255, 255, 255, 255});

    if (field.focused && context.cursor_visible) {
        std::string text_before_cursor = field.password ?
            std::string(field.cursor_pos, '*') : field.text.substr(0, field.cursor_pos);
        int cursor_x = text_x + context.ui_renderer.get_text_width(text_before_cursor);
        context.ui_renderer.draw_panel({cursor_x, text_y, 2, text_height},
                                       {255, 255, 255, 255}, {255, 255, 255, 255});
    }
}

void render_button(const LoginStateContext& context, const Button& button) {
    Color bg_color;
    Color border_color;
    Color text_color;

    if (!button.enabled) {
        bg_color = {40, 40, 50, 255};
        border_color = {60, 60, 70, 255};
        text_color = {100, 100, 100, 255};
    } else if (button.pressed) {
        bg_color = {40, 60, 100, 255};
        border_color = {80, 120, 200, 255};
        text_color = {255, 255, 255, 255};
    } else if (button.hovered) {
        bg_color = {60, 80, 120, 255};
        border_color = {100, 140, 220, 255};
        text_color = {255, 255, 255, 255};
    } else {
        bg_color = {50, 60, 90, 255};
        border_color = {80, 100, 160, 255};
        text_color = {220, 220, 220, 255};
    }

    context.ui_renderer.draw_panel(button.bounds, bg_color, border_color);

    int text_width = static_cast<int>(button.text.length()) * 8;
    int text_x = button.bounds.x + (button.bounds.width - text_width) / 2;
    int text_y = button.bounds.y + (button.bounds.height - 16) / 2;

    context.ui_renderer.draw_text(button.text, text_x, text_y, text_color);
}

void render_login_background(const LoginStateContext& context) {
    context.renderer.clear(Color::black());

    if (context.background_texture && context.background_texture->valid()) {
        Rect bg_src = {0, 0, context.background_texture->width(), context.background_texture->height()};
        Rect bg_dst = {0, 0, context.screen_width, context.screen_height};
        context.renderer.draw_texture(*context.background_texture, bg_src, bg_dst);
    }

    if (!context.login_animation_playing || context.login_animation_frames.empty()) {
        return;
    }

    const int frame_index = context.login_animation_frame;
    if (frame_index < 0 || frame_index >= static_cast<int>(context.login_animation_frames.size())) {
        return;
    }

    const auto& anim_frame = context.login_animation_frames[frame_index];
    const auto& texture = anim_frame.texture;
    if (!texture || !texture->valid()) {
        return;
    }

    const int bg_w = (context.background_texture && context.background_texture->valid())
                     ? context.background_texture->width() : 800;
    const int bg_h = (context.background_texture && context.background_texture->valid())
                     ? context.background_texture->height() : 600;

    double scale_x = (bg_w > 0) ? static_cast<double>(context.screen_width) / static_cast<double>(bg_w) : 1.0;
    double scale_y = (bg_h > 0) ? static_cast<double>(context.screen_height) / static_cast<double>(bg_h) : 1.0;

    int scaled_w = static_cast<int>(std::lround(static_cast<double>(texture->width()) * scale_x));
    int scaled_h = static_cast<int>(std::lround(static_cast<double>(texture->height()) * scale_y));

    constexpr double kLoginAnimDesignTopY = 96.0;
    constexpr double kLoginAnimDesignHeight = 600.0;

    int dst_x = (context.screen_width - scaled_w) / 2;
    int dst_y = static_cast<int>(std::lround(static_cast<double>(context.screen_height) * (kLoginAnimDesignTopY / kLoginAnimDesignHeight)));

    dst_x += static_cast<int>(std::lround(static_cast<double>(anim_frame.offset_x) * scale_x));
    dst_y += static_cast<int>(std::lround(static_cast<double>(anim_frame.offset_y) * scale_y));

    Rect src = {0, 0, texture->width(), texture->height()};
    Rect dst = {dst_x, dst_y, scaled_w, scaled_h};
    context.renderer.draw_texture(*texture, src, dst);
}

void render_character_create_screen(const LoginStateContext& context) {
    context.renderer.clear(Color::black());

    std::shared_ptr<Texture> bg_texture = context.background_texture;
    if (context.create_background_texture && context.create_background_texture->valid()) {
        bg_texture = context.create_background_texture;
    }

    if (bg_texture && bg_texture->valid()) {
        Rect bg_src = {0, 0, bg_texture->width(), bg_texture->height()};
        Rect bg_dst = {0, 0, context.screen_width, context.screen_height};
        context.renderer.draw_texture(*bg_texture, bg_src, bg_dst);
    }

    if (!context.create_background_texture || !context.create_background_texture->valid()) {
        const std::string msg = "Character creation resources not loaded";
        int x = (context.screen_width - context.ui_renderer.get_text_width(msg)) / 2;
        int y = context.screen_height / 2;
        context.ui_renderer.draw_text(msg, x, y, {255, 100, 100, 255});
        return;
    }

    auto draw_texture_scaled = [&](const std::shared_ptr<Texture>& texture, const Rect& dst, uint8_t alpha = 255) {
        if (!texture || !texture->valid()) {
            return;
        }
        SDL_Texture* sdl_texture = texture->get();
        Uint8 previous_alpha = 255;
        SDL_GetTextureAlphaMod(sdl_texture, &previous_alpha);
        SDL_SetTextureAlphaMod(sdl_texture, alpha);
        Rect src = {0, 0, texture->width(), texture->height()};
        context.renderer.draw_texture(*texture, src, dst);
        SDL_SetTextureAlphaMod(sdl_texture, previous_alpha);
    };

    if ((context.class_panel_visible && context.selected_create_class_index >= 0 &&
         context.selected_create_gender_index >= 0) ||
        (!context.class_panel_visible && context.has_created_character)) {
        CharacterClass preview_class = context.create_class;
        Gender preview_gender = context.create_gender;
        if (!context.class_panel_visible && context.has_created_character) {
            preview_class = context.created_character_class;
            preview_gender = context.created_character_gender;
        }
        const int class_idx = static_cast<int>(preview_class);
        const int gender_idx = static_cast<int>(preview_gender);
        const int index = class_idx * 2 + gender_idx;
        if (index >= 0 && index < 6) {
            const auto& frames = context.character_preview_frames[index];
            if (!frames.empty()) {
                const int frame_idx = context.preview_animation_frame % static_cast<int>(frames.size());
                const auto& frame = frames[frame_idx];
                const auto& tex = frame.texture;
                if (tex && tex->valid()) {
                    const double scale_x = (tex->width() > 0)
                        ? static_cast<double>(context.preview_area_bounds.width) / static_cast<double>(tex->width()) : 1.0;
                    const double scale_y = (tex->height() > 0)
                        ? static_cast<double>(context.preview_area_bounds.height) / static_cast<double>(tex->height()) : 1.0;
                    const double scale = std::clamp(std::min(scale_x, scale_y), 1.0, 4.0);

                    const int dst_w = static_cast<int>(std::lround(static_cast<double>(tex->width()) * scale));
                    const int dst_h = static_cast<int>(std::lround(static_cast<double>(tex->height()) * scale));
                    const int dst_x = context.preview_area_bounds.x + (context.preview_area_bounds.width - dst_w) / 2;
                    const int dst_y = context.preview_area_bounds.y + (context.preview_area_bounds.height - dst_h);

                    Rect src = {0, 0, tex->width(), tex->height()};
                    Rect dst = {dst_x, dst_y, dst_w, dst_h};
                    context.renderer.draw_texture(*tex, src, dst);
                }
            }
        }
    }

    if (context.has_created_character) {
        const int padding_x = std::max(1, static_cast<int>(2 * context.ui_renderer.get_ui_scale()));
        const int name_x = context.created_name_bounds.x + padding_x;
        const int name_y = context.created_name_bounds.y +
                           std::max(0, (context.created_name_bounds.height - context.ui_renderer.get_text_height()) / 2);
        context.ui_renderer.draw_text(context.created_character_name, name_x, name_y, {255, 255, 255, 255});

        const std::string level_text = std::to_string(context.created_character_level);
        const int level_x = context.created_level_bounds.x + padding_x;
        const int level_y = context.created_level_bounds.y +
                            std::max(0, (context.created_level_bounds.height - context.ui_renderer.get_text_height()) / 2);
        context.ui_renderer.draw_text(level_text, level_x, level_y, {255, 255, 255, 255});

        const char* class_text = class_display_name(context.created_character_class);
        const int class_x = context.created_class_bounds.x + padding_x;
        const int class_y = context.created_class_bounds.y +
                            std::max(0, (context.created_class_bounds.height - context.ui_renderer.get_text_height()) / 2);
        context.ui_renderer.draw_text(class_text, class_x, class_y, {255, 255, 255, 255});
    }

    const float panel_visibility = std::clamp(context.class_panel_visibility, 0.0f, 1.0f);
    const uint8_t panel_alpha = static_cast<uint8_t>(std::lround(255.0f * panel_visibility));
    if (panel_alpha == 0) {
        return;
    }

    draw_texture_scaled(context.class_panel_texture, context.class_panel_bounds, panel_alpha);
    render_login_field_text(context, context.create_name_field, panel_alpha);

    for (int i = 0; i < 5; ++i) {
        bool selected = false;
        std::shared_ptr<Texture> icon_tex;

        if (i < 3) {
            selected = (context.selected_create_class_index >= 0 && context.selected_create_class_index == i);
            if (selected && i < static_cast<int>(context.class_highlight_textures.size())) {
                icon_tex = context.class_highlight_textures[i];
            }
        } else {
            selected = (context.selected_create_gender_index >= 0 && context.selected_create_gender_index == (i - 3));
            if (selected && i < static_cast<int>(context.class_highlight_textures.size())) {
                icon_tex = context.class_highlight_textures[i];
            }
        }

        if (selected && icon_tex && icon_tex->valid()) {
            draw_texture_scaled(icon_tex, context.class_select_bounds[i], panel_alpha);
        }
    }

    if (context.create_name_field.text.empty()) {
        const uint8_t overlay_alpha = static_cast<uint8_t>(std::lround(120.0f * panel_visibility));
        context.renderer.draw_rect(context.confirm_create_button.bounds, {0, 0, 0, overlay_alpha});
    }
}

bool handle_character_create_click(LoginStateContext& context, int x, int y) {
    if (!context.create_background_texture || !context.create_background_texture->valid()) {
        return false;
    }

    if (x < 0 || y < 0 || x >= context.screen_width || y >= context.screen_height) {
        return false;
    }

    const bool panel_animating = context.class_panel_visibility > 0.0f;

    if (!context.class_panel_visible) {
        if (panel_animating) {
            return true;
        }
        if (context.start_game_bounds.contains(x, y)) {
            if (context.has_created_character && context.on_start_game) {
                context.on_start_game();
            }
            return true;
        }
        if (context.create_button_bounds.contains(x, y)) {
            context.class_panel_visible = true;
            context.create_name_field.focused = true;
            context.create_name_field.clear();
            return true;
        }
        return false;
    }

    if (context.cancel_create_button.bounds.contains(x, y)) {
        context.class_panel_visible = false;
        context.create_name_field.focused = false;
        context.preview_animation_frame = 0;
        context.preview_animation_timer = 0.0f;
        context.selected_create_class_index = -1;
        context.selected_create_gender_index = -1;
        if (!context.has_created_character) {
            for (auto& frames : context.character_preview_frames) {
                frames.clear();
            }
        }
        return true;
    }

    if (context.create_name_field.bounds.contains(x, y)) {
        context.create_name_field.focused = true;
        return true;
    }

    if (context.confirm_create_button.bounds.contains(x, y)) {
        if (context.create_name_field.text.empty()) {
            if (context.set_error) {
                context.set_error("Character name cannot be empty");
            }
            return true;
        }
        if (context.selected_create_class_index < 0 || context.selected_create_gender_index < 0) {
            if (context.set_error) {
                context.set_error("Please select class and gender");
            }
            return true;
        }
        auto result = validate_character_name(context.create_name_field.text);
        if (!result.valid) {
            if (context.set_error) {
                context.set_error(result.error_message);
            }
            return true;
        }
        if (context.on_character_create) {
            context.on_character_create(context.create_name_field.text,
                                        context.create_class,
                                        context.create_gender);
        }
        context.class_panel_visible = false;
        context.create_name_field.focused = false;
        context.preview_animation_frame = 0;
        context.preview_animation_timer = 0.0f;
        return true;
    }

    for (int i = 0; i < 5; ++i) {
        if (!context.class_select_bounds[i].contains(x, y)) {
            continue;
        }
        bool selection_changed = false;
        if (i < 3) {
            selection_changed = (context.selected_create_class_index != i);
            context.selected_create_class_index = i;
            context.create_class = static_cast<CharacterClass>(i);
        } else {
            selection_changed = (context.selected_create_gender_index != (i - 3));
            context.selected_create_gender_index = i - 3;
            context.create_gender = static_cast<Gender>(i - 3);
        }
        if (selection_changed && context.selected_create_class_index >= 0 &&
            context.selected_create_gender_index >= 0) {
            load_character_preview_on_demand(context, context.create_class, context.create_gender);
            context.preview_animation_frame = 0;
            context.preview_animation_timer = 0.0f;
        }
        return true;
    }

    return false;
}

void update_button_hover(Button& button, int mouse_x, int mouse_y) {
    button.hovered = button.contains_point(mouse_x, mouse_y);
}

void update_slot_hover(CharacterSlot& slot, int mouse_x, int mouse_y) {
    slot.hovered = slot.bounds.contains(mouse_x, mouse_y);
}

void load_character_preview_on_demand(LoginStateContext& context, CharacterClass char_class, Gender gender) {
    int class_idx = static_cast<int>(char_class);
    int gender_idx = static_cast<int>(gender);
    if (class_idx < 0 || class_idx >= 3 || gender_idx < 0 || gender_idx >= 2) {
        return;
    }

    int index = class_idx * 2 + gender_idx;
    if (!context.character_preview_frames[index].empty()) {
        return;
    }

    const char* archive_name = nullptr;
    if (context.resource_manager.is_archive_loaded("ChrSel")) {
        archive_name = "ChrSel";
    } else if (context.resource_manager.is_archive_loaded("chrsel")) {
        archive_name = "chrsel";
    } else {
        return;
    }

    const int start_map[3][2] = {
        {40, 160},
        {80, 200},
        {120, 240}
    };
    const int end_map[3][2] = {
        {55, 175},
        {95, 215},
        {135, 255}
    };

    const int start = start_map[class_idx][gender_idx];
    const int end = end_map[class_idx][gender_idx];

    std::vector<LoginScreen::AnimationFrame> frames;
    frames.reserve(static_cast<size_t>(std::max(0, end - start + 1)));

    for (int idx = start; idx <= end; ++idx) {
        auto sprite_opt = context.resource_manager.get_sprite(archive_name, idx);
        if (!sprite_opt || !sprite_opt->is_valid()) {
            continue;
        }

        mir2::client::Sprite sprite = *sprite_opt;
        if (sprite.width <= 1 && sprite.height <= 1) {
            continue;
        }

        auto tex = context.renderer.create_texture_from_sprite(sprite, true);
        if (!tex) {
            continue;
        }

        LoginScreen::AnimationFrame frame;
        frame.texture = std::move(tex);
        frame.offset_x = sprite.offset_x;
        frame.offset_y = sprite.offset_y;
        frames.push_back(std::move(frame));
    }

    if (!frames.empty()) {
        context.character_preview_frames[index] = std::move(frames);
    }
}

const char* class_display_name(CharacterClass char_class) {
    switch (char_class) {
        case CharacterClass::WARRIOR:
            return "姝﹀＋";
        case CharacterClass::MAGE:
            return "娉曞笀";
        case CharacterClass::TAOIST:
            return "閬撳＋";
        default:
            return "鏈煡";
    }
}

} // namespace mir2::ui::screens
