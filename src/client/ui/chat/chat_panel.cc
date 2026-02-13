// =============================================================================
// Legend2 Chat UI - Chat Panel Implementation
// =============================================================================

#include "ui/chat/chat_panel.h"

#include <algorithm>
#include <cmath>
#include <sstream>

#include "common/types.h"

namespace mir2::ui::chat {

using mir2::common::Color;
using mir2::common::Rect;

// =============================================================================
// Color constants for the chat panel
// =============================================================================

namespace {

constexpr Color kPanelBackground{10, 10, 10, 180};
constexpr Color kPanelBorder{60, 60, 60, 200};
constexpr Color kInputBackground{20, 20, 20, 200};
constexpr Color kInputBorder{80, 80, 80, 220};
constexpr Color kCursorColor{220, 220, 220, 255};

// Channel display colors
constexpr Color kColorWorld{255, 255, 255, 255};    // White
constexpr Color kColorPrivate{255, 150, 200, 255};  // Pink
constexpr Color kColorGuild{100, 255, 100, 255};    // Green
constexpr Color kColorSystem{255, 255, 80, 255};    // Yellow
constexpr Color kColorTeam{100, 150, 255, 255};     // Blue
constexpr Color kColorArea{80, 255, 255, 255};       // Cyan

constexpr float kCursorBlinkInterval = 0.5f;

} // namespace

// =============================================================================
// Construction
// =============================================================================

ChatPanel::ChatPanel() {
    set_id("chat_panel");
    // Default position: bottom-left corner (caller should adjust for screen size)
    set_bounds({4, 380, kPanelWidth, kPanelHeight});
}

// =============================================================================
// Message API
// =============================================================================

void ChatPanel::add_message(const ChatMessageData& msg) {
    messages_.push_back(msg);
    while (messages_.size() > kMaxMessages) {
        messages_.pop_front();
    }
    // Auto-scroll to bottom when a new message arrives and we are already
    // at the bottom (scroll_offset_ == 0 means viewing newest).
    if (scroll_offset_ == 0) {
        // Already viewing newest -- nothing to adjust.
    }
}

void ChatPanel::add_system_message(const std::string& text) {
    ChatMessageData msg;
    msg.channel = ChatChannel::kSystem;
    msg.from_id = 0;
    msg.from_name = "System";
    msg.to_id = 0;
    msg.content = text;
    msg.color = 0xFFFF50FF;  // yellow-ish
    msg.timestamp = 0;
    add_message(msg);
}

// =============================================================================
// Channel filtering
// =============================================================================

void ChatPanel::set_active_channel(ChatChannel channel) {
    active_channel_ = channel;
}

void ChatPanel::set_filter_channel(std::optional<ChatChannel> channel) {
    filter_channel_ = channel;
    scroll_offset_ = 0;
}

// =============================================================================
// Update
// =============================================================================

void ChatPanel::update(float delta_time) {
    if (input_active_) {
        cursor_blink_timer_ += delta_time;
        if (cursor_blink_timer_ >= kCursorBlinkInterval) {
            cursor_blink_timer_ -= kCursorBlinkInterval;
            cursor_visible_ = !cursor_visible_;
        }
    }
}

// =============================================================================
// Rendering
// =============================================================================

void ChatPanel::render(UIRenderer& renderer) {
    if (!is_visible()) {
        return;
    }

    const Rect& bounds = get_bounds();

    // -- Background panel -----------------------------------------------------
    renderer.draw_panel(bounds, kPanelBackground, kPanelBorder);

    // -- Message area ---------------------------------------------------------
    // The message area occupies the panel above the input line.
    const int msg_area_x = bounds.x + kPadding;
    const int msg_area_y = bounds.y + kPadding;
    const int msg_area_width = bounds.width - kPadding * 2;
    const int msg_area_height = bounds.height - kInputHeight - kPadding * 3;
    const int max_visible_lines = msg_area_height / kMessageLineHeight;

    // Collect filtered messages
    std::vector<const ChatMessageData*> filtered;
    filtered.reserve(messages_.size());
    for (const auto& m : messages_) {
        if (!filter_channel_.has_value() || m.channel == filter_channel_.value()) {
            filtered.push_back(&m);
        }
    }

    // Determine the range of messages to display, accounting for scroll offset.
    // scroll_offset_ == 0 means the bottom of the list is visible (newest).
    const int total = static_cast<int>(filtered.size());
    const int end_index = total - scroll_offset_;  // exclusive upper bound
    const int start_index = std::max(0, end_index - max_visible_lines);

    // Draw each visible message line
    for (int i = start_index; i < end_index && i < total; ++i) {
        const ChatMessageData& msg = *filtered[static_cast<size_t>(i)];
        const int line_y = msg_area_y + (i - start_index) * kMessageLineHeight;

        const std::string line = format_message(msg);
        const Color color = channel_color(msg.channel);

        renderer.draw_text(line, msg_area_x, line_y, color);
    }

    // -- Scroll indicator (simple) --------------------------------------------
    if (scroll_offset_ > 0) {
        // Draw a small upward arrow hint at the top-right of the message area
        const std::string scroll_hint = "^ +" + std::to_string(scroll_offset_);
        renderer.draw_text(scroll_hint,
                           bounds.x + bounds.width - 60,
                           msg_area_y,
                           Color{180, 180, 180, 200});
    }

    // -- Input line -----------------------------------------------------------
    const int input_y = bounds.y + bounds.height - kInputHeight - kPadding;
    const Rect input_rect{bounds.x + kPadding,
                          input_y,
                          msg_area_width,
                          kInputHeight};
    renderer.draw_panel(input_rect, kInputBackground, kInputBorder);

    // Channel label prefix
    const std::string prefix = std::string("[") + channel_label(active_channel_) + "] ";

    if (input_active_) {
        // Show channel prefix and user text with blinking cursor
        std::string display = prefix + input_text_;
        if (cursor_visible_) {
            display += "_";
        }
        renderer.draw_text(display,
                           input_rect.x + 2,
                           input_rect.y + 2,
                           channel_color(active_channel_));
    } else {
        // Show hint text when not focused
        renderer.draw_text("Press Enter to chat...",
                           input_rect.x + 2,
                           input_rect.y + 2,
                           Color{120, 120, 120, 180});
    }
}

// =============================================================================
// Event handling
// =============================================================================

bool ChatPanel::handle_event(const SDL_Event& event) {
    if (!is_visible() || !is_enabled()) {
        return false;
    }
    return dispatch_event(event);
}

bool ChatPanel::on_key_down(SDL_Scancode /*scancode*/, SDL_Keycode keycode, bool /*repeat*/) {
    // Enter toggles input mode / sends message
    if (keycode == SDLK_RETURN || keycode == SDLK_KP_ENTER) {
        if (!input_active_) {
            // Activate input
            input_active_ = true;
            cursor_visible_ = true;
            cursor_blink_timer_ = 0.0f;
            return true;
        }
        // Send the message if there is text
        if (!input_text_.empty()) {
            if (on_send_message) {
                on_send_message(active_channel_, input_text_);
            }
            input_text_.clear();
        }
        // Deactivate input after sending
        input_active_ = false;
        return true;
    }

    if (!input_active_) {
        return false;
    }

    // Escape cancels input
    if (keycode == SDLK_ESCAPE) {
        input_text_.clear();
        input_active_ = false;
        return true;
    }

    // Tab cycles active channel
    if (keycode == SDLK_TAB) {
        cycle_channel();
        return true;
    }

    // Backspace removes last character
    if (keycode == SDLK_BACKSPACE) {
        if (!input_text_.empty()) {
            // Handle UTF-8: remove last byte (simplified single-byte removal).
            // For full UTF-8 support, pop back until a leading byte is found.
            input_text_.pop_back();
        }
        return true;
    }

    return false;
}

bool ChatPanel::on_key_up(SDL_Scancode /*scancode*/, SDL_Keycode /*keycode*/) {
    // We do not consume key-up events.
    return false;
}

bool ChatPanel::on_text_input(const char* text) {
    if (!input_active_ || text == nullptr) {
        return false;
    }
    input_text_ += text;
    // Reset cursor blink so user sees the cursor while typing
    cursor_visible_ = true;
    cursor_blink_timer_ = 0.0f;
    return true;
}

bool ChatPanel::on_mouse_wheel(int /*x*/, int y) {
    if (!is_visible()) {
        return false;
    }

    // y > 0 means scroll up (older messages), y < 0 means scroll down (newer).
    scroll_offset_ += y;
    clamp_scroll();
    return true;
}

bool ChatPanel::on_mouse_button_down(int /*button*/, int mx, int my) {
    // If user clicks inside the panel, activate input
    if (contains_point(mx, my)) {
        if (!input_active_) {
            input_active_ = true;
            cursor_visible_ = true;
            cursor_blink_timer_ = 0.0f;
        }
        return true;
    }
    // Click outside deactivates
    if (input_active_) {
        input_active_ = false;
    }
    return false;
}

// =============================================================================
// Helpers
// =============================================================================

Color ChatPanel::channel_color(ChatChannel ch) {
    switch (ch) {
        case ChatChannel::kWorld:   return kColorWorld;
        case ChatChannel::kPrivate: return kColorPrivate;
        case ChatChannel::kGuild:   return kColorGuild;
        case ChatChannel::kSystem:  return kColorSystem;
        case ChatChannel::kTeam:    return kColorTeam;
        case ChatChannel::kArea:    return kColorArea;
    }
    return kColorWorld;
}

const char* ChatPanel::channel_label(ChatChannel ch) {
    switch (ch) {
        case ChatChannel::kWorld:   return "World";
        case ChatChannel::kPrivate: return "Private";
        case ChatChannel::kGuild:   return "Guild";
        case ChatChannel::kSystem:  return "System";
        case ChatChannel::kTeam:    return "Team";
        case ChatChannel::kArea:    return "Area";
    }
    return "World";
}

std::string ChatPanel::format_message(const ChatMessageData& msg) {
    std::ostringstream oss;
    oss << "[" << channel_label(msg.channel) << "] ";
    if (msg.channel == ChatChannel::kSystem) {
        oss << msg.content;
    } else {
        if (!msg.from_name.empty()) {
            oss << msg.from_name << ": ";
        }
        oss << msg.content;
    }
    return oss.str();
}

void ChatPanel::cycle_channel() {
    // Cycle order: World -> Private -> Guild -> Team -> Area -> World
    // Skip System (players cannot send to System channel).
    static constexpr ChatChannel kCycleOrder[] = {
        ChatChannel::kWorld,
        ChatChannel::kPrivate,
        ChatChannel::kGuild,
        ChatChannel::kTeam,
        ChatChannel::kArea,
    };
    static constexpr int kCycleCount = static_cast<int>(
        sizeof(kCycleOrder) / sizeof(kCycleOrder[0]));

    for (int i = 0; i < kCycleCount; ++i) {
        if (kCycleOrder[i] == active_channel_) {
            active_channel_ = kCycleOrder[(i + 1) % kCycleCount];
            return;
        }
    }
    // Fallback (e.g. if active_channel_ was kSystem for some reason)
    active_channel_ = ChatChannel::kWorld;
}

void ChatPanel::clamp_scroll() {
    const int total = filtered_message_count();
    const int msg_area_height = get_bounds().height - kInputHeight - kPadding * 3;
    const int max_visible_lines = msg_area_height / kMessageLineHeight;
    const int max_scroll = std::max(0, total - max_visible_lines);
    scroll_offset_ = std::clamp(scroll_offset_, 0, max_scroll);
}

int ChatPanel::filtered_message_count() const {
    if (!filter_channel_.has_value()) {
        return static_cast<int>(messages_.size());
    }
    int count = 0;
    for (const auto& m : messages_) {
        if (m.channel == filter_channel_.value()) {
            ++count;
        }
    }
    return count;
}

} // namespace mir2::ui::chat
