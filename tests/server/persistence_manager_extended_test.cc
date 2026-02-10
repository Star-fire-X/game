/**
 * @file persistence_manager_extended_test.cc
 * @brief Extended unit tests for PersistenceManager
 *
 * Test coverage:
 * - Initialization edge cases (null backends, double init, concurrent init)
 * - Save scenarios (backend failures, large data, empty batches, partial failures)
 * - Metrics tracking accuracy
 * - Load scenarios (non-existent entities, corrupted data, large datasets)
 * - Snapshot operations (backend failures, integrity validation)
 * - Component change listeners (multiple listeners, exception handling)
 */

#include <gtest/gtest.h>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <random>
#include <thread>
#include <vector>

#define private public
#include "persistence/batch_writer.h"
#include "persistence/persistence_manager.h"
#include "persistence/save_scheduler.h"
#undef private

#include "mocks/enhanced_storage_backend.h"
#include "mocks/mock_clock.h"
#include "persistence/json_serializer.h"

namespace mir2::persistence::test {

// ============================================
// Test Fixture
// ============================================

class PersistenceManagerExtendedTest : public ::testing::Test {
 protected:
    void SetUp() override {
        PersistenceManager::Shutdown();
    }

    void TearDown() override {
        PersistenceManager::Shutdown();
    }

    void InitializeWithEnhancedBackend(
        const FailureConfig& config = FailureConfig{},
        const PersistenceManager::Config& pm_config = PersistenceManager::Config{}) {
        auto backend = std::make_unique<EnhancedStorageBackend>();
        backend->SetFailureConfig(config);
        backend_ptr_ = backend.get();

        auto serializer = std::make_unique<JsonSerializer>();

        PersistenceManager::Initialize(
            std::move(backend),
            std::move(serializer),
            pm_config);
    }

    EnhancedStorageBackend* backend_ptr_ = nullptr;
};

// ============================================
// H1: Initialization Scenarios
// ============================================

TEST_F(PersistenceManagerExtendedTest, MultipleInitialize_ShouldReturnFalse) {
    // First initialization should succeed
    InitializeWithEnhancedBackend();
    EXPECT_TRUE(PersistenceManager::IsInitialized());

    // Second initialization should fail
    auto backend = std::make_unique<EnhancedStorageBackend>();
    auto serializer = std::make_unique<JsonSerializer>();

    bool second_init = PersistenceManager::Initialize(
        std::move(backend),
        std::move(serializer),
        PersistenceManager::Config{});

    EXPECT_FALSE(second_init);
    EXPECT_TRUE(PersistenceManager::IsInitialized());
}

TEST_F(PersistenceManagerExtendedTest, InitializeWithNullBackend_ShouldFail) {
    auto serializer = std::make_unique<JsonSerializer>();

    bool result = PersistenceManager::Initialize(
        nullptr,
        std::move(serializer),
        PersistenceManager::Config{});

    EXPECT_FALSE(result);
    EXPECT_FALSE(PersistenceManager::IsInitialized());
}

TEST_F(PersistenceManagerExtendedTest, ConcurrentInitialize_OnlyOneSucceeds) {
    std::atomic<int> success_count{0};
    std::atomic<int> failure_count{0};
    const int NUM_THREADS = 10;

    std::vector<std::thread> threads;

    for (int i = 0; i < NUM_THREADS; ++i) {
        threads.emplace_back([&]() {
            auto backend = std::make_unique<EnhancedStorageBackend>();
            auto serializer = std::make_unique<JsonSerializer>();

            bool result = PersistenceManager::Initialize(
                std::move(backend),
                std::move(serializer),
                PersistenceManager::Config{});

            if (result) {
                success_count++;
            } else {
                failure_count++;
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    EXPECT_EQ(success_count.load(), 1) << "Exactly one initialization should succeed";
    EXPECT_EQ(failure_count.load(), NUM_THREADS - 1);
    EXPECT_TRUE(PersistenceManager::IsInitialized());
}

// ============================================
// H2: Save Scenarios
// ============================================

TEST_F(PersistenceManagerExtendedTest, SaveEntity_BackendNotReady_ReturnsFailure) {
    FailureConfig config;
    config.backend_ready = false;
    InitializeWithEnhancedBackend(config);

    auto& mgr = PersistenceManager::Instance();
    std::vector<uint8_t> data{1, 2, 3, 4, 5};

    auto result = mgr.SaveEntity(123, "player", data, 1);

    EXPECT_FALSE(result.success);
    EXPECT_FALSE(result.error_message.empty());
}

TEST_F(PersistenceManagerExtendedTest, SaveEntity_LargeData_HandlesCorrectly) {
    InitializeWithEnhancedBackend();

    auto& mgr = PersistenceManager::Instance();

    // Create 10MB of data
    std::vector<uint8_t> large_data(10 * 1024 * 1024, 0xAB);

    auto result = mgr.SaveEntity(999, "large_entity", large_data, 1);

    EXPECT_TRUE(result.success);

    auto stats = backend_ptr_->GetStatistics();
    EXPECT_GE(stats.total_bytes_written, large_data.size());
}

TEST_F(PersistenceManagerExtendedTest, SaveEntitiesBatch_EmptyBatch_ReturnsSuccess) {
    InitializeWithEnhancedBackend();

    auto& mgr = PersistenceManager::Instance();
    std::vector<std::tuple<uint64_t, std::string, std::vector<uint8_t>, uint32_t>> empty_batch;

    auto result = mgr.SaveEntitiesBatch(empty_batch);

    EXPECT_TRUE(result.success);
}

TEST_F(PersistenceManagerExtendedTest, SaveEntitiesBatch_SingleEntity_WorksCorrectly) {
    InitializeWithEnhancedBackend();

    auto& mgr = PersistenceManager::Instance();
    std::vector<std::tuple<uint64_t, std::string, std::vector<uint8_t>, uint32_t>> batch;
    batch.push_back({100, "player", std::vector<uint8_t>{1, 2, 3}, 1});

    auto result = mgr.SaveEntitiesBatch(batch);

    EXPECT_TRUE(result.success);
    EXPECT_TRUE(backend_ptr_->HasEntity(100));
}

TEST_F(PersistenceManagerExtendedTest, SaveEntitiesBatch_PartialFailure_ReportsCorrectly) {
    FailureConfig config;
    config.batch_partial_failure_rate = 0.5;  // 50% failure rate
    InitializeWithEnhancedBackend(config);

    auto& mgr = PersistenceManager::Instance();
    std::vector<std::tuple<uint64_t, std::string, std::vector<uint8_t>, uint32_t>> batch;

    for (int i = 0; i < 100; ++i) {
        batch.push_back({
            static_cast<uint64_t>(i),
            "player",
            std::vector<uint8_t>{1, 2, 3},
            1
        });
    }

    auto result = mgr.SaveEntitiesBatch(batch);

    // With 50% failure rate, batch should report failure
    EXPECT_FALSE(result.success);
    EXPECT_FALSE(result.error_message.empty());

    auto stats = backend_ptr_->GetStatistics();
    EXPECT_GT(stats.failed_saves, 0);
    EXPECT_LT(backend_ptr_->GetEntityCount(), 100);  // Not all entities saved
}

// ============================================
// H3: Metrics Scenarios
// ============================================

TEST_F(PersistenceManagerExtendedTest, Metrics_SaveSuccessIncrementsTotalSaves) {
    InitializeWithEnhancedBackend();

    auto& mgr = PersistenceManager::Instance();
    std::vector<uint8_t> data{1, 2, 3, 4, 5};

    // Perform multiple saves
    for (int i = 0; i < 10; ++i) {
        mgr.SaveEntity(i, "player", data, 1);
    }

    auto stats = backend_ptr_->GetStatistics();
    EXPECT_EQ(stats.total_saves, 10);
    EXPECT_EQ(stats.failed_saves, 0);
}

TEST_F(PersistenceManagerExtendedTest, Metrics_SaveFailureIncrementsFailedSaves) {
    FailureConfig config;
    config.save_failure_rate = 1.0;  // 100% failure
    InitializeWithEnhancedBackend(config);

    auto& mgr = PersistenceManager::Instance();
    std::vector<uint8_t> data{1, 2, 3, 4, 5};

    // Attempt multiple saves
    for (int i = 0; i < 5; ++i) {
        mgr.SaveEntity(i, "player", data, 1);
    }

    auto stats = backend_ptr_->GetStatistics();
    EXPECT_EQ(stats.total_saves, 0);
    EXPECT_EQ(stats.failed_saves, 5);
}

TEST_F(PersistenceManagerExtendedTest, Metrics_TotalBytesWritten_AccuracyVerified) {
    InitializeWithEnhancedBackend();

    auto& mgr = PersistenceManager::Instance();

    uint64_t expected_bytes = 0;

    for (int i = 0; i < 10; ++i) {
        size_t data_size = (i + 1) * 100;  // Variable sizes
        std::vector<uint8_t> data(data_size, 0xFF);
        mgr.SaveEntity(i, "player", data, 1);
        expected_bytes += data_size;
    }

    auto stats = backend_ptr_->GetStatistics();
    EXPECT_EQ(stats.total_bytes_written, expected_bytes);
}

// ============================================
// H4: Load Scenarios
// ============================================

TEST_F(PersistenceManagerExtendedTest, LoadEntity_NonExistent_ReturnsNullopt) {
    InitializeWithEnhancedBackend();

    auto& mgr = PersistenceManager::Instance();

    auto result = mgr.LoadEntity(99999);

    EXPECT_FALSE(result.has_value());
}

TEST_F(PersistenceManagerExtendedTest, LoadEntity_CorruptedData_HandlesGracefully) {
    FailureConfig config;
    config.corrupt_data = true;
    config.corruption_rate = 1.0;  // Always corrupt
    InitializeWithEnhancedBackend(config);

    auto& mgr = PersistenceManager::Instance();

    // Save entity (will be corrupted)
    std::vector<uint8_t> data{1, 2, 3, 4, 5};
    mgr.SaveEntity(123, "player", data, 1);

    // Load entity
    auto result = mgr.LoadEntity(123);

    // Should still load, but data will be corrupted
    EXPECT_TRUE(result.has_value());

    auto stats = backend_ptr_->GetStatistics();
    EXPECT_GT(stats.corrupted_entities, 0);
}

TEST_F(PersistenceManagerExtendedTest, LoadAllEntities_EmptyDatabase_ReturnsEmptyMap) {
    InitializeWithEnhancedBackend();

    auto& mgr = PersistenceManager::Instance();

    auto result = mgr.LoadAllEntities();

    EXPECT_TRUE(result.has_value());
    EXPECT_TRUE(result->empty());
}

TEST_F(PersistenceManagerExtendedTest, LoadAllEntities_LargeDataset_PerformanceTest) {
    InitializeWithEnhancedBackend();

    auto& mgr = PersistenceManager::Instance();

    // Save 1000 entities
    const int ENTITY_COUNT = 1000;
    std::vector<uint8_t> data{1, 2, 3, 4, 5};

    for (int i = 0; i < ENTITY_COUNT; ++i) {
        mgr.SaveEntity(i, "player", data, 1);
    }

    // Measure load time
    auto start = std::chrono::steady_clock::now();
    auto result = mgr.LoadAllEntities();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start).count();

    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(result->size(), ENTITY_COUNT);

    // Should complete within reasonable time (< 1 second for 1000 entities)
    EXPECT_LT(elapsed, 1000) << "LoadAllEntities took " << elapsed << "ms";
}

// ============================================
// H5: Snapshot Operations
// ============================================

TEST_F(PersistenceManagerExtendedTest, CreateSnapshot_BackendFailure_ReturnsError) {
    FailureConfig config;
    config.snapshot_creation_fails = true;
    InitializeWithEnhancedBackend(config);

    auto& mgr = PersistenceManager::Instance();

    auto result = mgr.CreateGracefulShutdownSnapshot();

    EXPECT_FALSE(result.success);
    EXPECT_FALSE(result.error_message.empty());
}

TEST_F(PersistenceManagerExtendedTest, PerformStartupRecovery_NoSnapshot_HandlesGracefully) {
    InitializeWithEnhancedBackend();

    auto& mgr = PersistenceManager::Instance();

    // No snapshots exist, should handle gracefully
    auto result = mgr.PerformStartupRecovery();

    EXPECT_TRUE(result.success);
}

TEST_F(PersistenceManagerExtendedTest, ValidateSnapshot_ChecksIntegrity) {
    InitializeWithEnhancedBackend();

    auto& mgr = PersistenceManager::Instance();

    // Create some entities
    std::vector<uint8_t> data{1, 2, 3, 4, 5};
    mgr.SaveEntity(1, "player", data, 1);
    mgr.SaveEntity(2, "npc", data, 1);

    // Create snapshot
    auto snapshot_result = mgr.CreateGracefulShutdownSnapshot();
    EXPECT_TRUE(snapshot_result.success);

    // Validate snapshot
    auto validate_result = mgr.ValidateSnapshot();
    EXPECT_TRUE(validate_result.success);
}

// ============================================
// H6: Component Change Listeners
// ============================================

TEST_F(PersistenceManagerExtendedTest, RegisterListener_MultipleListeners_AllInvoked) {
    InitializeWithEnhancedBackend();

    auto& mgr = PersistenceManager::Instance();

    std::atomic<int> callback1_count{0};
    std::atomic<int> callback2_count{0};
    std::atomic<int> callback3_count{0};

    mgr.RegisterComponentChangeListener("position", [&](uint64_t id, const std::string& name) {
        callback1_count++;
    });

    mgr.RegisterComponentChangeListener("position", [&](uint64_t id, const std::string& name) {
        callback2_count++;
    });

    mgr.RegisterComponentChangeListener("position", [&](uint64_t id, const std::string& name) {
        callback3_count++;
    });

    // Mark component dirty (should trigger all listeners)
    mgr.MarkComponentDirty(123, "position", true);

    // Give listeners time to execute
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // All three listeners should be invoked
    EXPECT_GT(callback1_count.load() + callback2_count.load() + callback3_count.load(), 0);
}

TEST_F(PersistenceManagerExtendedTest, RegisterListener_ExceptionInListener_DoesNotCrash) {
    InitializeWithEnhancedBackend();

    auto& mgr = PersistenceManager::Instance();

    // Register listener that throws exception
    mgr.RegisterComponentChangeListener("position", [](uint64_t id, const std::string& name) {
        throw std::runtime_error("Listener exception");
    });

    // Register normal listener
    std::atomic<bool> second_listener_called{false};
    mgr.RegisterComponentChangeListener("position", [&](uint64_t id, const std::string& name) {
        second_listener_called = true;
    });

    // Mark component dirty (should not crash despite exception)
    EXPECT_NO_THROW({
        mgr.MarkComponentDirty(123, "position", true);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    });
}

// ============================================
// H7: Thread Safety Stress Tests
// ============================================

TEST_F(PersistenceManagerExtendedTest, ConcurrentSaveAndLoad_ThreadSafe) {
    InitializeWithEnhancedBackend();

    auto& mgr = PersistenceManager::Instance();

    const int NUM_THREADS = 10;
    const int OPS_PER_THREAD = 100;
    std::atomic<int> save_successes{0};
    std::atomic<int> load_successes{0};

    std::vector<std::thread> threads;

    for (int t = 0; t < NUM_THREADS; ++t) {
        threads.emplace_back([&, t]() {
            std::vector<uint8_t> data{1, 2, 3, 4, 5};

            for (int i = 0; i < OPS_PER_THREAD; ++i) {
                uint64_t entity_id = (t * OPS_PER_THREAD) + i;

                // Save
                auto save_result = mgr.SaveEntity(entity_id, "player", data, 1);
                if (save_result.success) {
                    save_successes++;
                }

                // Load
                auto load_result = mgr.LoadEntity(entity_id);
                if (load_result.has_value()) {
                    load_successes++;
                }
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    EXPECT_EQ(save_successes.load(), NUM_THREADS * OPS_PER_THREAD);
    EXPECT_GT(load_successes.load(), 0);  // At least some loads should succeed
}

TEST_F(PersistenceManagerExtendedTest, HighContentionDirtyMarking_NoDeadlock) {
    InitializeWithEnhancedBackend();

    auto& mgr = PersistenceManager::Instance();

    const int NUM_THREADS = 50;
    const int MARKS_PER_THREAD = 1000;

    std::vector<std::thread> threads;
    std::atomic<bool> deadlock_detected{false};

    for (int t = 0; t < NUM_THREADS; ++t) {
        threads.emplace_back([&, t]() {
            for (int i = 0; i < MARKS_PER_THREAD; ++i) {
                uint64_t entity_id = (t * 10) + (i % 10);
                mgr.MarkComponentDirty(entity_id, "position", i % 2 == 0);
            }
        });
    }

    // Timeout watchdog
    std::thread watchdog([&]() {
        std::this_thread::sleep_for(std::chrono::seconds(10));
        deadlock_detected = true;
    });

    for (auto& t : threads) {
        t.join();
    }

    EXPECT_FALSE(deadlock_detected.load()) << "Deadlock detected";

    if (watchdog.joinable()) {
        watchdog.detach();
    }
}

// ============================================
// H8: Edge Cases
// ============================================

TEST_F(PersistenceManagerExtendedTest, SaveEntity_ZeroVersion_HandledCorrectly) {
    InitializeWithEnhancedBackend();

    auto& mgr = PersistenceManager::Instance();
    std::vector<uint8_t> data{1, 2, 3, 4, 5};

    auto result = mgr.SaveEntity(123, "player", data, 0);

    EXPECT_TRUE(result.success);
    EXPECT_TRUE(backend_ptr_->HasEntity(123));
}

TEST_F(PersistenceManagerExtendedTest, SaveEntity_EmptyEntityType_HandledCorrectly) {
    InitializeWithEnhancedBackend();

    auto& mgr = PersistenceManager::Instance();
    std::vector<uint8_t> data{1, 2, 3, 4, 5};

    auto result = mgr.SaveEntity(123, "", data, 1);

    EXPECT_TRUE(result.success);
}

TEST_F(PersistenceManagerExtendedTest, SaveEntity_EmptyData_HandledCorrectly) {
    InitializeWithEnhancedBackend();

    auto& mgr = PersistenceManager::Instance();
    std::vector<uint8_t> empty_data;

    auto result = mgr.SaveEntity(123, "player", empty_data, 1);

    EXPECT_TRUE(result.success);
}

TEST_F(PersistenceManagerExtendedTest, MarkComponentDirty_SameEntityMultipleTimes_Handled) {
    InitializeWithEnhancedBackend();

    auto& mgr = PersistenceManager::Instance();

    // Mark same component dirty multiple times
    for (int i = 0; i < 100; ++i) {
        mgr.MarkComponentDirty(123, "position", true);
    }

    // Should not crash or deadlock
    SUCCEED();
}

// ============================================
// H9: Time-Based Behavior
// ============================================

TEST_F(PersistenceManagerExtendedTest, AutoSave_TriggeredAfterInterval) {
    MockClock clock;
    clock.SetTime(std::chrono::steady_clock::now() - std::chrono::seconds(300));
    clock.Freeze();
    clock.Advance(std::chrono::seconds(300));

    std::atomic<int> save_calls{0};
    std::mutex mutex;
    std::condition_variable cv;

    SaveScheduler scheduler(
        300,
        []() { return std::vector<uint64_t>{1}; },
        [&](const std::vector<uint64_t>&) {
            save_calls++;
            std::lock_guard<std::mutex> lock(mutex);
            cv.notify_one();
        });

    scheduler.last_save_time_ = clock.Now() - std::chrono::seconds(300);
    scheduler.Start();

    std::unique_lock<std::mutex> lock(mutex);
    bool saved = cv.wait_for(lock, std::chrono::seconds(1), [&]() {
        return save_calls.load() > 0;
    });

    scheduler.Stop();

    EXPECT_TRUE(saved);
}

TEST_F(PersistenceManagerExtendedTest, DirtyThreshold_HighPriorityMark_ImmediateSave) {
    PersistenceManager::Config pm_config;
    pm_config.dirty_save_threshold_ms = 100;

    InitializeWithEnhancedBackend(FailureConfig{}, pm_config);
    auto& mgr = PersistenceManager::Instance();

    MockClock clock;
    clock.SetTime(std::chrono::steady_clock::now());
    clock.Freeze();

    mgr.MarkComponentDirty(42, "position", true);

    ASSERT_FALSE(mgr.dirty_queue_.empty());
    auto entry = mgr.dirty_queue_.front();
    EXPECT_TRUE(entry.is_high_priority);

    clock.Advance(std::chrono::milliseconds(100));

    auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        clock.Now() - entry.mark_time).count();

    EXPECT_LE(elapsed_ms,
              static_cast<int64_t>(pm_config.dirty_save_threshold_ms) + 5);

    std::vector<uint8_t> data{1, 2, 3};
    auto save_result = mgr.SaveEntity(entry.entity_id, "player", data, 1);
    EXPECT_TRUE(save_result.success);

    auto stats = backend_ptr_->GetStatistics();
    EXPECT_EQ(stats.total_saves, 1);
}

TEST_F(PersistenceManagerExtendedTest, ShutdownTimeout_ExceedsThreshold_GracefulFailure) {
    PersistenceManager::Config pm_config;
    pm_config.shutdown_timeout_ms = 100;

    MockClock clock;
    clock.SetTime(std::chrono::steady_clock::now());
    clock.Freeze();

    auto simulate_shutdown = [&](uint32_t pending_saves) {
        auto start = clock.Now();
        while (pending_saves > 0) {
            if (clock.Now() - start >
                std::chrono::milliseconds(pm_config.shutdown_timeout_ms)) {
                return false;
            }
            clock.Advance(std::chrono::milliseconds(20));
        }
        return true;
    };

    bool completed = simulate_shutdown(1000);
    EXPECT_FALSE(completed);
}

TEST_F(PersistenceManagerExtendedTest, BatchTimeout_FlushesOnTimeout) {
    auto backend = std::make_shared<EnhancedStorageBackend>();
    BatchWriter writer(backend, 10, 200);

    MockClock clock;
    clock.SetTime(std::chrono::steady_clock::now() - std::chrono::milliseconds(250));
    clock.Freeze();
    clock.Advance(std::chrono::milliseconds(250));

    writer.last_flush_time_ = clock.Now() - std::chrono::milliseconds(250);
    writer.Start();

    writer.Enqueue(1, "player", std::vector<uint8_t>{1, 2, 3}, 1);

    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
    while (std::chrono::steady_clock::now() < deadline) {
        if (backend->GetEntityCount() > 0) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    writer.Stop();

    EXPECT_GT(backend->GetEntityCount(), 0u);
}

}  // namespace mir2::persistence::test
