/**
 * @file monster_spawn_system_test.cc
 * @brief 怪物刷新系统单元测试
 */

#include <gtest/gtest.h>
#include <entt/entt.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

#include "ecs/components/monster_component.h"
#include "ecs/components/transform_component.h"
#include "game/entity/monster_spawn_config.h"

#define private public
#include "ecs/systems/monster_spawn_system.h"
#undef private

namespace mir2::ecs {
namespace {

class MonsterSpawnSystemTest : public ::testing::Test {
protected:
    void SetUp() override {
        registry_.clear();
        auto base = std::filesystem::temp_directory_path();
        auto unique = std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count());
        temp_dir_ = base / ("mir2_spawn_test_" + unique);
        std::filesystem::create_directories(temp_dir_);
        file_counter_ = 0;
    }

    void TearDown() override {
        registry_.clear();
        std::error_code ec;
        if (!temp_dir_.empty()) {
            std::filesystem::remove_all(temp_dir_, ec);
        }
    }

    std::filesystem::path WriteConfig(const std::string& contents) {
        auto path = temp_dir_ / ("spawn_" + std::to_string(file_counter_++) + ".yaml");
        std::ofstream out(path);
        out << contents;
        return path;
    }

    entt::registry registry_;
    std::filesystem::path temp_dir_;
    int file_counter_ = 0;
};

template <typename Transform>
int32_t GetTransformX(const Transform& transform) {
    if constexpr (requires { transform.position.x; }) {
        return transform.position.x;
    } else {
        return transform.x;
    }
}

template <typename Transform>
int32_t GetTransformY(const Transform& transform) {
    if constexpr (requires { transform.position.y; }) {
        return transform.position.y;
    } else {
        return transform.y;
    }
}

TEST_F(MonsterSpawnSystemTest, SpawnPointConfig) {
    game::entity::MonsterSpawnPoint spawn;
    spawn.spawn_id = 1;
    spawn.max_count = 5;
    spawn.respawn_interval = 30.0f;
    
    EXPECT_EQ(spawn.spawn_id, 1);
    EXPECT_EQ(spawn.max_count, 5);
}

TEST_F(MonsterSpawnSystemTest, SpawnSystem_LoadConfig) {
    MonsterSpawnSystem system;
    const auto path = WriteConfig(R"(spawn_points:
  - spawn_id: 1
    map_id: 7
    center: { x: 10, y: 20 }
    spawn_radius: 3
    monster_template_id: 99
    patrol_radius: 6
    respawn_interval: 12
    max_count: 2
    aggro_range: 9
    attack_range: 4
  - spawn_id: 0
    map_id: 1
)");

    system.LoadSpawnConfig(path.string());

    ASSERT_EQ(system.spawn_points_.size(), 1u);
    const auto& spawn = system.spawn_points_.at(1);
    EXPECT_EQ(spawn.map_id, 7u);
    EXPECT_EQ(spawn.center_x, 10);
    EXPECT_EQ(spawn.center_y, 20);
    EXPECT_EQ(spawn.spawn_radius, 3);
    EXPECT_EQ(spawn.monster_template_id, 99u);
    EXPECT_EQ(spawn.patrol_radius, 6);
    EXPECT_FLOAT_EQ(spawn.respawn_interval, 12.0f);
    EXPECT_EQ(spawn.max_count, 2);
    EXPECT_EQ(spawn.current_count, 0);
    EXPECT_EQ(spawn.aggro_range, 9);
    EXPECT_EQ(spawn.attack_range, 4);
}

TEST_F(MonsterSpawnSystemTest, SpawnSystem_SpawnAtPoint) {
    MonsterSpawnSystem system;
    game::entity::MonsterSpawnPoint spawn;
    spawn.spawn_id = 5;
    spawn.map_id = 2;
    spawn.center_x = 100;
    spawn.center_y = 200;
    spawn.spawn_radius = 2;
    spawn.monster_template_id = 77;
    spawn.aggro_range = 13;
    spawn.attack_range = 4;
    spawn.max_count = 3;

    system.SpawnMonsterAtPoint(registry_, spawn);

    EXPECT_EQ(spawn.current_count, 1);

    auto view = registry_.view<MonsterIdentityComponent,
                               TransformComponent,
                               MonsterAIComponent,
                               MonsterAggroComponent>();
    entt::entity spawned = entt::null;
    size_t count = 0;
    for (auto entity : view) {
        spawned = entity;
        ++count;
    }
    ASSERT_EQ(count, 1u);

    const auto& identity = view.get<MonsterIdentityComponent>(spawned);
    EXPECT_EQ(identity.monster_template_id, 77u);
    EXPECT_EQ(identity.spawn_point_id, 5u);

    const auto& transform = view.get<TransformComponent>(spawned);
    EXPECT_EQ(transform.map_id, 2u);
    const int32_t x = GetTransformX(transform);
    const int32_t y = GetTransformY(transform);
    EXPECT_GE(x, spawn.center_x - spawn.spawn_radius);
    EXPECT_LE(x, spawn.center_x + spawn.spawn_radius);
    EXPECT_GE(y, spawn.center_y - spawn.spawn_radius);
    EXPECT_LE(y, spawn.center_y + spawn.spawn_radius);

    const auto& ai = view.get<MonsterAIComponent>(spawned);
    EXPECT_EQ(ai.return_position.x, spawn.center_x);
    EXPECT_EQ(ai.return_position.y, spawn.center_y);

    const auto& aggro = view.get<MonsterAggroComponent>(spawned);
    EXPECT_EQ(aggro.aggro_range, spawn.aggro_range);
    EXPECT_EQ(aggro.attack_range, spawn.attack_range);
}

TEST_F(MonsterSpawnSystemTest, SpawnSystem_RespawnTimer) {
    MonsterSpawnSystem system;
    game::entity::MonsterSpawnPoint spawn;
    spawn.spawn_id = 42;
    spawn.map_id = 3;
    spawn.center_x = 7;
    spawn.center_y = 9;
    spawn.spawn_radius = 0;
    spawn.monster_template_id = 123;
    spawn.aggro_range = 10;
    spawn.attack_range = 3;

    system.spawn_points_[spawn.spawn_id] = spawn;
    system.ScheduleRespawn(1001, spawn.spawn_id, 2.0f);

    ASSERT_EQ(system.respawn_timers_.size(), 1u);

    system.ProcessRespawnTimers(registry_, 1.0f);
    EXPECT_EQ(system.respawn_timers_.size(), 1u);
    EXPECT_EQ(system.spawn_points_.at(spawn.spawn_id).current_count, 0);

    system.ProcessRespawnTimers(registry_, 1.1f);
    EXPECT_EQ(system.respawn_timers_.size(), 0u);
    EXPECT_EQ(system.spawn_points_.at(spawn.spawn_id).current_count, 1);

    auto view = registry_.view<MonsterIdentityComponent, TransformComponent>();
    entt::entity spawned = entt::null;
    size_t count = 0;
    for (auto entity : view) {
        spawned = entity;
        ++count;
    }
    ASSERT_EQ(count, 1u);

    const auto& identity = view.get<MonsterIdentityComponent>(spawned);
    EXPECT_EQ(identity.spawn_point_id, spawn.spawn_id);

    const auto& transform = view.get<TransformComponent>(spawned);
    EXPECT_EQ(transform.map_id, spawn.map_id);
    EXPECT_EQ(GetTransformX(transform), spawn.center_x);
    EXPECT_EQ(GetTransformY(transform), spawn.center_y);
}

}  // namespace
}  // namespace mir2::ecs
