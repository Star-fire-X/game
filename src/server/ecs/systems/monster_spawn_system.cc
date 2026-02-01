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

#include <filesystem>
#include <iostream>
#include <random>

#include <yaml-cpp/yaml.h>

namespace mir2::ecs {

namespace {

template <typename T>
T ReadOrDefault(const YAML::Node& node, const char* key, const T& default_value) {
    if (node && node[key]) {
        return node[key].as<T>();
    }
    return default_value;
}

}  // namespace

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

void MonsterSpawnSystem::LoadSpawnConfig(const std::string& config_path) {
    // 解析YAML刷新点配置
    spawn_points_.clear();

    try {
        if (config_path.empty() || !std::filesystem::exists(config_path)) {
            return;
        }

        YAML::Node root = YAML::LoadFile(config_path);
        YAML::Node spawn_nodes = root["spawn_points"];
        if (!spawn_nodes) {
            spawn_nodes = root["spawns"];
        }
        if (!spawn_nodes) {
            spawn_nodes = root;
        }
        if (!spawn_nodes || !spawn_nodes.IsSequence()) {
            return;
        }

        for (const auto& node : spawn_nodes) {
            if (!node || !node.IsMap()) {
                continue;
            }

            game::entity::MonsterSpawnPoint spawn;
            spawn.spawn_id = ReadOrDefault(node, "spawn_id", spawn.spawn_id);
            if (spawn.spawn_id == 0 && node["id"]) {
                spawn.spawn_id = node["id"].as<uint32_t>();
            }
            if (spawn.spawn_id == 0) {
                continue;
            }

            spawn.map_id = ReadOrDefault(node, "map_id", spawn.map_id);
            const YAML::Node center = node["center"] ? node["center"] : node["position"];
            if (center) {
                spawn.center_x = ReadOrDefault(center, "x", spawn.center_x);
                spawn.center_y = ReadOrDefault(center, "y", spawn.center_y);
            } else {
                spawn.center_x = ReadOrDefault(node, "center_x", spawn.center_x);
                spawn.center_y = ReadOrDefault(node, "center_y", spawn.center_y);
            }

            spawn.spawn_radius = ReadOrDefault(node, "spawn_radius", spawn.spawn_radius);
            spawn.monster_template_id =
                ReadOrDefault(node, "monster_template_id", spawn.monster_template_id);
            if (spawn.monster_template_id == 0 && node["monster_id"]) {
                spawn.monster_template_id = node["monster_id"].as<uint32_t>();
            }
            spawn.patrol_radius = ReadOrDefault(node, "patrol_radius", spawn.patrol_radius);
            spawn.respawn_interval = ReadOrDefault(node, "respawn_interval", spawn.respawn_interval);
            spawn.max_count = ReadOrDefault(node, "max_count", spawn.max_count);
            spawn.aggro_range = ReadOrDefault(node, "aggro_range", spawn.aggro_range);
            spawn.attack_range = ReadOrDefault(node, "attack_range", spawn.attack_range);
            // 重置当前数量以避免配置热重载污染
            spawn.current_count = 0;
            spawn.last_spawn_time = elapsed_time_ - spawn.respawn_interval;

            spawn_points_[spawn.spawn_id] = spawn;
        }
    } catch (const std::exception& ex) {
        std::cerr << "Spawn config load failed: " << ex.what() << std::endl;
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
    transform.x = x;
    transform.y = y;
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

    event_bus_->Subscribe<events::EntityDeathEvent>(
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
