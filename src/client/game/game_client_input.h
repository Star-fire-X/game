#ifndef LEGEND2_GAME_CLIENT_INPUT_H
#define LEGEND2_GAME_CLIENT_INPUT_H

#include "common/types.h"

#include <cstdint>
#include <functional>
#include <memory>

union SDL_Event;

namespace legend2 {
class PositionInterpolator;
} // namespace legend2

namespace mir2::render {
class Camera;
struct Actor;
} // namespace mir2::render

namespace mir2::ui {
class NpcDialogUI;
} // namespace mir2::ui

namespace mir2::ui::skill {
class SkillBook;
} // namespace mir2::ui::skill

namespace mir2::client::skill {
class SkillExecutor;
} // namespace mir2::client::skill

namespace mir2::game::map {
class MapRenderer;
} // namespace mir2::game::map

namespace mir2::game {

struct InputState;
class MovementController;
class ClientCharacter;
class EntityManager;

// =============================================================================
// 输入子模块 (Game Client Input)
// =============================================================================

/// 游戏客户端输入子模块
/// 负责处理SDL事件、键盘/鼠标输入和游戏内交互
class GameClientInput {
public:
    struct Dependencies {
        InputState* input = nullptr;
        std::unique_ptr<ClientCharacter>* player = nullptr;
        mir2::render::Actor* player_actor = nullptr;
        mir2::render::Actor** focused_actor = nullptr;
        map::MapRenderer* map_renderer = nullptr;
        mir2::ui::NpcDialogUI* npc_dialog_ui = nullptr;
        EntityManager* entity_manager = nullptr;
        std::unique_ptr<MovementController>* movement_controller = nullptr;
        legend2::PositionInterpolator* player_interpolator = nullptr;
        mir2::render::Camera* camera = nullptr;
        std::unique_ptr<mir2::client::skill::SkillExecutor>* skill_executor = nullptr;
        std::unique_ptr<mir2::ui::skill::SkillBook>* skill_book = nullptr;
    };

    struct Callbacks {
        std::function<void()> request_shutdown;
        std::function<void(int width, int height)> on_window_resized;
        std::function<void(const SDL_Event&)> dispatch_event;
        std::function<void()> handle_escape;
        std::function<void()> process_state_input;
        std::function<mir2::common::Position(int screen_x, int screen_y)> screen_to_world;
        std::function<void(uint64_t npc_id)> interact_with_npc;
        std::function<void(uint64_t item_entity_id)> interact_with_ground_item;
        std::function<bool()> is_connected;
        std::function<void(const mir2::common::Position& target)> send_move_request;
        std::function<mir2::render::Actor*(int32_t actor_id)> find_actor;
        std::function<void(mir2::render::Actor* actor)> set_focused_actor;
    };

    GameClientInput(Dependencies deps, Callbacks callbacks);

    void ProcessEvents();
    void UpdateInputState(const SDL_Event& event);
    void ProcessInput();

    void ProcessInputLogin();
    void ProcessInputCharacterSelect();
    void ProcessInputCharacterCreate();
    void ProcessInputPlaying();

private:
    void ProcessSkillHotkeys();

    Dependencies deps_;
    Callbacks callbacks_;
};

} // namespace mir2::game

#endif // LEGEND2_GAME_CLIENT_INPUT_H
