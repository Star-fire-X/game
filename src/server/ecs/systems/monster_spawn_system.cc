/**
 * @file monster_spawn_system.cc
 * @brief 怪物刷新系统实现
 */

#include "ecs/systems/monster_spawn_system.h"
#include "ecs/components/monster_component.h"
#include "ecs/components/transform_component.h"
#include "ecs/event_bus.h"
#include "ecs/events/combat_events.h"
#include "ecs/events/monster_events.h"

#include <random>

namespace mir2::ecs {

MonsterSpawnSystem::MonsterSpawnSystem() = default;

MonsterSpawnSystem::MonsterSpawnSystem(entt::registry& registry, EventBus& event_bus)
    : registry_(&registry),
      event_bus_(&event_bus) {
    SubscribeToDeathEvents();
}

MonsterSpawnSystem::~MonsterSpawnSystem() = default;

void MonsterSpawnSystem::Update(entt::registry& registry, float dt) {
    if (dt > 0.0f) {
        elapsed_time_ += dt;
    }
    CheckAndSpawn(registry, dt);
    ProcessRespawnTimers(registry, dt);
}

void MonsterSpawnSystem::ReplaceAllSpawnPoints(
    std::unordered_map<uint32_t, game::entity::MonsterSpawnPoint> spawn_points) {
    spawn_points_.clear();
    respawn_timers_.clear();

    for (auto& [spawn_id, spawn] : spawn_points) {
        spawn.current_count = 0;
        spawn.last_spawn_time = elapsed_time_ - spawn.respawn_interval;
        spawn_points_.emplace(spawn_id, std::move(spawn));
    }
}

void MonsterSpawnSystem::TriggerDynamicSpawn(
    const game::entity::DynamicSpawnEvent& event) {
    // TODO: 处理动态刷新事件
}

void MonsterSpawnSystem::CheckAndSpawn(entt::registry& registry, float dt) {
    for (auto& [id, spawn] : spawn_points_) {
        if (spawn.max_count <= 0) {
            continue;
        }
        if (spawn.current_count >= spawn.max_count) {
            continue;
        }
        if (spawn.respawn_interval > 0.0f &&
            (elapsed_time_ - spawn.last_spawn_time) < spawn.respawn_interval) {
            continue;
        }
        SpawnMonsterAtPoint(registry, spawn);
    }
}

void MonsterSpawnSystem::SpawnMonsterAtPoint(entt::registry& registry, 
                                             game::entity::MonsterSpawnPoint& spawn) {
    static thread_local std::mt19937 gen(std::random_device{}());
    
    // 随机位置偏移
    std::uniform_int_distribution<> dis(-spawn.spawn_radius, spawn.spawn_radius);
    int32_t x = spawn.center_x + dis(gen);
    int32_t y = spawn.center_y + dis(gen);
    
    // 创建怪物实体
    auto entity = registry.create();

    // 记录怪物模板与刷新点信息
    auto& identity = registry.emplace<MonsterIdentityComponent>(entity);
    identity.monster_template_id = spawn.monster_template_id;
    identity.spawn_point_id = spawn.spawn_id;
    
    // 添加Transform组件
    auto& transform = registry.emplace<TransformComponent>(entity);
    transform.position = {x, y};
    transform.map_id = spawn.map_id;
    
    // 添加AI组件
    auto& ai = registry.emplace<MonsterAIComponent>(entity);
    ai.return_position = {spawn.center_x, spawn.center_y};
    
    // 添加仇恨组件
    auto& aggro = registry.emplace<MonsterAggroComponent>(entity);
    aggro.aggro_range = spawn.aggro_range;
    aggro.attack_range = spawn.attack_range;
    
    spawn.current_count++;
    spawn.last_spawn_time = elapsed_time_;
}

void MonsterSpawnSystem::OnMonsterDeath(uint32_t spawn_point_id) {
    if (spawn_point_id == 0) {
        return;
    }

    auto it = spawn_points_.find(spawn_point_id);
    if (it == spawn_points_.end()) {
        return;
    }

    auto& spawn = it->second;
    if (spawn.current_count > 0) {
        spawn.current_count--;
    }
    spawn.last_spawn_time = elapsed_time_;
}

void MonsterSpawnSystem::SubscribeToDeathEvents() {
    if (!event_bus_) {
        return;
    }

    death_subscription_ = event_bus_->SubscribeScoped<events::EntityDeathEvent>(
        [this](events::EntityDeathEvent& event) {
            if (!registry_ || event.entity == entt::null) {
                return;
            }

            const auto* identity = registry_->try_get<MonsterIdentityComponent>(event.entity);
            if (!identity || identity->spawn_point_id == 0) {
                return;
            }

            OnMonsterDeath(identity->spawn_point_id);
        });
}

void MonsterSpawnSystem::ScheduleRespawn(uint64_t monster_id, uint32_t spawn_id, 
                                         float delay) {
    RespawnTimer timer;
    timer.monster_id = monster_id;
    timer.spawn_point_id = spawn_id;
    timer.remaining_time = delay;
    respawn_timers_[monster_id] = timer;
}

void MonsterSpawnSystem::ProcessRespawnTimers(entt::registry& registry, float dt) {
    std::vector<uint64_t> ready;
    
    for (auto& [id, timer] : respawn_timers_) {
        timer.remaining_time -= dt;
        if (timer.remaining_time <= 0.0f) {
            ready.push_back(id);
        }
    }
    
    for (auto id : ready) {
        auto& timer = respawn_timers_[id];
        auto it = spawn_points_.find(timer.spawn_point_id);
        if (it != spawn_points_.end()) {
            SpawnMonsterAtPoint(registry, it->second);
        }
        respawn_timers_.erase(id);
    }
}

}  // namespace mir2::ecs
