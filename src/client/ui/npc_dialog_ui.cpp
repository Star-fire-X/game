#include "ui/npc_dialog_ui.h"

#include "ui/ui_layout_calculator.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace mir2::ui {

namespace {
constexpr int kDesignWidth = 800;
constexpr int kDesignHeight = 600;

constexpr Rect kDialogRect = {80, 110, 640, 360};
constexpr int kPadding = 20;
constexpr int kPortraitSize = 96;
constexpr int kNameHeight = 24;
constexpr int kTextGap = 12;
constexpr int kButtonHeight = 28;
constexpr int kButtonSpacing = 8;
constexpr int kCloseSize = 20;
constexpr int kLineSpacing = 4;

const Color kOverlayColor = {0, 0, 0, 120};
const Color kPanelColor = {20, 20, 20, 230};
const Color kPanelBorder = {180, 180, 180, 255};
const Color kPortraitFill = {30, 30, 30, 255};
const Color kTextColor = {230, 230, 230, 255};
const Color kNameColor = {255, 220, 120, 255};
const Color kCloseTextColor = {230, 230, 230, 255};

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
    return 1;
}

size_t next_utf8_index(const std::string& text, size_t index) {
    if (index >= text.size()) {
        return text.size();
    }
    const unsigned char lead = static_cast<unsigned char>(text[index]);
    const int len = utf8_sequence_length(lead);
    const size_t next = index + static_cast<size_t>(len);
    if (next > text.size()) {
        return index + 1;
    }
    return next;
}

size_t prev_utf8_index(const std::string& text, size_t index) {
    if (text.empty() || index == 0) {
        return 0;
    }
    size_t i = std::min(index, text.size());
    if (i == 0) {
        return 0;
    }
    --i;
    while (i > 0 && (static_cast<unsigned char>(text[i]) & 0xC0) == 0x80) {
        --i;
    }
    return i;
}

bool is_break_char(const std::string& chunk) {
    return chunk == " " || chunk == "\t";
}

void trim_left_spaces(std::string& text) {
    size_t pos = 0;
    while (pos < text.size() && (text[pos] == ' ' || text[pos] == '\t')) {
        ++pos;
    }
    if (pos > 0) {
        text.erase(0, pos);
    }
}

void trim_right_spaces(std::string& text) {
    while (!text.empty()) {
        char c = text.back();
        if (c != ' ' && c != '\t') {
            break;
        }
        text.pop_back();
    }
}

size_t find_last_break_pos(const std::string& text) {
    size_t pos = text.find_last_of(" \t");
    if (pos == std::string::npos) {
        return std::string::npos;
    }
    return pos + 1; // break after space
}

void append_ellipsis(std::string& line, int max_width, UIRenderer& renderer) {
    const std::string ellipsis = "...";
    const int ellipsis_width = renderer.get_text_width(ellipsis);
    if (ellipsis_width <= 0 || max_width <= 0) {
        return;
    }

    while (!line.empty() && renderer.get_text_width(line) + ellipsis_width > max_width) {
        const size_t prev = prev_utf8_index(line, line.size());
        line.erase(prev);
    }

    if (line.empty()) {
        line = ellipsis;
        return;
    }
    line += ellipsis;
}

std::vector<std::string> wrap_text(const std::string& text, int max_width, UIRenderer& renderer) {
    std::vector<std::string> lines;
    if (text.empty()) {
        return lines;
    }
    if (max_width <= 0) {
        lines.push_back(text);
        return lines;
    }

    std::string current_line;
    size_t last_break = std::string::npos;

    auto push_line = [&](std::string line) {
        trim_right_spaces(line);
        lines.push_back(line);
    };

    size_t i = 0;
    while (i < text.size()) {
        char c = text[i];
        if (c == '\n' || c == '\r') {
            if (c == '\r' && i + 1 < text.size() && text[i + 1] == '\n') {
                ++i;
            }
            push_line(current_line);
            current_line.clear();
            last_break = std::string::npos;
            ++i;
            continue;
        }

        size_t next = next_utf8_index(text, i);
        std::string chunk = text.substr(i, next - i);
        current_line += chunk;
        if (is_break_char(chunk)) {
            last_break = current_line.size();
        }

        if (renderer.get_text_width(current_line) > max_width) {
            if (last_break != std::string::npos && last_break > 0) {
                std::string line = current_line.substr(0, last_break);
                trim_right_spaces(line);
                lines.push_back(line);

                current_line = current_line.substr(last_break);
                trim_left_spaces(current_line);
                last_break = find_last_break_pos(current_line);
            } else {
                const size_t chunk_start = current_line.size() - chunk.size();
                if (chunk_start > 0) {
                    std::string line = current_line.substr(0, chunk_start);
                    trim_right_spaces(line);
                    lines.push_back(line);
                    current_line = chunk;
                } else {
                    lines.push_back(current_line);
                    current_line.clear();
                }
                last_break = find_last_break_pos(current_line);
            }
        }
        i = next;
    }

    if (!current_line.empty() || (!text.empty() && (text.back() == '\n' || text.back() == '\r'))) {
        push_line(current_line);
    }

    return lines;
}
} // namespace

NpcDialogUI::NpcDialogUI(SDLRenderer& renderer, UIRenderer& ui_renderer, EventDispatcher* dispatcher)
    : renderer_(renderer)
    , ui_renderer_(ui_renderer)
    , event_dispatcher_(dispatcher) {
}

void NpcDialogUI::set_event_dispatcher(EventDispatcher* dispatcher) {
    event_dispatcher_ = dispatcher;
}

void NpcDialogUI::set_menu_select_callback(MenuSelectCallback callback) {
    menu_select_callback_ = std::move(callback);
}

void NpcDialogUI::set_dimensions(int width, int height) {
    width_ = width;
    height_ = height;
    layout_dirty_ = true;
}

void NpcDialogUI::show_dialog(uint64_t npc_id,
                              const std::string& npc_name,
                              const std::string& text,
                              const std::vector<std::string>& options) {
    npc_id_ = npc_id;
    npc_name_ = npc_name;
    dialog_text_ = text;
    menu_options_ = options;
    visible_ = true;
    layout_dirty_ = true;
    text_dirty_ = true;
    hovered_option_ = -1;
    pressed_option_ = -1;
    close_hovered_ = false;
    close_pressed_ = false;
}

void NpcDialogUI::update_dialog_text(const std::string& text) {
    dialog_text_ = text;
    text_dirty_ = true;
}

void NpcDialogUI::update_menu_options(const std::vector<std::string>& options) {
    menu_options_ = options;
    layout_dirty_ = true;
    text_dirty_ = true;
    hovered_option_ = -1;
    pressed_option_ = -1;
}

void NpcDialogUI::update_npc_name(const std::string& npc_name) {
    npc_name_ = npc_name;
}

void NpcDialogUI::set_portrait(std::shared_ptr<Texture> portrait) {
    portrait_texture_ = std::move(portrait);
}

void NpcDialogUI::hide_dialog() {
    visible_ = false;
    hovered_option_ = -1;
    pressed_option_ = -1;
    close_hovered_ = false;
    close_pressed_ = false;
}

bool NpcDialogUI::on_mouse_move(int x, int y) {
    if (!visible_) {
        return false;
    }
    if (layout_dirty_) {
        refresh_layout();
    }
    hovered_option_ = option_at(x, y);
    close_hovered_ = point_in_rect(close_bounds_, x, y);
    return true;
}

bool NpcDialogUI::on_mouse_button_down(int button, int x, int y) {
    if (!visible_) {
        return false;
    }
    if (layout_dirty_) {
        refresh_layout();
    }
    if (button != SDL_BUTTON_LEFT) {
        return true;
    }

    if (point_in_rect(close_bounds_, x, y)) {
        close_pressed_ = true;
        pressed_option_ = -1;
        return true;
    }

    pressed_option_ = option_at(x, y);
    close_pressed_ = false;
    return true;
}

bool NpcDialogUI::on_mouse_button_up(int button, int x, int y) {
    if (!visible_) {
        return false;
    }
    if (layout_dirty_) {
        refresh_layout();
    }
    if (button != SDL_BUTTON_LEFT) {
        return true;
    }

    if (close_pressed_ && point_in_rect(close_bounds_, x, y)) {
        hide_dialog();
        close_pressed_ = false;
        return true;
    }

    if (pressed_option_ >= 0) {
        const int released_index = option_at(x, y);
        if (released_index == pressed_option_) {
            send_menu_select(static_cast<uint8_t>(released_index));
        }
    }

    pressed_option_ = -1;
    close_pressed_ = false;
    return true;
}

bool NpcDialogUI::on_key_down(SDL_Scancode key, SDL_Keycode keycode, bool repeat) {
    (void)key;
    (void)repeat;
    if (!visible_) {
        return false;
    }
    if (keycode == SDLK_ESCAPE) {
        hide_dialog();
        return true;
    }
    return false;
}

bool NpcDialogUI::on_user_event(const SDL_UserEvent& event) {
    const auto type = static_cast<NpcUiEventType>(event.code);
    if (type == NpcUiEventType::kDialogShow) {
        if (!event.data1) {
            return false;
        }
        const auto* payload = static_cast<const NpcDialogEvent*>(event.data1);
        handle_dialog_event(*payload);
        return true;
    }
    if (type == NpcUiEventType::kInteractResponse) {
        if (!event.data1) {
            return false;
        }
        const auto* payload = static_cast<const NpcInteractEvent*>(event.data1);
        handle_interact_event(*payload);
        return true;
    }
    return false;
}

void NpcDialogUI::render() {
    if (!visible_) {
        return;
    }
    if (layout_dirty_) {
        refresh_layout();
    }
    if (text_dirty_) {
        rebuild_text_lines();
    }

    renderer_.set_blend_mode(SDL_BLENDMODE_BLEND);

    renderer_.draw_rect(screen_bounds_, kOverlayColor);
    ui_renderer_.draw_panel(dialog_bounds_, kPanelColor, kPanelBorder);

    if (portrait_texture_ && portrait_texture_->valid()) {
        Rect src{0, 0, portrait_texture_->width(), portrait_texture_->height()};
        renderer_.draw_texture(*portrait_texture_, src, portrait_bounds_);
        renderer_.draw_rect_outline(portrait_bounds_, kPanelBorder);
    } else {
        renderer_.draw_rect(portrait_bounds_, kPortraitFill);
        renderer_.draw_rect_outline(portrait_bounds_, kPanelBorder);
        const std::string label = "NPC";
        int label_width = ui_renderer_.get_text_width(label);
        int label_x = portrait_bounds_.x + (portrait_bounds_.width - label_width) / 2;
        int label_y = portrait_bounds_.y + (portrait_bounds_.height - text_line_height_) / 2;
        ui_renderer_.draw_text(label, label_x, label_y, kTextColor);
    }

    const std::string name = npc_name_.empty() ? "NPC" : npc_name_;
    ui_renderer_.draw_text(name, name_bounds_.x, name_bounds_.y, kNameColor);

    int text_y = text_bounds_.y;
    const int text_bottom = text_bounds_.y + text_bounds_.height;
    const int line_advance = text_line_height_ + text_line_spacing_;
    for (const auto& line : wrapped_lines_) {
        if (line.empty() && text_line_height_ <= 0) {
            continue;
        }
        if (text_y + text_line_height_ > text_bottom) {
            break;
        }
        ui_renderer_.draw_text(line, text_bounds_.x, text_y, kTextColor);
        text_y += line_advance;
    }

    for (size_t i = 0; i < option_bounds_.size(); ++i) {
        const Rect& bounds = option_bounds_[i];
        const bool hovered = static_cast<int>(i) == hovered_option_;
        const bool pressed = static_cast<int>(i) == pressed_option_;
        Color bg = {60, 60, 60, 220};
        if (pressed) {
            bg = {40, 40, 40, 220};
        } else if (hovered) {
            bg = {80, 80, 80, 220};
        }
        ui_renderer_.draw_panel(bounds, bg, kPanelBorder);

        const std::string& option_text = menu_options_[i];
        int text_width = ui_renderer_.get_text_width(option_text);
        int text_x = bounds.x + (bounds.width - text_width) / 2;
        int text_y = bounds.y + (bounds.height - text_line_height_) / 2;
        ui_renderer_.draw_text(option_text, text_x, text_y, kTextColor);
    }

    Color close_bg = {60, 60, 60, 220};
    if (close_pressed_) {
        close_bg = {40, 40, 40, 220};
    } else if (close_hovered_) {
        close_bg = {80, 80, 80, 220};
    }
    ui_renderer_.draw_panel(close_bounds_, close_bg, kPanelBorder);
    const std::string close_label = "X";
    int close_text_width = ui_renderer_.get_text_width(close_label);
    int close_text_x = close_bounds_.x + (close_bounds_.width - close_text_width) / 2;
    int close_text_y = close_bounds_.y + (close_bounds_.height - text_line_height_) / 2;
    ui_renderer_.draw_text(close_label, close_text_x, close_text_y, kCloseTextColor);

}

void NpcDialogUI::refresh_layout() {
    UILayoutCalculator calc(kDesignWidth, kDesignHeight, width_, height_);
    const double uniform_scale = std::min(calc.get_scale_x(), calc.get_scale_y());
    auto scale_uniform = [&](int value) {
        return static_cast<int>(std::lround(value * uniform_scale));
    };

    screen_bounds_ = {0, 0, width_, height_};
    dialog_bounds_ = calc.scale_rect(kDialogRect);

    const int padding = scale_uniform(kPadding);
    const int portrait_size = scale_uniform(kPortraitSize);
    const int name_height = scale_uniform(kNameHeight);
    const int close_size = scale_uniform(kCloseSize);

    portrait_bounds_ = {
        dialog_bounds_.x + padding,
        dialog_bounds_.y + padding,
        portrait_size,
        portrait_size
    };

    name_bounds_ = {
        portrait_bounds_.x + portrait_bounds_.width + padding,
        dialog_bounds_.y + padding + (portrait_bounds_.height - name_height) / 2,
        dialog_bounds_.width - (portrait_bounds_.width + padding * 2),
        name_height
    };

    close_bounds_ = {
        dialog_bounds_.x + dialog_bounds_.width - padding - close_size,
        dialog_bounds_.y + padding / 2,
        close_size,
        close_size
    };

    const int name_right = close_bounds_.x - padding / 2;
    if (name_right > name_bounds_.x) {
        name_bounds_.width = name_right - name_bounds_.x;
    } else {
        name_bounds_.width = 0;
    }

    const int text_start_x = dialog_bounds_.x + padding;
    const int text_start_y = portrait_bounds_.y + portrait_bounds_.height + padding / 2;
    const int text_width = dialog_bounds_.width - padding * 2;

    button_height_ = scale_uniform(kButtonHeight);
    button_spacing_ = scale_uniform(kButtonSpacing);
    text_line_spacing_ = scale_uniform(kLineSpacing);

    const int option_count = static_cast<int>(menu_options_.size());
    const int options_height = option_count > 0
        ? option_count * button_height_ + (option_count - 1) * button_spacing_
        : 0;
    const int text_gap = option_count > 0 ? scale_uniform(kTextGap) : 0;

    int text_bottom = dialog_bounds_.y + dialog_bounds_.height - padding - options_height - text_gap;
    if (text_bottom < text_start_y) {
        text_bottom = text_start_y;
    }

    text_bounds_ = {
        text_start_x,
        text_start_y,
        text_width,
        text_bottom - text_start_y
    };

    rebuild_option_bounds();
    layout_dirty_ = false;
    text_dirty_ = true;
}

void NpcDialogUI::rebuild_text_lines() {
    wrapped_lines_ = wrap_text(dialog_text_, text_bounds_.width, ui_renderer_);
    text_line_height_ = ui_renderer_.get_text_height();

    const int line_advance = text_line_height_ + text_line_spacing_;
    int max_lines = 0;
    if (text_bounds_.height > 0 && line_advance > 0) {
        max_lines = std::max(1, text_bounds_.height / line_advance);
    }

    if (max_lines > 0 && static_cast<int>(wrapped_lines_.size()) > max_lines) {
        wrapped_lines_.resize(static_cast<size_t>(max_lines));
        append_ellipsis(wrapped_lines_.back(), text_bounds_.width, ui_renderer_);
    }

    text_dirty_ = false;
}

void NpcDialogUI::rebuild_option_bounds() {
    option_bounds_.clear();
    const int option_count = static_cast<int>(menu_options_.size());
    if (option_count == 0) {
        return;
    }

    const int padding = std::max(0, (dialog_bounds_.width - text_bounds_.width) / 2);
    const int options_height = option_count * button_height_ + (option_count - 1) * button_spacing_;
    const int options_top = dialog_bounds_.y + dialog_bounds_.height - padding - options_height;

    for (int i = 0; i < option_count; ++i) {
        Rect bounds;
        bounds.x = dialog_bounds_.x + padding;
        bounds.y = options_top + i * (button_height_ + button_spacing_);
        bounds.width = dialog_bounds_.width - padding * 2;
        bounds.height = button_height_;
        option_bounds_.push_back(bounds);
    }
}

void NpcDialogUI::handle_dialog_event(const NpcDialogEvent& event) {
    npc_id_ = event.npc_id;
    dialog_text_ = event.text;
    if (event.has_menu) {
        menu_options_ = event.options;
    } else {
        menu_options_.clear();
    }
    visible_ = true;
    layout_dirty_ = true;
    text_dirty_ = true;
    hovered_option_ = -1;
    pressed_option_ = -1;
    close_hovered_ = false;
    close_pressed_ = false;
}

void NpcDialogUI::handle_interact_event(const NpcInteractEvent& event) {
    if (!event.npc_name.empty()) {
        npc_name_ = event.npc_name;
    }
    npc_id_ = event.npc_id;
    if (event.result != 0) {
        hide_dialog();
    }
}

void NpcDialogUI::send_menu_select(uint8_t option_index) {
    if (menu_select_callback_) {
        menu_select_callback_(npc_id_, option_index);
    }
    if (!event_dispatcher_) {
        return;
    }

    NpcMenuSelectEvent payload{npc_id_, option_index};

    SDL_Event event{};
    event.type = SDL_USEREVENT;
    event.user.code = static_cast<int>(NpcUiEventType::kMenuSelect);
    event.user.data1 = &payload;
    event.user.data2 = nullptr;

    event_dispatcher_->dispatch(event);
}

int NpcDialogUI::option_at(int x, int y) const {
    for (size_t i = 0; i < option_bounds_.size(); ++i) {
        if (point_in_rect(option_bounds_[i], x, y)) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

bool NpcDialogUI::point_in_rect(const Rect& rect, int x, int y) const {
    return x >= rect.x && x < rect.x + rect.width &&
           y >= rect.y && y < rect.y + rect.height;
}

} // namespace mir2::ui
