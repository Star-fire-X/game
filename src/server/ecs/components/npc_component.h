/**
 * @file npc_component.h
 * @brief ECS NPC component definitions
 */

#ifndef LEGEND2_SERVER_ECS_NPC_COMPONENT_H
#define LEGEND2_SERVER_ECS_NPC_COMPONENT_H

#include "common/types.h"
#include "game/npc/npc_state_machine.h"

#include <entt/entt.hpp>

#include <any>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace mir2::ecs {

/**
 * @brief NPC state component
 */
struct NpcStateComponent {
    game::npc::NpcState current_state = game::npc::NpcState::Idle;
    game::npc::NpcState previous_state = game::npc::NpcState::Idle;
    float state_timer = 0.0f;                 ///< Time spent in current state
    entt::entity interacting_player = entt::null;  ///< Interacting player
};

/**
 * @brief NPC AI configuration component
 */
struct NpcAIComponent {
    bool enable_patrol = false;                     ///< Enable patrol behavior
    std::vector<mir2::common::Position> patrol_points;   ///< Patrol points
    uint32_t current_patrol_index = 0;              ///< Current patrol index
    float patrol_speed = 1.0f;                      ///< Patrol speed (tiles/sec)
    float idle_duration = 2.0f;                     ///< Idle duration before patrol
    mir2::common::Position spawn_point = {0, 0};         ///< Spawn point
};

/**
 * @brief NPC script component
 */
struct NpcScriptComponent {
    std::string script_id;  ///< Lua script ID
    std::unordered_map<std::string, std::any> script_data;  ///< Script custom data
};

}  // namespace mir2::ecs

#endif  // LEGEND2_SERVER_ECS_NPC_COMPONENT_H
