#include <gtest/gtest.h>

#include <entt/entt.hpp>

#include "common/character_data.h"
#include "ecs/character_entity_manager.h"
#include "ecs/components/character_components.h"
#include "ecs/systems/combat_system.h"

#include <atomic>
#include <thread>
#include <vector>

namespace {

mir2::common::CharacterData MakeCharacterData(uint32_t id) {
    mir2::common::CharacterData data;
    data.id = id;
    data.account_id = "acc";
    data.name = "Tester";
    data.char_class = mir2::common::CharacterClass::MAGE;
    data.gender = mir2::common::Gender::FEMALE;
    data.stats.level = 4;
    data.stats.experience = 250;
    data.stats.hp = 70;
    data.stats.max_hp = 120;
    data.stats.mp = 40;
    data.stats.max_mp = 80;
    data.stats.attack = 12;
    data.stats.defense = 6;
    data.stats.magic_attack = 20;
    data.stats.magic_defense = 9;
    data.stats.speed = 5;
    data.stats.gold = 1234;
    data.map_id = 3;
    data.position = {8, 9};
    data.equipment_json = R"({"weapon":"staff"})";
    data.inventory_json = R"(["potion"])";
    data.skills_json = R"(["blink"])";
    data.created_at = 1700000000;
    data.last_login = 1700000100;
    return data;
}

}  // namespace

TEST(CharacterEntityManagerTest, PreloadCreatesEntityFromData) {
    entt::registry registry;
    mir2::ecs::CharacterEntityManager manager(registry);

    auto data = MakeCharacterData(42);
    auto entity = manager.Preload(data);

    ASSERT_TRUE(registry.valid(entity));
    const auto& identity = registry.get<mir2::ecs::CharacterIdentityComponent>(entity);
    const auto& attributes = registry.get<mir2::ecs::CharacterAttributesComponent>(entity);
    const auto& state = registry.get<mir2::ecs::CharacterStateComponent>(entity);
    const auto& inventory = registry.get<mir2::ecs::InventoryComponent>(entity);

    EXPECT_EQ(identity.id, data.id);
    EXPECT_EQ(identity.account_id, data.account_id);
    EXPECT_EQ(identity.name, data.name);
    EXPECT_EQ(identity.char_class, data.char_class);
    EXPECT_EQ(identity.gender, data.gender);

    EXPECT_EQ(attributes.level, data.stats.level);
    EXPECT_EQ(attributes.experience, data.stats.experience);
    EXPECT_EQ(attributes.hp, data.stats.hp);
    EXPECT_EQ(attributes.max_hp, data.stats.max_hp);
    EXPECT_EQ(attributes.mp, data.stats.mp);
    EXPECT_EQ(attributes.max_mp, data.stats.max_mp);
    EXPECT_EQ(attributes.attack, data.stats.attack);
    EXPECT_EQ(attributes.defense, data.stats.defense);
    EXPECT_EQ(attributes.magic_attack, data.stats.magic_attack);
    EXPECT_EQ(attributes.magic_defense, data.stats.magic_defense);
    EXPECT_EQ(attributes.speed, data.stats.speed);
    EXPECT_EQ(attributes.gold, data.stats.gold);

    EXPECT_EQ(state.map_id, data.map_id);
    EXPECT_EQ(state.position, data.position);

    EXPECT_EQ(inventory.equipment_json, data.equipment_json);
    EXPECT_EQ(inventory.inventory_json, data.inventory_json);
    EXPECT_EQ(inventory.skills_json, data.skills_json);
}

TEST(CharacterEntityManagerTest, GetOrCreateRepairsIndex) {
    entt::registry registry;
    mir2::ecs::CharacterEntityManager manager(registry);

    auto entity = registry.create();
    auto& identity = registry.emplace<mir2::ecs::CharacterIdentityComponent>(entity);
    identity.id = 7;
    identity.name = "Orphan";

    auto result = manager.GetOrCreate(7);
    EXPECT_EQ(result, entity);
    EXPECT_TRUE(manager.TryGet(7).has_value());
}

TEST(CharacterEntityManagerTest, SaveAndTimeoutCleanup) {
    entt::registry registry;
    mir2::ecs::CharacterEntityManager manager(registry);
    manager.SetSaveIntervalSeconds(0.1f);
    manager.SetTimeoutSeconds(0.2f);

    auto entity = manager.GetOrCreate(5);
    ASSERT_TRUE(registry.valid(entity));

    auto& attributes = registry.get<mir2::ecs::CharacterAttributesComponent>(entity);
    attributes.hp = 42;

    manager.Update(0.11f);
    auto stored = manager.GetStoredData(5);
    ASSERT_TRUE(stored.has_value());
    EXPECT_EQ(stored->stats.hp, 42);

    manager.OnDisconnect(5);
    manager.Update(0.21f);
    EXPECT_FALSE(manager.TryGet(5).has_value());
}

TEST(CharacterEntityManagerTest, CombatSystemIntegratesWithManager) {
    entt::registry registry;
    mir2::ecs::CharacterEntityManager manager(registry);

    auto attacker = manager.GetOrCreate(1);
    auto target = manager.GetOrCreate(2);
    ASSERT_TRUE(registry.valid(attacker));
    ASSERT_TRUE(registry.valid(target));

    auto& attacker_attr = registry.get<mir2::ecs::CharacterAttributesComponent>(attacker);
    auto& target_attr = registry.get<mir2::ecs::CharacterAttributesComponent>(target);
    attacker_attr.attack = 20;
    target_attr.defense = 0;
    target_attr.max_hp = 50;
    target_attr.hp = 50;

    auto& attacker_state = registry.get<mir2::ecs::CharacterStateComponent>(attacker);
    auto& target_state = registry.get<mir2::ecs::CharacterStateComponent>(target);
    attacker_state.position = {5, 5};
    target_state.position = {5, 5};

    legend2::CombatConfig config;
    config.default_melee_range = 1;
    config.base_miss_chance = 0.0f;
    config.min_variance_percent = 0;
    config.max_variance_percent = 0;

    auto result = mir2::ecs::CombatSystem::ExecuteAttack(registry, attacker, target, config);
    EXPECT_TRUE(result.success);
    EXPECT_LT(target_attr.hp, 50);
}

// ============================================================================
// 并发测试
// ============================================================================

// DISABLED: ECS系统采用单线程设计,不支持并发访问
// 详见 src/server/ecs/THREADING.md
TEST(CharacterEntityManagerTest, DISABLED_ConcurrentGetOrCreate) {
    entt::registry registry;
    mir2::ecs::CharacterEntityManager manager(registry);

    constexpr int kThreadCount = 10;
    constexpr int kOperationsPerThread = 100;
    std::atomic<int> success_count{0};

    std::vector<std::thread> threads;
    for (int i = 0; i < kThreadCount; ++i) {
        threads.emplace_back([&, i]() {
            for (int j = 0; j < kOperationsPerThread; ++j) {
                uint32_t character_id = i * kOperationsPerThread + j;
                auto entity = manager.GetOrCreate(character_id);
                if (entity != entt::null) {
                    success_count++;
                }
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    EXPECT_EQ(success_count.load(), kThreadCount * kOperationsPerThread);
    EXPECT_EQ(manager.GetIndexSize(), kThreadCount * kOperationsPerThread);
}

// DISABLED: ECS系统采用单线程设计,不支持并发访问
// 详见 src/server/ecs/THREADING.md
TEST(CharacterEntityManagerTest, DISABLED_ConcurrentGetOrCreateSameId) {
    entt::registry registry;
    mir2::ecs::CharacterEntityManager manager(registry);

    constexpr int kThreadCount = 20;
    constexpr uint32_t kCharacterId = 42;
    std::atomic<int> success_count{0};
    std::vector<entt::entity> entities(kThreadCount);

    std::vector<std::thread> threads;
    for (int i = 0; i < kThreadCount; ++i) {
        threads.emplace_back([&, i]() {
            auto entity = manager.GetOrCreate(kCharacterId);
            entities[i] = entity;
            if (entity != entt::null) {
                success_count++;
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    // 所有线程应该获得相同的 entity
    EXPECT_EQ(success_count.load(), kThreadCount);
    for (int i = 1; i < kThreadCount; ++i) {
        EXPECT_EQ(entities[i], entities[0]);
    }
    EXPECT_EQ(manager.GetIndexSize(), 1);
}

// DISABLED: ECS系统采用单线程设计,不支持并发访问
// 详见 src/server/ecs/THREADING.md
TEST(CharacterEntityManagerTest, DISABLED_ConcurrentTryGet) {
    entt::registry registry;
    mir2::ecs::CharacterEntityManager manager(registry);

    // 预先创建一些实体
    constexpr int kEntityCount = 100;
    for (uint32_t i = 0; i < kEntityCount; ++i) {
        manager.GetOrCreate(i);
    }

    constexpr int kThreadCount = 10;
    std::atomic<int> success_count{0};

    std::vector<std::thread> threads;
    for (int i = 0; i < kThreadCount; ++i) {
        threads.emplace_back([&]() {
            for (uint32_t j = 0; j < kEntityCount; ++j) {
                auto entity = manager.TryGet(j);
                if (entity.has_value()) {
                    success_count++;
                }
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    EXPECT_EQ(success_count.load(), kThreadCount * kEntityCount);
}

// DISABLED: ECS系统采用单线程设计,不支持并发访问
// 详见 src/server/ecs/THREADING.md
TEST(CharacterEntityManagerTest, DISABLED_ConcurrentMixedOperations) {
    entt::registry registry;
    mir2::ecs::CharacterEntityManager manager(registry);

    constexpr int kThreadCount = 8;
    constexpr int kOperationsPerThread = 50;
    std::atomic<int> create_count{0};
    std::atomic<int> read_count{0};

    std::vector<std::thread> threads;
    for (int i = 0; i < kThreadCount; ++i) {
        threads.emplace_back([&, i]() {
            for (int j = 0; j < kOperationsPerThread; ++j) {
                uint32_t character_id = (i * kOperationsPerThread + j) % 200;

                // 50% 创建，50% 读取
                if (j % 2 == 0) {
                    auto entity = manager.GetOrCreate(character_id);
                    if (entity != entt::null) {
                        create_count++;
                    }
                } else {
                    auto entity = manager.TryGet(character_id);
                    if (entity.has_value()) {
                        read_count++;
                    }
                }
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    EXPECT_GT(create_count.load(), 0);
    EXPECT_GT(read_count.load(), 0);
    EXPECT_LE(manager.GetIndexSize(), 200);
}

// DISABLED: ECS系统采用单线程设计,不支持并发访问
// 详见 src/server/ecs/THREADING.md
TEST(CharacterEntityManagerTest, DISABLED_ConcurrentStressTest) {
    entt::registry registry;
    mir2::ecs::CharacterEntityManager manager(registry);

    constexpr int kThreadCount = 16;
    constexpr int kIterations = 1000;
    std::atomic<int> total_operations{0};

    std::vector<std::thread> threads;
    for (int i = 0; i < kThreadCount; ++i) {
        threads.emplace_back([&, i]() {
            for (int j = 0; j < kIterations; ++j) {
                uint32_t id = (i * 1000 + j) % 500;

                if (j % 3 == 0) {
                    manager.GetOrCreate(id);
                } else {
                    manager.TryGet(id);
                }
                total_operations++;
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    EXPECT_EQ(total_operations.load(), kThreadCount * kIterations);
    EXPECT_LE(manager.GetIndexSize(), 500);
}
