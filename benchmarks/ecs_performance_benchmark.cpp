/**
 * @file ecs_performance_benchmark.cpp
 * @brief ECS性能基准测试 - 1000实体完整tick
 */

#include <benchmark/benchmark.h>

#include <cstdint>
#include <random>
#include <string>
#include <vector>

#include "common/types/constants.h"
#include "ecs/components/character_components.h"
#include "ecs/components/item_component.h"
#include "ecs/systems/combat_system.h"
#include "ecs/systems/inventory_system.h"
#include "ecs/systems/level_up_system.h"
#include "ecs/systems/movement_system.h"
#include "ecs/world.h"

namespace {

constexpr int kEntityCount = 1000;
constexpr float kDeltaTime = 0.016f;  // 16ms (60 FPS)
constexpr int kHotPathAttackCount = 100;
constexpr int kInventorySlots = mir2::common::constants::MAX_INVENTORY_SIZE;
constexpr uint32_t kInventoryTrackedItemId = 1001;
constexpr uint32_t kInventoryOtherItemId = 1002;

/**
 * @brief 创建测试用角色实体
 */
entt::entity CreateTestCharacter(entt::registry& registry, uint32_t id, int x, int y) {
    auto entity = registry.create();

    // 身份组件
    auto& identity = registry.emplace<mir2::ecs::CharacterIdentityComponent>(entity);
    identity.id = id;
    identity.name = "Player" + std::to_string(id);
    identity.char_class = mir2::common::CharacterClass::WARRIOR;
    identity.gender = mir2::common::Gender::MALE;

    // 属性组件
    auto& attrs = registry.emplace<mir2::ecs::CharacterAttributesComponent>(entity);
    attrs.level = 1 + (id % 10);  // 等级1-10
    attrs.experience = 0;
    attrs.max_hp = 100 + attrs.level * 20;
    attrs.hp = attrs.max_hp;
    attrs.max_mp = 50 + attrs.level * 10;
    attrs.mp = attrs.max_mp;
    attrs.attack = 10 + attrs.level * 3;
    attrs.defense = 5 + attrs.level * 2;
    attrs.magic_attack = 8 + attrs.level * 2;
    attrs.magic_defense = 4 + attrs.level * 1;
    attrs.speed = 100;
    attrs.gold = 1000;

    // 状态组件
    auto& state = registry.emplace<mir2::ecs::CharacterStateComponent>(entity);
    state.map_id = 1;
    state.position = {x, y};
    state.direction = mir2::common::Direction::DOWN;

    return entity;
}

/**
 * @brief 初始化1000个测试实体
 */
void SetupEntities(entt::registry& registry, std::vector<entt::entity>& entities) {
    entities.clear();
    entities.reserve(kEntityCount);

    // 在100x100的网格中分布实体
    for (int i = 0; i < kEntityCount; ++i) {
        int x = (i % 100) * 10;
        int y = (i / 100) * 10;
        entities.push_back(CreateTestCharacter(registry, i, x, y));
    }
}

void ClearInventoryItemsForCharacter(entt::registry& registry,
                                     entt::entity character) {
    std::vector<entt::entity> items_to_destroy;
    auto view = registry.view<mir2::ecs::InventoryOwnerComponent,
                              mir2::ecs::ItemComponent>();
    items_to_destroy.reserve(kInventorySlots);

    for (auto entity : view) {
        const auto& owner = view.get<mir2::ecs::InventoryOwnerComponent>(entity);
        if (owner.owner == character) {
            items_to_destroy.push_back(entity);
        }
    }

    for (auto entity : items_to_destroy) {
        registry.destroy(entity);
    }
}

}  // namespace

/**
 * @brief 基准测试：1000实体完整tick（移动+战斗+升级系统）
 */
static void BM_ECS_FullTick_1000Entities(benchmark::State& state) {
    mir2::ecs::World world;
    std::vector<entt::entity> entities;

    // 创建系统
    world.CreateSystem<mir2::ecs::MovementSystem>();
    world.CreateSystem<mir2::ecs::CombatSystem>();
    world.CreateSystem<mir2::ecs::LevelUpSystem>();

    // 初始化实体
    SetupEntities(world.Registry(), entities);

    // 性能测试
    for (auto _ : state) {
        world.Update(kDeltaTime);
        benchmark::DoNotOptimize(entities);
    }

    // 统计信息
    state.SetItemsProcessed(state.iterations() * kEntityCount);
    state.counters["entities"] = kEntityCount;
    state.counters["systems"] = 3;
}

/**
 * @brief 基准测试：仅移动系统
 */
static void BM_ECS_MovementOnly_1000Entities(benchmark::State& state) {
    mir2::ecs::World world;
    std::vector<entt::entity> entities;

    world.CreateSystem<mir2::ecs::MovementSystem>();

    SetupEntities(world.Registry(), entities);

    for (auto _ : state) {
        world.Update(kDeltaTime);
        benchmark::DoNotOptimize(entities);
    }

    state.SetItemsProcessed(state.iterations() * kEntityCount);
    state.counters["entities"] = kEntityCount;
}

/**
 * @brief 基准测试：仅战斗系统
 */
static void BM_ECS_CombatOnly_1000Entities(benchmark::State& state) {
    mir2::ecs::World world;
    std::vector<entt::entity> entities;

    world.CreateSystem<mir2::ecs::CombatSystem>();

    SetupEntities(world.Registry(), entities);

    for (auto _ : state) {
        world.Update(kDeltaTime);
        benchmark::DoNotOptimize(entities);
    }

    state.SetItemsProcessed(state.iterations() * kEntityCount);
    state.counters["entities"] = kEntityCount;
}

/**
 * @brief 基准测试：仅升级系统
 */
static void BM_ECS_LevelUpOnly_1000Entities(benchmark::State& state) {
    mir2::ecs::World world;
    std::vector<entt::entity> entities;

    world.CreateSystem<mir2::ecs::LevelUpSystem>();

    SetupEntities(world.Registry(), entities);

    for (auto _ : state) {
        world.Update(kDeltaTime);
        benchmark::DoNotOptimize(entities);
    }

    state.SetItemsProcessed(state.iterations() * kEntityCount);
    state.counters["entities"] = kEntityCount;
}

/**
 * @brief 基准测试：组件访问性能
 */
static void BM_ECS_ComponentAccess_1000Entities(benchmark::State& state) {
    entt::registry registry;
    std::vector<entt::entity> entities;

    SetupEntities(registry, entities);

    int sum = 0;
    for (auto _ : state) {
        // 遍历所有实体，访问属性组件
        auto view = registry.view<mir2::ecs::CharacterAttributesComponent>();
        for (auto entity : view) {
            auto& attrs = view.get<mir2::ecs::CharacterAttributesComponent>(entity);
            sum += attrs.hp;
        }
        benchmark::DoNotOptimize(sum);
    }

    state.SetItemsProcessed(state.iterations() * kEntityCount);
    state.counters["entities"] = kEntityCount;
}

/**
 * @brief 基准测试：战斗伤害计算（100次攻击）
 */
static void BM_ECS_CombatDamage_100Attacks(benchmark::State& state) {
    entt::registry registry;
    std::vector<entt::entity> entities;

    SetupEntities(registry, entities);

    legend2::CombatConfig config;
    std::mt19937 rng(42);
    std::uniform_int_distribution<size_t> dist(0, entities.size() - 1);

    for (auto _ : state) {
        // 执行100次随机攻击
        for (int i = 0; i < kHotPathAttackCount; ++i) {
            size_t attacker_idx = dist(rng);
            size_t target_idx = dist(rng);
            if (attacker_idx != target_idx) {
                auto result = mir2::ecs::CombatSystem::ExecuteAttack(
                    registry, entities[attacker_idx], entities[target_idx], config);
                benchmark::DoNotOptimize(result);
            }
        }
    }

    state.SetItemsProcessed(state.iterations() * kHotPathAttackCount);
    state.counters["attacks_per_iter"] = kHotPathAttackCount;
}

/**
 * @brief 基准测试：实体创建和销毁
 */
static void BM_ECS_EntityCreationDestruction(benchmark::State& state) {
    entt::registry registry;

    for (auto _ : state) {
        std::vector<entt::entity> temp_entities;
        temp_entities.reserve(100);

        // 创建100个实体
        for (int i = 0; i < 100; ++i) {
            temp_entities.push_back(CreateTestCharacter(registry, i, i * 10, i * 10));
        }

        // 销毁所有实体
        for (auto entity : temp_entities) {
            registry.destroy(entity);
        }

        benchmark::DoNotOptimize(temp_entities);
    }

    state.SetItemsProcessed(state.iterations() * 100);
}

static void BM_MigrationHotPath_Tick_1000Entities(benchmark::State& state) {
    BM_ECS_FullTick_1000Entities(state);
}

static void BM_MigrationHotPath_Combat_100Attacks(benchmark::State& state) {
    BM_ECS_CombatDamage_100Attacks(state);
}

static void BM_MigrationHotPath_Inventory_40Slots(benchmark::State& state) {
    entt::registry registry;
    const entt::entity character = CreateTestCharacter(registry, 1, 10, 10);

    for (auto _ : state) {
        state.PauseTiming();
        ClearInventoryItemsForCharacter(registry, character);
        state.ResumeTiming();

        int added = 0;
        for (int slot = 0; slot < kInventorySlots; ++slot) {
            const uint32_t item_id = (slot % 2 == 0) ? kInventoryTrackedItemId
                                                      : kInventoryOtherItemId;
            const auto item = mir2::ecs::InventorySystem::AddItem(
                registry, character, item_id, 1);
            if (item.has_value()) {
                ++added;
            }
        }

        int tracked_count = mir2::ecs::InventorySystem::CountItem(
            registry, character, kInventoryTrackedItemId);
        bool has_required_count = mir2::ecs::InventorySystem::HasItem(
            registry, character, kInventoryTrackedItemId, kInventorySlots / 2);

        benchmark::DoNotOptimize(added);
        benchmark::DoNotOptimize(tracked_count);
        benchmark::DoNotOptimize(has_required_count);
    }

    state.SetItemsProcessed(state.iterations() * kInventorySlots);
    state.counters["slots"] = kInventorySlots;
}

// 注册基准测试
BENCHMARK(BM_ECS_FullTick_1000Entities)
    ->Unit(benchmark::kMillisecond)
    ->Iterations(1000);

BENCHMARK(BM_ECS_MovementOnly_1000Entities)
    ->Unit(benchmark::kMicrosecond)
    ->Iterations(10000);

BENCHMARK(BM_ECS_CombatOnly_1000Entities)
    ->Unit(benchmark::kMicrosecond)
    ->Iterations(10000);

BENCHMARK(BM_ECS_LevelUpOnly_1000Entities)
    ->Unit(benchmark::kMicrosecond)
    ->Iterations(10000);

BENCHMARK(BM_ECS_ComponentAccess_1000Entities)
    ->Unit(benchmark::kMicrosecond)
    ->Iterations(10000);

BENCHMARK(BM_ECS_CombatDamage_100Attacks)
    ->Unit(benchmark::kMicrosecond)
    ->Iterations(10000);

BENCHMARK(BM_ECS_EntityCreationDestruction)
    ->Unit(benchmark::kMicrosecond)
    ->Iterations(10000);

BENCHMARK(BM_MigrationHotPath_Tick_1000Entities)
    ->Unit(benchmark::kMillisecond)
    ->Iterations(1000);

BENCHMARK(BM_MigrationHotPath_Combat_100Attacks)
    ->Unit(benchmark::kMicrosecond)
    ->Iterations(10000);

BENCHMARK(BM_MigrationHotPath_Inventory_40Slots)
    ->Unit(benchmark::kMicrosecond)
    ->Iterations(10000);

BENCHMARK_MAIN();
