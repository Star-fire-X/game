#include "ui/states/login_state_helpers.h"

#include "ui/login_screen.h"

#include <algorithm>
#include <cmath>
#include <utility>
#include <SDL.h>

namespace mir2::ui::screens {

void render_login_field_text(IFieldRenderContext& context, const TextInputField& field, uint8_t alpha) {
    render_field_text(context, field, field.password, alpha);
}

void render_field_text(IFieldRenderContext& context, const TextInputField& field, bool mask, uint8_t alpha) {
    auto& ui_renderer = context.get_ui_renderer();
    const float ui_scale = ui_renderer.get_ui_scale();
    const int padding_x = std::max(1, static_cast<int>(2 * ui_scale));

    int text_height = ui_renderer.get_text_height();
    int text_x = field.bounds.x + padding_x;
    int text_y = field.bounds.y + std::max(0, (field.bounds.height - text_height) / 2);

    if (field.text.empty() && !field.focused) {
        return;
    }

    std::string display_text = mask ? std::string(field.text.length(), '*') : field.text;
    ui_renderer.draw_text(display_text, text_x, text_y, {255, 255, 255, alpha});

    if (field.focused && context.is_cursor_visible()) {
        std::string text_before_cursor = mask ? std::string(field.cursor_pos, '*') : field.text.substr(0, field.cursor_pos);
        int cursor_x = text_x + ui_renderer.get_text_width(text_before_cursor);
        int cursor_w = std::max(1, static_cast<int>(2 * ui_scale));
        ui_renderer.draw_panel({cursor_x, text_y, cursor_w, text_height},
                               {255, 255, 255, alpha}, {255, 255, 255, alpha});
    }
}

void render_text_field(IFieldRenderContext& context, const TextInputField& field) {
    auto& ui_renderer = context.get_ui_renderer();
    Color bg_color = field.focused ? Color{50, 50, 70, 255} : Color{40, 40, 60, 255};
    Color border_color = field.focused ? Color{100, 150, 255, 255} : Color{80, 80, 120, 255};

    ui_renderer.draw_panel(field.bounds, bg_color, border_color);

    int text_x = field.bounds.x + 8;
    int text_height = ui_renderer.get_text_height();
    int text_y = field.bounds.y + (field.bounds.height - text_height) / 2;

    if (field.text.empty() && !field.focused) {
        ui_renderer.draw_text(field.placeholder, text_x, text_y, {100, 100, 120, 255});
        return;
    }

    std::string display_text = field.password ? std::string(field.text.length(), '*') : field.text;
    ui_renderer.draw_text(display_text, text_x, text_y, {255, 255, 255, 255});

    if (field.focused && context.is_cursor_visible()) {
        std::string text_before_cursor = field.password ?
            std::string(field.cursor_pos, '*') : field.text.substr(0, field.cursor_pos);
        int cursor_x = text_x + ui_renderer.get_text_width(text_before_cursor);
        ui_renderer.draw_panel({cursor_x, text_y, 2, text_height},
                               {255, 255, 255, 255}, {255, 255, 255, 255});
    }
}

void render_button(IRenderProvider& context, const Button& button) {
    auto& ui_renderer = context.get_ui_renderer();
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

    ui_renderer.draw_panel(button.bounds, bg_color, border_color);

    int text_width = static_cast<int>(button.text.length()) * 8;
    int text_x = button.bounds.x + (button.bounds.width - text_width) / 2;
    int text_y = button.bounds.y + (button.bounds.height - 16) / 2;

    ui_renderer.draw_text(button.text, text_x, text_y, text_color);
}

void render_common_background(IBackgroundRenderContext& context) {
    auto& renderer = context.get_renderer();
    renderer.clear(Color::black());

    auto background = context.get_background_texture();
    if (background && background->valid()) {
        Rect bg_src = {0, 0, background->width(), background->height()};
        Rect bg_dst = {0, 0, context.get_screen_width(), context.get_screen_height()};
        renderer.draw_texture(*background, bg_src, bg_dst);
    }
}

void render_login_background(ILoginAnimationRenderContext& context) {
    render_common_background(context);

    const auto& login_frames = context.get_login_animation_frames();
    if (!context.is_login_animation_playing() || login_frames.empty()) {
        return;
    }

    const int frame_index = context.get_login_animation_frame();
    if (frame_index < 0 || frame_index >= static_cast<int>(login_frames.size())) {
        return;
    }

    const auto& anim_frame = login_frames[frame_index];
    const auto& texture = anim_frame.texture;
    if (!texture || !texture->valid()) {
        return;
    }

    auto background = context.get_background_texture();
    const int bg_w = (background && background->valid())
                     ? background->width() : layout::DEFAULT_SCREEN_WIDTH;
    const int bg_h = (background && background->valid())
                     ? background->height() : layout::DEFAULT_SCREEN_HEIGHT;

    double scale_x = (bg_w > 0) ? static_cast<double>(context.get_screen_width()) / static_cast<double>(bg_w) : 1.0;
    double scale_y = (bg_h > 0) ? static_cast<double>(context.get_screen_height()) / static_cast<double>(bg_h) : 1.0;

    int scaled_w = static_cast<int>(std::lround(static_cast<double>(texture->width()) * scale_x));
    int scaled_h = static_cast<int>(std::lround(static_cast<double>(texture->height()) * scale_y));

    constexpr double kLoginAnimDesignTopY = 96.0;
    constexpr double kLoginAnimDesignHeight = static_cast<double>(layout::DEFAULT_SCREEN_HEIGHT);

    int dst_x = (context.get_screen_width() - scaled_w) / 2;
    int dst_y = static_cast<int>(std::lround(static_cast<double>(context.get_screen_height()) * (kLoginAnimDesignTopY / kLoginAnimDesignHeight)));

    dst_x += static_cast<int>(std::lround(static_cast<double>(anim_frame.offset_x) * scale_x));
    dst_y += static_cast<int>(std::lround(static_cast<double>(anim_frame.offset_y) * scale_y));

    Rect src = {0, 0, texture->width(), texture->height()};
    Rect dst = {dst_x, dst_y, scaled_w, scaled_h};
    context.get_renderer().draw_texture(*texture, src, dst);
}

void render_character_create_screen(ICharacterCreateStateContext& context) {
    auto& renderer = context.get_renderer();
    auto& ui_renderer = context.get_ui_renderer();
    renderer.clear(Color::black());

    std::shared_ptr<Texture> bg_texture = context.get_background_texture();
    auto create_background = context.get_create_background_texture();
    if (create_background && create_background->valid()) {
        bg_texture = create_background;
    }

    if (bg_texture && bg_texture->valid()) {
        Rect bg_src = {0, 0, bg_texture->width(), bg_texture->height()};
        Rect bg_dst = {0, 0, context.get_screen_width(), context.get_screen_height()};
        renderer.draw_texture(*bg_texture, bg_src, bg_dst);
    }

    if (!create_background || !create_background->valid()) {
        const std::string msg = "Character creation resources not loaded";
        int x = (context.get_screen_width() - ui_renderer.get_text_width(msg)) / 2;
        int y = context.get_screen_height() / 2;
        ui_renderer.draw_text(msg, x, y, {255, 100, 100, 255});
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
        renderer.draw_texture(*texture, src, dst);
        SDL_SetTextureAlphaMod(sdl_texture, previous_alpha);
    };

    const bool panel_visible = context.is_class_panel_visible();
    const int selected_class_index = context.get_selected_class_index();
    const int selected_gender_index = context.get_selected_gender_index();

    if ((panel_visible && selected_class_index >= 0 && selected_gender_index >= 0) ||
        (!panel_visible && context.has_created_character())) {
        CharacterClass preview_class = context.get_create_class();
        Gender preview_gender = context.get_create_gender();
        if (!panel_visible && context.has_created_character()) {
            preview_class = context.get_created_character_class();
            preview_gender = context.get_created_character_gender();
        }
        const int class_idx = static_cast<int>(preview_class);
        const int gender_idx = static_cast<int>(preview_gender);
        const int index = class_idx * 2 + gender_idx;
        if (index >= 0 && index < layout::MAX_CHARACTER_PREVIEW_SLOTS) {
            const auto& frames = context.get_character_preview_frames(index);
            if (!frames.empty()) {
                const int frame_idx = context.get_preview_animation_frame() % static_cast<int>(frames.size());
                const auto& frame = frames[frame_idx];
                const auto& tex = frame.texture;
                if (tex && tex->valid()) {
                    const Rect& preview_bounds = context.get_preview_area_bounds();
                    const double scale_x = (tex->width() > 0)
                        ? static_cast<double>(preview_bounds.width) / static_cast<double>(tex->width()) : 1.0;
                    const double scale_y = (tex->height() > 0)
                        ? static_cast<double>(preview_bounds.height) / static_cast<double>(tex->height()) : 1.0;
                    const double scale = std::clamp(std::min(scale_x, scale_y), 1.0, 4.0);

                    const int dst_w = static_cast<int>(std::lround(static_cast<double>(tex->width()) * scale));
                    const int dst_h = static_cast<int>(std::lround(static_cast<double>(tex->height()) * scale));
                    const int dst_x = preview_bounds.x + (preview_bounds.width - dst_w) / 2;
                    const int dst_y = preview_bounds.y + (preview_bounds.height - dst_h);

                    Rect src = {0, 0, tex->width(), tex->height()};
                    Rect dst = {dst_x, dst_y, dst_w, dst_h};
                    renderer.draw_texture(*tex, src, dst);
                }
            }
        }
    }

    if (context.has_created_character()) {
        const int padding_x = std::max(1, static_cast<int>(2 * ui_renderer.get_ui_scale()));

        const Rect& name_bounds = context.get_created_name_bounds();
        const int name_x = name_bounds.x + padding_x;
        const int name_y = name_bounds.y + std::max(0, (name_bounds.height - ui_renderer.get_text_height()) / 2);
        ui_renderer.draw_text(context.get_created_character_name(), name_x, name_y, {255, 255, 255, 255});

        const Rect& level_bounds = context.get_created_level_bounds();
        const std::string level_text = std::to_string(context.get_created_character_level());
        const int level_x = level_bounds.x + padding_x;
        const int level_y = level_bounds.y + std::max(0, (level_bounds.height - ui_renderer.get_text_height()) / 2);
        ui_renderer.draw_text(level_text, level_x, level_y, {255, 255, 255, 255});

        const Rect& class_bounds = context.get_created_class_bounds();
        const char* class_text = class_display_name(context.get_created_character_class());
        const int class_x = class_bounds.x + padding_x;
        const int class_y = class_bounds.y + std::max(0, (class_bounds.height - ui_renderer.get_text_height()) / 2);
        ui_renderer.draw_text(class_text, class_x, class_y, {255, 255, 255, 255});
    }

    const float panel_visibility = std::clamp(context.get_class_panel_visibility(), 0.0f, 1.0f);
    const uint8_t panel_alpha = static_cast<uint8_t>(std::lround(255.0f * panel_visibility));
    if (panel_alpha == 0) {
        return;
    }

    draw_texture_scaled(context.get_class_panel_texture(), context.get_class_panel_bounds(), panel_alpha);

    auto& name_field = context.get_create_name_field();
    render_login_field_text(context, name_field, panel_alpha);

    const auto& highlight_textures = context.get_class_highlight_textures();
    for (int i = 0; i < 5; ++i) {
        bool selected = false;
        std::shared_ptr<Texture> icon_tex;

        if (i < 3) {
            selected = (selected_class_index >= 0 && selected_class_index == i);
            if (selected && i < static_cast<int>(highlight_textures.size())) {
                icon_tex = highlight_textures[i];
            }
        } else {
            selected = (selected_gender_index >= 0 && selected_gender_index == (i - 3));
            if (selected && i < static_cast<int>(highlight_textures.size())) {
                icon_tex = highlight_textures[i];
            }
        }

        if (selected && icon_tex && icon_tex->valid()) {
            draw_texture_scaled(icon_tex, context.get_class_select_bounds(i), panel_alpha);
        }
    }

    if (name_field.text.empty()) {
        const uint8_t overlay_alpha = static_cast<uint8_t>(std::lround(120.0f * panel_visibility));
        renderer.draw_rect(context.get_confirm_create_button().bounds, {0, 0, 0, overlay_alpha});
    }
}

bool handle_character_create_click(ICharacterCreateStateContext& context, int x, int y) {
    auto create_background = context.get_create_background_texture();
    if (!create_background || !create_background->valid()) {
        return false;
    }

    if (x < 0 || y < 0 || x >= context.get_screen_width() || y >= context.get_screen_height()) {
        return false;
    }

    const bool panel_animating = context.get_class_panel_visibility() > 0.0f;

    if (!context.is_class_panel_visible()) {
        if (panel_animating) {
            return true;
        }
        if (context.get_start_game_bounds().contains(x, y)) {
            if (context.has_created_character()) {
                context.invoke_start_game();
            }
            return true;
        }
        if (context.get_create_button_bounds().contains(x, y)) {
            context.set_class_panel_visible(true);
            auto& name_field = context.get_create_name_field();
            name_field.focused = true;
            name_field.clear();
            return true;
        }
        return false;
    }

    if (context.get_cancel_create_button().bounds.contains(x, y)) {
        context.set_class_panel_visible(false);
        context.get_create_name_field().focused = false;
        context.set_preview_animation_frame(0);
        context.set_preview_animation_timer(0.0f);
        context.set_selected_class_index(-1);
        context.set_selected_gender_index(-1);
        if (!context.has_created_character()) {
            for (int i = 0; i < layout::MAX_CHARACTER_PREVIEW_SLOTS; ++i) {
                context.get_character_preview_frames(i).clear();
            }
        }
        return true;
    }

    if (context.get_create_name_field().bounds.contains(x, y)) {
        context.get_create_name_field().focused = true;
        return true;
    }

    if (context.get_confirm_create_button().bounds.contains(x, y)) {
        if (context.get_create_name_field().text.empty()) {
            context.set_error("Character name cannot be empty");
            return true;
        }
        if (context.get_selected_class_index() < 0 || context.get_selected_gender_index() < 0) {
            context.set_error("Please select class and gender");
            return true;
        }
        auto result = validate_character_name(context.get_create_name_field().text);
        if (!result.valid) {
            context.set_error(result.error_message);
            return true;
        }
        context.invoke_character_create(context.get_create_name_field().text,
                                        context.get_create_class(),
                                        context.get_create_gender());
        context.set_class_panel_visible(false);
        context.get_create_name_field().focused = false;
        context.set_preview_animation_frame(0);
        context.set_preview_animation_timer(0.0f);
        return true;
    }

    for (int i = 0; i < 5; ++i) {
        if (!context.get_class_select_bounds(i).contains(x, y)) {
            continue;
        }
        bool selection_changed = false;
        if (i < 3) {
            selection_changed = (context.get_selected_class_index() != i);
            context.set_selected_class_index(i);
            context.set_create_class(static_cast<CharacterClass>(i));
        } else {
            selection_changed = (context.get_selected_gender_index() != (i - 3));
            context.set_selected_gender_index(i - 3);
            context.set_create_gender(static_cast<Gender>(i - 3));
        }
        if (selection_changed && context.get_selected_class_index() >= 0 &&
            context.get_selected_gender_index() >= 0) {
            load_character_preview_on_demand(context, context.get_create_class(), context.get_create_gender());
            context.set_preview_animation_frame(0);
            context.set_preview_animation_timer(0.0f);
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

void load_character_preview_on_demand(ICharacterCreateStateContext& context, CharacterClass char_class, Gender gender) {
    int class_idx = static_cast<int>(char_class);
    int gender_idx = static_cast<int>(gender);
    if (class_idx < 0 || class_idx >= 3 || gender_idx < 0 || gender_idx >= 2) {
        return;
    }

    int index = class_idx * 2 + gender_idx;
    if (!context.get_character_preview_frames(index).empty()) {
        return;
    }

    auto& resource_manager = context.get_resource_manager();
    const char* archive_name = nullptr;
    if (resource_manager.is_archive_loaded("ChrSel")) {
        archive_name = "ChrSel";
    } else if (resource_manager.is_archive_loaded("chrsel")) {
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

    std::vector<AnimationFrame> frames;
    frames.reserve(static_cast<size_t>(std::max(0, end - start + 1)));

    for (int idx = start; idx <= end; ++idx) {
        auto sprite_opt = resource_manager.get_sprite(archive_name, idx);
        if (!sprite_opt || !sprite_opt->is_valid()) {
            continue;
        }

        mir2::client::Sprite sprite = *sprite_opt;
        if (sprite.width <= 1 && sprite.height <= 1) {
            continue;
        }

        auto tex = context.get_renderer().create_texture_from_sprite(sprite, true);
        if (!tex) {
            continue;
        }

        AnimationFrame frame;
        frame.texture = std::move(tex);
        frame.offset_x = sprite.offset_x;
        frame.offset_y = sprite.offset_y;
        frames.push_back(std::move(frame));
    }

    if (!frames.empty()) {
        context.get_character_preview_frames(index) = std::move(frames);
    }
}

const char* class_display_name(CharacterClass char_class) {
    switch (char_class) {
        case CharacterClass::WARRIOR:
            return "武士";
        case CharacterClass::MAGE:
            return "法师";
        case CharacterClass::TAOIST:
            return "道士";
        default:
            return "未知";
    }
}

} // namespace mir2::ui::screens
