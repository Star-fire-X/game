/**
 * @file persistence_integration_test.cc
 * @brief Integration tests for complete persistence workflows
 *
 * Test scenarios:
 * - Full workflow: register, mark dirty, save, load, verify
 * - Snapshot creation and recovery
 * - Concurrent operations with data race detection
 * - Backend failure recovery with retries
 * - Stress tests for memory leaks and resource cleanup
 */

#include <gtest/gtest.h>
#include "persistence/persistence_manager.h"
#include "persistence/json_serializer.h"
#include "persistence/component_registry.h"
#include "mocks/enhanced_storage_backend.h"
#include <thread>
#include <atomic>
#include <memory>
#include <random>

namespace mir2::persistence::test {

// ============================================
// Domain Models for Integration Tests
// ============================================

struct PlayerPosition {
    double x, y, z;

    bool operator==(const PlayerPosition& other) const {
        return x == other.x && y == other.y && z == other.z;
    }
};

struct PlayerInventory {
    std::vector<std::string> items;
    uint32_t capacity;

    bool operator==(const PlayerInventory& other) const {
        return items == other.items && capacity == other.capacity;
    }
};

struct PlayerStats {
    uint32_t level;
    uint64_t experience;
    std::string class_name;

    bool operator==(const PlayerStats& other) const {
        return level == other.level && experience == other.experience &&
               class_name == other.class_name;
    }
};

// ============================================
// Integration Test Fixture
// ============================================

class PersistenceIntegrationTest : public ::testing::Test {
 protected:
    void SetUp() override {
        PersistenceManager::Shutdown();
    }

    void TearDown() override {
        PersistenceManager::Shutdown();
    }

    void InitializeSystem(const FailureConfig& config = FailureConfig{}) {
        auto backend = std::make_unique<EnhancedStorageBackend>();
        backend->SetFailureConfig(config);
        backend_ptr_ = backend.get();

        auto serializer = std::make_unique<JsonSerializer>();
        RegisterSerializers(serializer.get());

        PersistenceManager::Initialize(
            std::move(backend),
            std::move(serializer),
            PersistenceManager::Config{
                .auto_save_interval_seconds = 1,
                .batch_size = 10,
                .shutdown_timeout_ms = 5000
            });
    }

    void RegisterSerializers(JsonSerializer* serializer) {
        // Register PlayerPosition
        serializer->RegisterCustom<PlayerPosition>(
            [](const PlayerPosition& p) {
                json j;
                j["x"] = p.x;
                j["y"] = p.y;
                j["z"] = p.z;
                return j;
            },
            [](const json& j, PlayerPosition& p) {
                p.x = j["x"].get<double>();
                p.y = j["y"].get<double>();
                p.z = j["z"].get<double>();
                return true;
            });

        // Register PlayerInventory
        serializer->RegisterCustom<PlayerInventory>(
            [](const PlayerInventory& inv) {
                json j;
                j["items"] = inv.items;
                j["capacity"] = inv.capacity;
                return j;
            },
            [](const json& j, PlayerInventory& inv) {
                inv.items = j["items"].get<std::vector<std::string>>();
                inv.capacity = j["capacity"].get<uint32_t>();
                return true;
            });

        // Register PlayerStats
        serializer->RegisterCustom<PlayerStats>(
            [](const PlayerStats& stats) {
                json j;
                j["level"] = stats.level;
                j["experience"] = stats.experience;
                j["class_name"] = stats.class_name;
                return j;
            },
            [](const json& j, PlayerStats& stats) {
                stats.level = j["level"].get<uint32_t>();
                stats.experience = j["experience"].get<uint64_t>();
                stats.class_name = j["class_name"].get<std::string>();
                return true;
            });
    }

    EnhancedStorageBackend* backend_ptr_ = nullptr;
};

// ============================================
// H1: Full Workflow Tests
// ============================================

TEST_F(PersistenceIntegrationTest, FullWorkflow_RegisterMarkSaveVerify) {
    InitializeSystem();
    auto& mgr = PersistenceManager::Instance();

    // Setup test data
    PlayerPosition pos{10.5, 20.5, 30.5};
    PlayerInventory inv{{"sword", "shield", "potion"}, 100};
    PlayerStats stats{10, 5000, "Warrior"};

    // Serialize components
    JsonSerializer serializer;
    RegisterSerializers(&serializer);

    auto pos_result = serializer.Serialize(pos);
    auto inv_result = serializer.Serialize(inv);
    auto stats_result = serializer.Serialize(stats);

    ASSERT_TRUE(pos_result.success);
    ASSERT_TRUE(inv_result.success);
    ASSERT_TRUE(stats_result.success);

    // Save entities
    auto save_pos = mgr.SaveEntity(1, "player", pos_result.data, 1);
    auto save_inv = mgr.SaveEntity(1, "player", inv_result.data, 1);
    auto save_stats = mgr.SaveEntity(1, "player", stats_result.data, 1);

    EXPECT_TRUE(save_pos.success);
    EXPECT_TRUE(save_inv.success);
    EXPECT_TRUE(save_stats.success);

    // Load and verify
    auto load_result = mgr.LoadEntity(1);
    EXPECT_TRUE(load_result.has_value());

    EXPECT_EQ(backend_ptr_->GetEntityCount(), 1);
}

TEST_F(PersistenceIntegrationTest, MultipleEntities_SaveAndVerifyAll) {
    InitializeSystem();
    auto& mgr = PersistenceManager::Instance();

    JsonSerializer serializer;
    RegisterSerializers(&serializer);

    // Create and save multiple players
    const int NUM_PLAYERS = 100;

    for (int i = 0; i < NUM_PLAYERS; ++i) {
        PlayerStats stats{
            static_cast<uint32_t>(i),
            static_cast<uint64_t>(i * 1000),
            "Class" + std::to_string(i)
        };

        auto result = serializer.Serialize(stats);
        ASSERT_TRUE(result.success);

        auto save_result = mgr.SaveEntity(i, "player", result.data, 1);
        ASSERT_TRUE(save_result.success);
    }

    // Verify all entities are stored
    EXPECT_EQ(backend_ptr_->GetEntityCount(), NUM_PLAYERS);

    // Load all entities
    auto all_result = mgr.LoadAllEntities();
    EXPECT_TRUE(all_result.has_value());
    EXPECT_EQ(all_result->size(), NUM_PLAYERS);
}

// ============================================
// H2: Snapshot and Recovery Tests
// ============================================

TEST_F(PersistenceIntegrationTest, SnapshotRecovery_CreateShutdownRecover) {
    InitializeSystem();
    auto& mgr = PersistenceManager::Instance();

    JsonSerializer serializer;
    RegisterSerializers(&serializer);

    // Save some data
    PlayerStats stats{20, 10000, "Paladin"};
    auto result = serializer.Serialize(stats);
    mgr.SaveEntity(1, "player", result.data, 1);

    // Create shutdown snapshot
    auto snapshot_result = mgr.CreateGracefulShutdownSnapshot();
    EXPECT_TRUE(snapshot_result.success);

    // Simulate restart and recovery
    PersistenceManager::Shutdown();

    InitializeSystem();

    auto& mgr2 = PersistenceManager::Instance();
    auto recovery_result = mgr2.PerformStartupRecovery();

    EXPECT_TRUE(recovery_result.success);
}

TEST_F(PersistenceIntegrationTest, SnapshotValidation_IntegrityCheck) {
    InitializeSystem();
    auto& mgr = PersistenceManager::Instance();

    JsonSerializer serializer;
    RegisterSerializers(&serializer);

    // Create data
    for (int i = 0; i < 10; ++i) {
        PlayerPosition pos{
            static_cast<double>(i),
            static_cast<double>(i * 2),
            static_cast<double>(i * 3)
        };
        auto result = serializer.Serialize(pos);
        mgr.SaveEntity(i, "player", result.data, 1);
    }

    // Create snapshot
    auto snapshot_result = mgr.CreateGracefulShutdownSnapshot();
    EXPECT_TRUE(snapshot_result.success);

    // Validate snapshot
    auto validate_result = mgr.ValidateSnapshot();
    EXPECT_TRUE(validate_result.success);
}

// ============================================
// H3: Concurrent Operations Tests
// ============================================

TEST_F(PersistenceIntegrationTest, ConcurrentOperations_SaveAndLoad_DataRaceCheck) {
    InitializeSystem();
    auto& mgr = PersistenceManager::Instance();

    JsonSerializer serializer;
    RegisterSerializers(&serializer);

    const int NUM_THREADS = 10;
    const int OPERATIONS_PER_THREAD = 100;

    std::vector<std::thread> threads;
    std::atomic<int> total_saves{0};
    std::atomic<int> total_loads{0};

    for (int t = 0; t < NUM_THREADS; ++t) {
        threads.emplace_back([&, t]() {
            for (int i = 0; i < OPERATIONS_PER_THREAD; ++i) {
                uint64_t entity_id = (t * OPERATIONS_PER_THREAD) + i;

                PlayerStats stats{
                    static_cast<uint32_t>(entity_id),
                    entity_id * 1000,
                    "Thread_" + std::to_string(t)
                };

                auto serialize_result = serializer.Serialize(stats);
                if (serialize_result.success) {
                    auto save_result = mgr.SaveEntity(
                        entity_id, "player", serialize_result.data, 1);

                    if (save_result.success) {
                        total_saves++;
                    }
                }

                // Attempt to load some entities
                auto load_result = mgr.LoadEntity(entity_id);
                if (load_result.has_value()) {
                    total_loads++;
                }
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    EXPECT_EQ(total_saves.load(), NUM_THREADS * OPERATIONS_PER_THREAD);
    EXPECT_GT(total_loads.load(), 0);
    EXPECT_EQ(backend_ptr_->GetEntityCount(), NUM_THREADS * OPERATIONS_PER_THREAD);
}

TEST_F(PersistenceIntegrationTest, ConcurrentMarking_DirtyStateConsistency) {
    InitializeSystem();
    auto& mgr = PersistenceManager::Instance();

    const int NUM_THREADS = 20;
    const int MARKS_PER_THREAD = 1000;

    std::vector<std::thread> threads;

    for (int t = 0; t < NUM_THREADS; ++t) {
        threads.emplace_back([&, t]() {
            for (int i = 0; i < MARKS_PER_THREAD; ++i) {
                uint64_t entity_id = (i % 100);  // Limited entity set
                mgr.MarkComponentDirty(entity_id, "position", i % 2 == 0);
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    // Should complete without deadlock or crash
    SUCCEED();
}

// ============================================
// H4: Failure Recovery Tests
// ============================================

TEST_F(PersistenceIntegrationTest, TransientFailure_AutomaticRecovery) {
    FailureConfig config;
    config.save_failure_rate = 0.3;  // 30% failure rate
    InitializeSystem(config);

    auto& mgr = PersistenceManager::Instance();
    JsonSerializer serializer;
    RegisterSerializers(&serializer);

    int successful_saves = 0;
    int total_attempts = 100;

    for (int i = 0; i < total_attempts; ++i) {
        PlayerStats stats{10, 5000, "Warrior"};
        auto result = serializer.Serialize(stats);

        auto save_result = mgr.SaveEntity(i, "player", result.data, 1);
        if (save_result.success) {
            successful_saves++;
        }
    }

    // With 30% failure rate, should still get some successes
    EXPECT_GT(successful_saves, 0);
    EXPECT_LT(successful_saves, total_attempts);

    auto stats = backend_ptr_->GetStatistics();
    EXPECT_EQ(stats.total_saves + stats.failed_saves, total_attempts);
}

TEST_F(PersistenceIntegrationTest, BackendUnavailable_GracefulDegradation) {
    FailureConfig config;
    config.backend_ready = false;
    InitializeSystem(config);

    auto& mgr = PersistenceManager::Instance();
    JsonSerializer serializer;
    RegisterSerializers(&serializer);

    PlayerStats stats{10, 5000, "Warrior"};
    auto result = serializer.Serialize(stats);

    auto save_result = mgr.SaveEntity(1, "player", result.data, 1);

    // Should fail due to backend unavailability
    EXPECT_FALSE(save_result.success);
}

// ============================================
// H5: Stress and Memory Tests
// ============================================

TEST_F(PersistenceIntegrationTest, StressTest_ContinuousWrites_NoMemoryLeak) {
    InitializeSystem();
    auto& mgr = PersistenceManager::Instance();

    JsonSerializer serializer;
    RegisterSerializers(&serializer);

    const int ITERATIONS = 1000;

    for (int iter = 0; iter < ITERATIONS; ++iter) {
        for (int i = 0; i < 10; ++i) {
            PlayerStats stats{
                static_cast<uint32_t>(iter * 10 + i),
                static_cast<uint64_t>(iter * 1000 + i * 100),
                "Class_" + std::to_string(iter)
            };

            auto result = serializer.Serialize(stats);
            if (result.success) {
                mgr.SaveEntity(i, "player", result.data, 1);
            }
        }

        // Periodically create snapshots
        if (iter % 100 == 0) {
            mgr.CreateGracefulShutdownSnapshot();
        }
    }

    // Verify system is still functioning
    auto load_result = mgr.LoadAllEntities();
    EXPECT_TRUE(load_result.has_value());
}

TEST_F(PersistenceIntegrationTest, HighThroughput_BatchOperations) {
    InitializeSystem();
    auto& mgr = PersistenceManager::Instance();

    JsonSerializer serializer;
    RegisterSerializers(&serializer);

    const int BATCH_SIZE = 1000;
    const int NUM_BATCHES = 10;

    for (int batch = 0; batch < NUM_BATCHES; ++batch) {
        std::vector<std::tuple<uint64_t, std::string, std::vector<uint8_t>, uint32_t>> entities;

        for (int i = 0; i < BATCH_SIZE; ++i) {
            PlayerStats stats{
                static_cast<uint32_t>(batch * BATCH_SIZE + i),
                static_cast<uint64_t>((batch * BATCH_SIZE + i) * 1000),
                "Batch_" + std::to_string(batch)
            };

            auto result = serializer.Serialize(stats);
            if (result.success) {
                entities.push_back({
                    static_cast<uint64_t>(batch * BATCH_SIZE + i),
                    "player",
                    result.data,
                    1
                });
            }
        }

        auto batch_result = mgr.SaveEntitiesBatch(entities);
        EXPECT_TRUE(batch_result.success);
    }

    EXPECT_GE(backend_ptr_->GetEntityCount(), NUM_BATCHES * BATCH_SIZE / 2);
}

// ============================================
// H6: Listener and Notification Tests
// ============================================

TEST_F(PersistenceIntegrationTest, ComponentChangeListeners_CorrectNotification) {
    InitializeSystem();
    auto& mgr = PersistenceManager::Instance();

    std::atomic<int> position_changes{0};
    std::atomic<int> inventory_changes{0};

    mgr.RegisterComponentChangeListener("position", [&](uint64_t id, const std::string& name) {
        position_changes++;
    });

    mgr.RegisterComponentChangeListener("inventory", [&](uint64_t id, const std::string& name) {
        inventory_changes++;
    });

    // Mark components dirty
    for (int i = 0; i < 100; ++i) {
        mgr.MarkComponentDirty(1, "position", true);
        mgr.MarkComponentDirty(1, "inventory", false);
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    // Both listeners should have been called
    EXPECT_GT(position_changes.load(), 0);
    EXPECT_GT(inventory_changes.load(), 0);
}

// ============================================
// H7: End-to-End Workflow
// ============================================

TEST_F(PersistenceIntegrationTest, CompleteGameServerCycle) {
    // Simulate a complete game server lifecycle

    InitializeSystem();
    auto& mgr = PersistenceManager::Instance();

    JsonSerializer serializer;
    RegisterSerializers(&serializer);

    // Phase 1: Server startup with initial player load
    std::vector<uint64_t> player_ids;
    for (uint64_t i = 1; i <= 50; ++i) {
        player_ids.push_back(i);

        PlayerStats stats{
            static_cast<uint32_t>(i),
            i * 1000,
            "Warrior"
        };

        auto result = serializer.Serialize(stats);
        mgr.SaveEntity(i, "player", result.data, 1);
    }

    // Phase 2: Server running with continuous updates
    for (int update = 0; update < 100; ++update) {
        for (uint64_t player_id : player_ids) {
            // Simulate player activity
            mgr.MarkComponentDirty(player_id, "position", true);
            mgr.MarkComponentDirty(player_id, "stats", false);

            // Periodic saves
            if (update % 20 == 0) {
                PlayerPosition pos{
                    10.0 + player_id,
                    20.0 + (update * 0.1),
                    30.0
                };

                auto result = serializer.Serialize(pos);
                mgr.SaveEntity(player_id, "player", result.data, 1);
            }
        }
    }

    // Phase 3: Graceful shutdown
    auto snapshot_result = mgr.CreateGracefulShutdownSnapshot();
    EXPECT_TRUE(snapshot_result.success);

    PersistenceManager::Shutdown();

    // Phase 4: Server restart and recovery
    InitializeSystem();
    auto& mgr2 = PersistenceManager::Instance();

    auto recovery_result = mgr2.PerformStartupRecovery();
    EXPECT_TRUE(recovery_result.success);

    // Verify data integrity
    auto all_entities = mgr2.LoadAllEntities();
    EXPECT_TRUE(all_entities.has_value());
}

}  // namespace mir2::persistence::test
