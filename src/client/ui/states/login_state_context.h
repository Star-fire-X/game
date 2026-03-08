#ifndef LEGEND2_LOGIN_STATE_CONTEXT_H
#define LEGEND2_LOGIN_STATE_CONTEXT_H

#include <SDL.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "common/character_data.h"
#include "common/types.h"
#include "ui/states/login_screen_state.h"

namespace mir2::render {
class SDLRenderer;
class Texture;
}  // namespace mir2::render

namespace mir2::ui {
class UIRenderer;
class UILayoutLoader;
}  // namespace mir2::ui

namespace mir2::client {
class ResourceManager;
}  // namespace mir2::client

namespace mir2::ui::screens {

using mir2::common::Rect;
using mir2::common::CharacterClass;
using mir2::common::Gender;

struct TextInputField;
struct Button;
struct CharacterSlot;

struct AnimationFrame {
    std::shared_ptr<mir2::render::Texture> texture;
    int offset_x = 0;
    int offset_y = 0;
};

enum class LoginStateEnterReason { Transition, LayoutRefresh };

class IRenderProvider {
public:
    virtual ~IRenderProvider() = default;
    virtual mir2::render::SDLRenderer& get_renderer() const = 0;
    virtual UIRenderer& get_ui_renderer() const = 0;
};

class ILayoutProvider {
public:
    virtual ~ILayoutProvider() = default;
    virtual UILayoutLoader& get_layout_loader() const = 0;
    virtual int get_screen_width() const = 0;
    virtual int get_screen_height() const = 0;
    virtual LoginStateEnterReason get_enter_reason() const = 0;
};

class IStatusProvider {
public:
    virtual ~IStatusProvider() = default;
    virtual const std::string& get_status_text() const = 0;
    virtual const std::string& get_error_message() const = 0;
    virtual void set_error(const std::string& msg) = 0;
    virtual void clear_error() = 0;
};

class ILoginInputProvider {
public:
    virtual ~ILoginInputProvider() = default;
    virtual TextInputField& get_username_field() = 0;
    virtual TextInputField& get_password_field() = 0;
    virtual Rect& get_login_confirm_bounds() = 0;
    virtual Button& get_offline_button() = 0;
};

class ICharacterSelectProvider {
public:
    virtual ~ICharacterSelectProvider() = default;
    virtual std::vector<CharacterSlot>& get_character_slots() = 0;
    virtual int get_selected_character_index() const = 0;
    virtual void set_selected_character_index(int index) = 0;
};

class ICharacterCreateProvider {
public:
    virtual ~ICharacterCreateProvider() = default;
    virtual TextInputField& get_create_name_field() = 0;
    virtual CharacterClass get_create_class() const = 0;
    virtual void set_create_class(CharacterClass c) = 0;
    virtual Gender get_create_gender() const = 0;
    virtual void set_create_gender(Gender g) = 0;
    virtual Button& get_confirm_create_button() = 0;
    virtual Button& get_cancel_create_button() = 0;

    virtual int get_selected_class_index() const = 0;
    virtual void set_selected_class_index(int idx) = 0;
    virtual int get_selected_gender_index() const = 0;
    virtual void set_selected_gender_index(int idx) = 0;
    virtual bool is_class_panel_visible() const = 0;
    virtual void set_class_panel_visible(bool visible) = 0;
    virtual float get_class_panel_visibility() const = 0;
    virtual void set_class_panel_visibility(float v) = 0;

    virtual Rect& get_create_button_bounds() = 0;
    virtual Rect& get_class_panel_bounds() = 0;
    virtual Rect& get_class_select_bounds(int index) = 0;
    virtual Rect& get_preview_area_bounds() = 0;
    virtual Rect& get_start_game_bounds() = 0;
    virtual Rect& get_created_name_bounds() = 0;
    virtual Rect& get_created_level_bounds() = 0;
    virtual Rect& get_created_class_bounds() = 0;

    virtual const std::string& get_created_character_name() const = 0;
    virtual CharacterClass get_created_character_class() const = 0;
    virtual Gender get_created_character_gender() const = 0;
    virtual int get_created_character_level() const = 0;
    virtual bool has_created_character() const = 0;
};

class IAnimationProvider {
public:
    virtual ~IAnimationProvider() = default;
    virtual bool is_cursor_visible() const = 0;
    virtual bool is_login_animation_playing() const = 0;
    virtual void set_login_animation_playing(bool playing) = 0;
    virtual void start_login_animation(const std::string& username, const std::string& password) = 0;
    virtual int get_login_animation_frame() const = 0;
    virtual void set_login_animation_frame(int frame) = 0;
    virtual float get_login_animation_timer() const = 0;
    virtual void set_login_animation_timer(float timer) = 0;
    virtual float get_login_animation_frame_time() const = 0;
    virtual const std::string& get_pending_username() const = 0;
    virtual const std::string& get_pending_password() const = 0;
    virtual void clear_pending_credentials() = 0;
    virtual int get_preview_animation_frame() const = 0;
    virtual void set_preview_animation_frame(int frame) = 0;
    virtual float get_preview_animation_timer() const = 0;
    virtual void set_preview_animation_timer(float timer) = 0;
    virtual float get_preview_animation_frame_time() const = 0;
};

class IResourceProvider {
public:
    virtual ~IResourceProvider() = default;
    virtual mir2::client::ResourceManager& get_resource_manager() = 0;
    virtual std::shared_ptr<mir2::render::Texture> get_background_texture() const = 0;
    virtual std::shared_ptr<mir2::render::Texture> get_create_background_texture() const = 0;
    virtual std::shared_ptr<mir2::render::Texture> get_class_panel_texture() const = 0;
    virtual const std::vector<std::shared_ptr<mir2::render::Texture>>& get_class_select_textures() const = 0;
    virtual const std::vector<std::shared_ptr<mir2::render::Texture>>& get_class_highlight_textures() const = 0;
    virtual const std::vector<AnimationFrame>& get_login_animation_frames() const = 0;
    virtual std::vector<AnimationFrame>& get_character_preview_frames(int index) = 0;
    virtual const std::vector<AnimationFrame>& get_character_preview_frames(int index) const = 0;
};

class ICallbackProvider {
public:
    virtual ~ICallbackProvider() = default;
    virtual void transition_to(LoginScreenState state) = 0;
    virtual void invoke_login(const std::string& username, const std::string& password) = 0;
    virtual void invoke_character_select(uint32_t character_id) = 0;
    virtual void invoke_character_create(const std::string& name, CharacterClass c, Gender g) = 0;
    virtual void invoke_start_game() = 0;
    virtual void invoke_offline_play() = 0;
};

// Shared rendering capability used across multiple states.
class IFieldRenderContext : public virtual IRenderProvider,
                            public virtual IAnimationProvider {
public:
    virtual ~IFieldRenderContext() = default;
};

class IBackgroundRenderContext : public virtual IRenderProvider,
                                 public virtual ILayoutProvider,
                                 public virtual IResourceProvider {
public:
    virtual ~IBackgroundRenderContext() = default;
};

class ILoginAnimationRenderContext : public virtual IBackgroundRenderContext,
                                     public virtual IAnimationProvider {
public:
    virtual ~ILoginAnimationRenderContext() = default;
};

class ILoginInputStateContext : public virtual ILoginAnimationRenderContext,
                                public virtual IFieldRenderContext,
                                public virtual IStatusProvider,
                                public virtual ILoginInputProvider,
                                public virtual ICallbackProvider {
public:
    virtual ~ILoginInputStateContext() = default;
};

class ICharacterSelectStateContext : public virtual IBackgroundRenderContext,
                                     public virtual ICharacterSelectProvider,
                                     public virtual ICallbackProvider {
public:
    virtual ~ICharacterSelectStateContext() = default;
};

class ICharacterCreateStateContext : public virtual IBackgroundRenderContext,
                                     public virtual IFieldRenderContext,
                                     public virtual IStatusProvider,
                                     public virtual ICharacterCreateProvider,
                                     public virtual ICallbackProvider {
public:
    virtual ~ICharacterCreateStateContext() = default;
};

class IConnectingStateContext : public virtual IBackgroundRenderContext,
                                public virtual IStatusProvider,
                                public virtual ICallbackProvider {
public:
    virtual ~IConnectingStateContext() = default;
};

class IErrorStateContext : public virtual IBackgroundRenderContext,
                           public virtual IStatusProvider,
                           public virtual ICallbackProvider {
public:
    virtual ~IErrorStateContext() = default;
};

class ILoginStateContext : public virtual ILoginInputStateContext,
                           public virtual ICharacterSelectStateContext,
                           public virtual ICharacterCreateStateContext,
                           public virtual IConnectingStateContext,
                           public virtual IErrorStateContext {
public:
    virtual ~ILoginStateContext() = default;
};

}  // namespace mir2::ui::screens

#endif  // LEGEND2_LOGIN_STATE_CONTEXT_H
