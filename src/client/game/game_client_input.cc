#include "game/game_client_input.h"

#include "game/game_client.h"
#include "client/game/client_character.h"
#include "client/game/entity_manager.h"
#include "client/game/movement_controller.h"
#include "client/core/position_interpolator.h"
#include "game/map/map_renderer.h"
#include "render/actor_renderer.h"
#include "render/camera.h"
#include "ui/npc_dialog_ui.h"
#include "client/game/skill/skill_executor.h"
#include "client/ui/skill/skill_book.h"

#include <SDL.h>
#include <utility>

namespace mir2::game {

GameClientInput::GameClientInput(Dependencies deps, Callbacks callbacks)
    : deps_(std::move(deps)),
      callbacks_(std::move(callbacks)) {
}

void GameClientInput::ProcessEvents() {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_QUIT) {
            if (callbacks_.request_shutdown) {
                callbacks_.request_shutdown();
            }
            continue;
        }

        if (event.type == SDL_WINDOWEVENT) {
            if (event.window.event == SDL_WINDOWEVENT_RESIZED) {
                if (callbacks_.on_window_resized) {
                    callbacks_.on_window_resized(event.window.data1, event.window.data2);
                }
            }
            continue;
        }

        if (callbacks_.dispatch_event) {
            callbacks_.dispatch_event(event);
        }

        // 全局快捷键处理
        if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE) {
            if (callbacks_.handle_escape) {
                callbacks_.handle_escape();
            }
        }

        UpdateInputState(event);
    }
}

void GameClientInput::UpdateInputState(const SDL_Event& event) {
    if (!deps_.input) {
        return;
    }

    switch (event.type) {
        case SDL_KEYDOWN:
            if (event.key.keysym.scancode < SDL_NUM_SCANCODES) {
                // 记录按键按下状态（仅首次按下时触发 pressed）
                if (!deps_.input->keys[event.key.keysym.scancode]) {
                    deps_.input->keys_pressed[event.key.keysym.scancode] = true;
                }
                deps_.input->keys[event.key.keysym.scancode] = true;
            }
            break;
        case SDL_KEYUP:
            if (event.key.keysym.scancode < SDL_NUM_SCANCODES) {
                deps_.input->keys[event.key.keysym.scancode] = false;
            }
            break;
        case SDL_MOUSEMOTION:
            deps_.input->mouse_x = event.motion.x;
            deps_.input->mouse_y = event.motion.y;
            break;
        case SDL_MOUSEBUTTONDOWN:
            if (event.button.button == SDL_BUTTON_LEFT) {
                deps_.input->mouse_left = true;
                deps_.input->mouse_left_clicked = true;  // 单帧点击标记
            } else if (event.button.button == SDL_BUTTON_RIGHT) {
                deps_.input->mouse_right = true;
                deps_.input->mouse_right_clicked = true;
            } else if (event.button.button == SDL_BUTTON_MIDDLE) {
                deps_.input->mouse_middle = true;
            }
            break;
        case SDL_MOUSEBUTTONUP:
            if (event.button.button == SDL_BUTTON_LEFT) {
                deps_.input->mouse_left = false;
            } else if (event.button.button == SDL_BUTTON_RIGHT) {
                deps_.input->mouse_right = false;
            } else if (event.button.button == SDL_BUTTON_MIDDLE) {
                deps_.input->mouse_middle = false;
            }
            break;
        default:
            break;
    }
}

void GameClientInput::ProcessInput() {
    if (callbacks_.process_state_input) {
        callbacks_.process_state_input();
    }
}

void GameClientInput::ProcessInputLogin() {
    // Input is handled by LoginScreen via event forwarding
}

void GameClientInput::ProcessInputCharacterSelect() {
    // Input is handled by LoginScreen via event forwarding
}

void GameClientInput::ProcessInputCharacterCreate() {
    // Input is handled by LoginScreen via event forwarding
}

void GameClientInput::ProcessInputPlaying() {
    if (!deps_.input) {
        return;
    }

    auto* player = deps_.player ? deps_.player->get() : nullptr;
    if (!player) {
        return;
    }

    // 调试快捷键
    if (deps_.input->key_pressed(SDL_SCANCODE_G) && deps_.map_renderer) {
        // G键切换网格显示
        deps_.map_renderer->set_debug_grid(!deps_.map_renderer->is_debug_grid_enabled());
    }
    if (deps_.input->key_pressed(SDL_SCANCODE_W) && deps_.map_renderer) {
        // W键切换可行走区域显示
        deps_.map_renderer->set_debug_walkability(!deps_.map_renderer->is_debug_walkability_enabled());
    }

    ProcessSkillHotkeys();

    // NPC对话打开时屏蔽场景点击
    if (deps_.npc_dialog_ui && deps_.npc_dialog_ui->is_visible()) {
        return;
    }

    // 鼠标左键点击移动
    if (deps_.input->mouse_left_clicked) {
        Position target{-1, -1};
        if (callbacks_.screen_to_world) {
            target = callbacks_.screen_to_world(deps_.input->mouse_x, deps_.input->mouse_y);
        }

        if (target.x >= 0 && target.y >= 0) {
            const Entity* target_entity = nullptr;
            if (deps_.entity_manager) {
                const auto entities_at_target = deps_.entity_manager->query_at(target);
                for (const auto* entity : entities_at_target) {
                    if (!entity) {
                        continue;
                    }
                    if (entity->id == player->get_id()) {
                        continue;
                    }
                    if (entity->type == EntityType::NPC) {
                        target_entity = entity;
                        break;
                    }
                    if (!target_entity) {
                        target_entity = entity;
                    }
                }
            }

            if (target_entity) {
                mir2::render::Actor* actor = nullptr;
                if (callbacks_.find_actor) {
                    actor = callbacks_.find_actor(static_cast<int32_t>(target_entity->id));
                }

                if (callbacks_.set_focused_actor) {
                    callbacks_.set_focused_actor(actor);
                } else if (deps_.focused_actor) {
                    *deps_.focused_actor = actor;
                }

                if (target_entity->type == EntityType::NPC) {
                    if (callbacks_.interact_with_npc) {
                        callbacks_.interact_with_npc(target_entity->id);
                    }
                    return;
                }
            } else if (callbacks_.set_focused_actor) {
                callbacks_.set_focused_actor(nullptr);
            } else if (deps_.focused_actor) {
                *deps_.focused_actor = nullptr;
            }

            deps_.input->target_position = target;
            deps_.input->has_move_target = true;

            const bool connected = callbacks_.is_connected ? callbacks_.is_connected() : false;
            if (!connected) {
                // 离线模式直接移动
                player->set_position(target);
                if (deps_.entity_manager) {
                    const uint8_t direction = deps_.player_actor ? deps_.player_actor->data.direction : 0;
                    deps_.entity_manager->update_entity_position(player->get_id(), target, direction);
                }
                if (deps_.player_interpolator) {
                    deps_.player_interpolator->set_immediate(target);
                }
                if (deps_.camera) {
                    deps_.camera->center_on(target);
                }
            } else {
                bool accepted = false;
                auto* movement_controller = deps_.movement_controller ? deps_.movement_controller->get() : nullptr;
                if (movement_controller) {
                    accepted = movement_controller->request_move(player->get_position(), target);
                } else if (callbacks_.send_move_request) {
                    callbacks_.send_move_request(target);
                    accepted = true;
                }
                if (!accepted) {
                    deps_.input->has_move_target = false;
                }
            }
        }
    }
}

void GameClientInput::ProcessSkillHotkeys() {
    if (!deps_.input) {
        return;
    }

    auto* player = deps_.player ? deps_.player->get() : nullptr;
    auto* skill_executor = deps_.skill_executor ? deps_.skill_executor->get() : nullptr;
    if (!player || !skill_executor) {
        return;
    }

    static constexpr SDL_Scancode kHotkeys[] = {
        SDL_SCANCODE_F1, SDL_SCANCODE_F2, SDL_SCANCODE_F3, SDL_SCANCODE_F4,
        SDL_SCANCODE_F5, SDL_SCANCODE_F6, SDL_SCANCODE_F7, SDL_SCANCODE_F8
    };

    const int current_mp = player->get_data().stats.mp;
    uint64_t target_id = 0;
    if (deps_.focused_actor && *deps_.focused_actor) {
        target_id = static_cast<uint64_t>((*deps_.focused_actor)->data.id);
    } else {
        target_id = static_cast<uint64_t>(player->get_id());
    }

    const size_t hotkey_count = sizeof(kHotkeys) / sizeof(kHotkeys[0]);
    for (size_t i = 0; i < hotkey_count; ++i) {
        if (!deps_.input->key_pressed(kHotkeys[i])) {
            continue;
        }
        const uint8_t slot = static_cast<uint8_t>(i + 1);
        skill_executor->try_use_skill_by_hotkey(slot, current_mp, target_id);
    }

    auto* skill_book = deps_.skill_book ? deps_.skill_book->get() : nullptr;
    if (deps_.input->key_pressed(SDL_SCANCODE_K) && skill_book) {
        skill_book->toggle();
    }
}

} // namespace mir2::game
