/**
 * @file monster_data.h
 * @brief Client-side monster data definitions.
 */

#ifndef LEGEND2_CLIENT_GAME_MONSTER_MONSTER_DATA_H
#define LEGEND2_CLIENT_GAME_MONSTER_MONSTER_DATA_H

#include "client/core/position_interpolator.h"
#include "client/game/actor_data.h"
#include "common/types.h"

#include <cstdint>
#include <string>

namespace mir2::game::monster {

/**
 * @brief Monster animation/state types used on the client.
 */
enum class ClientMonsterState : uint8_t {
    Idle = 0,
    Walk,
    Attack,
    Struck,
    Death,
    Dead
};

/**
 * @brief Render configuration for a monster.
 */
struct MonsterRenderConfig {
    mir2::game::ActorRace race = mir2::game::ActorRace::HUMAN;
    int appearance = 0;
    bool show_hp_bar = true;
    bool show_name = true;
};

/**
 * @brief Runtime animation state for a monster.
 */
struct MonsterAnimState {
    ClientMonsterState state = ClientMonsterState::Idle;
    int current_frame = 0;
    int start_frame = 0;
    int end_frame = 0;
    int frame_time = 150;          ///< Per-frame duration (ms).
    uint64_t last_frame_tick = 0;  ///< Last frame update time (ms).
    bool loop = true;
    bool finished = false;
};

/**
 * @brief Client-side monster data container.
 */
struct ClientMonster {
    uint64_t id = 0;
    uint32_t template_id = 0;
    std::string name;
    mir2::common::Position position{};
    uint8_t direction = 0;
    legend2::EntityInterpolator interpolator;
    int hp = 0;
    int max_hp = 0;
    uint16_t level = 1;
    ClientMonsterState state = ClientMonsterState::Idle;
    bool is_boss = false;
    MonsterRenderConfig render_config{};
    MonsterAnimState anim_state{};
    uint64_t death_time = 0;

    /// Whether the monster is dead (death animation or corpse state).
    bool is_dead() const {
        return state == ClientMonsterState::Death ||
               state == ClientMonsterState::Dead ||
               hp <= 0;
    }

    /// HP percentage in [0, 1].
    float hp_percent() const {
        if (max_hp <= 0) {
            return 0.0f;
        }
        const float ratio = static_cast<float>(hp) / static_cast<float>(max_hp);
        return ratio < 0.0f ? 0.0f : (ratio > 1.0f ? 1.0f : ratio);
    }
};

} // namespace mir2::game::monster

#endif // LEGEND2_CLIENT_GAME_MONSTER_MONSTER_DATA_H
