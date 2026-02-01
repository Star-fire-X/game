// =============================================================================
// Legend2 NPC Dialog UI
//
// Features:
//   - Show NPC dialog text and menu options
//   - Handle mouse clicks and keyboard close (ESC)
//   - UTF-8 text wrapping for multilingual content
// =============================================================================

#ifndef LEGEND2_UI_NPC_DIALOG_UI_H
#define LEGEND2_UI_NPC_DIALOG_UI_H

#include "client/handlers/npc_handler.h"
#include "core/event_dispatcher.h"
#include "render/renderer.h"
#include "ui/ui_renderer.h"
#include "common/types.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace mir2::ui {

using mir2::common::Color;
using mir2::common::Rect;
using mir2::core::EventDispatcher;
using mir2::core::IEventListener;
using mir2::game::handlers::NpcDialogEvent;
using mir2::game::handlers::NpcInteractEvent;
using mir2::game::handlers::NpcMenuSelectEvent;
using mir2::game::handlers::NpcUiEventType;
using mir2::render::SDLRenderer;
using mir2::render::Texture;

/// @brief NPC dialog UI component (modal overlay).
class NpcDialogUI : public IEventListener {
public:
    /// Callback for menu selection.
    using MenuSelectCallback = std::function<void(uint64_t npc_id, uint8_t option_index)>;

    /// @brief Construct NPC dialog UI.
    /// @param renderer SDL renderer reference.
    /// @param ui_renderer UI renderer reference.
    /// @param dispatcher Optional event dispatcher for sending selection events.
    NpcDialogUI(SDLRenderer& renderer, UIRenderer& ui_renderer, EventDispatcher* dispatcher = nullptr);

    /// @brief Set event dispatcher for outgoing events.
    void set_event_dispatcher(EventDispatcher* dispatcher);

    /// @brief Set menu selection callback.
    void set_menu_select_callback(MenuSelectCallback callback);

    /// @brief Update screen dimensions.
    void set_dimensions(int width, int height);

    /// @brief Show dialog with NPC name, text and menu options.
    void show_dialog(uint64_t npc_id,
                     const std::string& npc_name,
                     const std::string& text,
                     const std::vector<std::string>& options);

    /// @brief Update dialog text.
    void update_dialog_text(const std::string& text);

    /// @brief Update menu options.
    void update_menu_options(const std::vector<std::string>& options);

    /// @brief Update NPC display name.
    void update_npc_name(const std::string& npc_name);

    /// @brief Set portrait texture (optional).
    void set_portrait(std::shared_ptr<Texture> portrait);

    /// @brief Hide dialog.
    void hide_dialog();

    /// @brief Check visibility.
    bool is_visible() const { return visible_; }

    /// @brief Render dialog.
    void render();

    // IEventListener overrides
    bool on_mouse_move(int x, int y) override;
    bool on_mouse_button_down(int button, int x, int y) override;
    bool on_mouse_button_up(int button, int x, int y) override;
    bool on_key_down(SDL_Scancode key, SDL_Keycode keycode, bool repeat) override;
    bool on_user_event(const SDL_UserEvent& event) override;

private:
    void refresh_layout();
    void rebuild_text_lines();
    void rebuild_option_bounds();
    void handle_dialog_event(const NpcDialogEvent& event);
    void handle_interact_event(const NpcInteractEvent& event);
    void send_menu_select(uint8_t option_index);
    int option_at(int x, int y) const;
    bool point_in_rect(const Rect& rect, int x, int y) const;

    SDLRenderer& renderer_;
    UIRenderer& ui_renderer_;
    EventDispatcher* event_dispatcher_ = nullptr;
    MenuSelectCallback menu_select_callback_;

    int width_ = 800;
    int height_ = 600;

    bool visible_ = false;
    uint64_t npc_id_ = 0;
    std::string npc_name_;
    std::string dialog_text_;
    std::vector<std::string> menu_options_;

    std::shared_ptr<Texture> portrait_texture_;

    Rect screen_bounds_{0, 0, 0, 0};
    Rect dialog_bounds_{0, 0, 0, 0};
    Rect portrait_bounds_{0, 0, 0, 0};
    Rect name_bounds_{0, 0, 0, 0};
    Rect text_bounds_{0, 0, 0, 0};
    Rect close_bounds_{0, 0, 0, 0};

    std::vector<Rect> option_bounds_;
    std::vector<std::string> wrapped_lines_;

    int hovered_option_ = -1;
    int pressed_option_ = -1;
    bool close_hovered_ = false;
    bool close_pressed_ = false;

    int button_height_ = 0;
    int button_spacing_ = 0;
    int text_line_height_ = 0;
    int text_line_spacing_ = 0;

    bool layout_dirty_ = true;
    bool text_dirty_ = true;
};

} // namespace mir2::ui

#endif // LEGEND2_UI_NPC_DIALOG_UI_H
