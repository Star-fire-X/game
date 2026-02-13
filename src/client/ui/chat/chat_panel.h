// =============================================================================
// Legend2 Chat UI - Chat Panel
//
// Features:
//   - Message history with channel filtering
//   - Scrollable message display with color-coded channels
//   - Text input with blinking cursor
//   - Channel cycling via Tab key
//   - Send callback for outgoing messages
// =============================================================================

#ifndef LEGEND2_UI_CHAT_CHAT_PANEL_H
#define LEGEND2_UI_CHAT_CHAT_PANEL_H

#include "client/handlers/chat_handler.h"
#include "ui/ui_manager.h"
#include "ui/ui_renderer.h"

#include <deque>
#include <functional>
#include <optional>
#include <string>

namespace mir2::ui::chat {

// Re-export client-side chat types for convenience
using mir2::game::handlers::ChatChannel;
using mir2::game::handlers::ChatMessageData;

/// @brief Chat panel widget for in-game messaging.
///
/// Renders a semi-transparent panel at the bottom-left of the screen showing
/// recent chat messages with channel color coding.  Handles text input,
/// channel switching, and scroll through message history.
///
/// Ownership: the caller is responsible for feeding incoming messages via
/// add_message() and wiring the on_send_message callback to the network layer.
class ChatPanel : public UIWidget {
public:
    /// Maximum number of messages stored in history.
    static constexpr size_t kMaxMessages = 200;

    /// Height of each message line in pixels.
    static constexpr int kMessageLineHeight = 16;

    /// Callback invoked when the user submits a chat message.
    /// Parameters: active channel and message text.
    std::function<void(ChatChannel channel, const std::string& text)> on_send_message;

    ChatPanel();
    ~ChatPanel() override = default;

    // -- Message API ----------------------------------------------------------

    /// @brief Append an incoming chat message to the history.
    void add_message(const ChatMessageData& msg);

    /// @brief Convenience: add a system-channel message with the given text.
    void add_system_message(const std::string& text);

    // -- Channel filtering ----------------------------------------------------

    /// @brief Set the active send-channel (also used when user presses Enter).
    void set_active_channel(ChatChannel channel);

    /// @brief Get the current active send-channel.
    ChatChannel get_active_channel() const { return active_channel_; }

    /// @brief Set a display filter.  If set, only messages matching the filter
    ///        channel are shown.  Pass std::nullopt to show all channels.
    void set_filter_channel(std::optional<ChatChannel> channel);

    /// @brief Get the current display filter channel (nullopt = show all).
    std::optional<ChatChannel> get_filter_channel() const { return filter_channel_; }

    // -- Widget overrides -----------------------------------------------------

    void update(float delta_time) override;
    void render(UIRenderer& renderer) override;
    bool handle_event(const SDL_Event& event) override;

protected:
    // IEventListener virtual overrides dispatched by UIWidget::dispatch_event
    bool on_key_down(SDL_Scancode scancode, SDL_Keycode keycode, bool repeat) override;
    bool on_key_up(SDL_Scancode scancode, SDL_Keycode keycode) override;
    bool on_text_input(const char* text) override;
    bool on_mouse_wheel(int x, int y) override;
    bool on_mouse_button_down(int button, int x, int y) override;

private:
    /// Return the display color for a given chat channel.
    static mir2::common::Color channel_color(ChatChannel ch);

    /// Return the short display label for a channel (e.g. "World").
    static const char* channel_label(ChatChannel ch);

    /// Format a single message for display.
    static std::string format_message(const ChatMessageData& msg);

    /// Cycle the active channel to the next one (used by Tab key).
    void cycle_channel();

    /// Clamp scroll_offset_ to valid range.
    void clamp_scroll();

    /// Count messages that pass the current display filter.
    int filtered_message_count() const;

    // -- State ----------------------------------------------------------------

    std::deque<ChatMessageData> messages_;

    ChatChannel active_channel_ = ChatChannel::kWorld;
    std::optional<ChatChannel> filter_channel_;  // nullopt = show all

    int scroll_offset_ = 0;  // 0 = newest messages visible at bottom

    bool input_active_ = false;
    std::string input_text_;

    float cursor_blink_timer_ = 0.0f;
    bool cursor_visible_ = true;

    // Panel layout constants (set in constructor from bounds_)
    static constexpr int kPanelWidth = 400;
    static constexpr int kPanelHeight = 220;
    static constexpr int kInputHeight = 20;
    static constexpr int kPadding = 4;
};

} // namespace mir2::ui::chat

#endif // LEGEND2_UI_CHAT_CHAT_PANEL_H
