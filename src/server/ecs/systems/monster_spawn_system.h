/**
 * @file monster_spawn_system.h
 * @brief 怪物刷新系统
 */

#ifndef MIR2_ECS_SYSTEMS_MONSTER_SPAWN_SYSTEM_H
#define MIR2_ECS_SYSTEMS_MONSTER_SPAWN_SYSTEM_H

#include <entt/entt.hpp>
#include <unordered_map>
#include <string>
#include <cstdint>

#include "game/entity/monster_spawn_config.h"

namespace mir2::ecs {

class EventBus;

/**
 * @brief 复活计时器
 */
struct RespawnTimer {
    uint64_t monster_id = 0;
    uint32_t spawn_point_id = 0;
    float remaining_time = 0.0f;
};

/**
 * @brief 怪物刷新系统
 */
class MonsterSpawnSystem {
public:
    MonsterSpawnSystem();
    explicit MonsterSpawnSystem(entt::registry& registry, EventBus& event_bus);
    ~MonsterSpawnSystem();

    void Update(entt::registry& registry, float dt);
    void LoadSpawnConfig(const std::string& config_path);
    void TriggerDynamicSpawn(const game::entity::DynamicSpawnEvent& event);
    void OnMonsterDeath(uint32_t spawn_point_id);

private:
    entt::registry* registry_ = nullptr;
    EventBus* event_bus_ = nullptr;
    std::unordered_map<uint32_t, game::entity::MonsterSpawnPoint> spawn_points_;
    std::unordered_map<uint64_t, RespawnTimer> respawn_timers_;
    float elapsed_time_ = 0.0f;

    void CheckAndSpawn(entt::registry& registry, float dt);
    void SpawnMonsterAtPoint(entt::registry& registry, 
                            game::entity::MonsterSpawnPoint& spawn);
    void ScheduleRespawn(uint64_t monster_id, uint32_t spawn_id, float delay);
    void ProcessRespawnTimers(entt::registry& registry, float dt);
    void SubscribeToDeathEvents();
};

}  // namespace mir2::ecs

#endif  // MIR2_ECS_SYSTEMS_MONSTER_SPAWN_SYSTEM_H
