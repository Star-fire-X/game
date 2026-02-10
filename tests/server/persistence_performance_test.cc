/**
 * @file persistence_performance_test.cc
 * @brief Performance benchmark tests for persistence system
 *
 * Benchmarks:
 * - SaveEntity latency (P50, P95, P99)
 * - SaveEntitiesBatch throughput
 * - LoadEntity performance (cached vs uncached)
 * - Memory usage under load
 * - Dirty tracking overhead
 */

#include <gtest/gtest.h>
#include "persistence/persistence_manager.h"
#include "persistence/json_serializer.h"
#include "mocks/enhanced_storage_backend.h"
#include <chrono>
#include <vector>
#include <algorithm>
#include <numeric>

namespace mir2::persistence::test {

// ============================================
// Performance Test Components
// ============================================

struct BenchmarkComponent {
    uint64_t id;
    std::string name;
    std::vector<double> data;

    bool operator==(const BenchmarkComponent& other) const {
        return id == other.id && name == other.name && data == other.data;
    }
};

// ============================================
// Performance Test Fixture
// ============================================

class PersistencePerformanceTest : public ::testing::Test {
 protected:
    void SetUp() override {
        PersistenceManager::Shutdown();
    }

    void TearDown() override {
        PersistenceManager::Shutdown();
    }

    void InitializeSystem() {
        auto backend = std::make_unique<EnhancedStorageBackend>();
        backend_ptr_ = backend.get();

        auto serializer = std::make_unique<JsonSerializer>();
        RegisterSerializer(serializer.get());

        PersistenceManager::Initialize(
            std::move(backend),
            std::move(serializer),
            PersistenceManager::Config{});
    }

    void RegisterSerializer(JsonSerializer* serializer) {
        serializer->RegisterCustom<BenchmarkComponent>(
            [](const BenchmarkComponent& c) {
                json j;
                j["id"] = c.id;
                j["name"] = c.name;
                j["data"] = c.data;
                return j;
            },
            [](const json& j, BenchmarkComponent& c) {
                c.id = j["id"].get<uint64_t>();
                c.name = j["name"].get<std::string>();
                c.data = j["data"].get<std::vector<double>>();
                return true;
            });
    }

    EnhancedStorageBackend* backend_ptr_ = nullptr;

    // ============================================
    // Latency Measurement Utilities
    // ============================================

    struct LatencyStats {
        double p50_ms;
        double p95_ms;
        double p99_ms;
        double min_ms;
        double max_ms;
        double avg_ms;
    };

    LatencyStats CalculateLatencyStats(const std::vector<int64_t>& latencies_us) {
        if (latencies_us.empty()) {
            return LatencyStats{0, 0, 0, 0, 0, 0};
        }

        std::vector<int64_t> sorted = latencies_us;
        std::sort(sorted.begin(), sorted.end());

        LatencyStats stats;
        stats.min_ms = sorted.front() / 1000.0;
        stats.max_ms = sorted.back() / 1000.0;
        stats.avg_ms = std::accumulate(sorted.begin(), sorted.end(), 0.0) / sorted.size() / 1000.0;

        size_t p50_idx = sorted.size() * 50 / 100;
        size_t p95_idx = sorted.size() * 95 / 100;
        size_t p99_idx = sorted.size() * 99 / 100;

        stats.p50_ms = sorted[p50_idx] / 1000.0;
        stats.p95_ms = sorted[p95_idx] / 1000.0;
        stats.p99_ms = sorted[p99_idx] / 1000.0;

        return stats;
    }

    void PrintLatencyStats(const std::string& name, const LatencyStats& stats) {
        std::cout << "\n=== " << name << " ===\n";
        std::cout << "  Min:    " << stats.min_ms << " ms\n";
        std::cout << "  P50:    " << stats.p50_ms << " ms\n";
        std::cout << "  P95:    " << stats.p95_ms << " ms\n";
        std::cout << "  P99:    " << stats.p99_ms << " ms\n";
        std::cout << "  Max:    " << stats.max_ms << " ms\n";
        std::cout << "  Avg:    " << stats.avg_ms << " ms\n";
    }
};

// ============================================
// Benchmark 1: SaveEntity Latency
// ============================================

TEST_F(PersistencePerformanceTest, BenchmarkSaveEntity_Latency_P50P95P99) {
    InitializeSystem();
    auto& mgr = PersistenceManager::Instance();

    JsonSerializer serializer;
    RegisterSerializer(&serializer);

    const int NUM_ITERATIONS = 1000;
    std::vector<int64_t> latencies_us;

    // Warm-up
    for (int i = 0; i < 10; ++i) {
        BenchmarkComponent comp{
            static_cast<uint64_t>(i),
            "warmup",
            {1.0, 2.0, 3.0}
        };
        auto result = serializer.Serialize(comp);
        mgr.SaveEntity(i, "benchmark", result.data, 1);
    }

    // Actual benchmark
    for (int i = 0; i < NUM_ITERATIONS; ++i) {
        BenchmarkComponent comp{
            static_cast<uint64_t>(i),
            "benchmark_" + std::to_string(i),
            {1.0, 2.0, 3.0, 4.0, 5.0}
        };

        auto result = serializer.Serialize(comp);

        auto start = std::chrono::steady_clock::now();
        auto save_result = mgr.SaveEntity(i, "benchmark", result.data, 1);
        auto elapsed_us = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - start).count();

        ASSERT_TRUE(save_result.success);
        latencies_us.push_back(elapsed_us);
    }

    auto stats = CalculateLatencyStats(latencies_us);
    PrintLatencyStats("SaveEntity Latency", stats);

    // Performance assertions
    EXPECT_LT(stats.p50_ms, 5.0) << "P50 latency should be < 5ms";
    EXPECT_LT(stats.p95_ms, 20.0) << "P95 latency should be < 20ms";
    EXPECT_LT(stats.p99_ms, 50.0) << "P99 latency should be < 50ms";
}

// ============================================
// Benchmark 2: SaveEntitiesBatch Throughput
// ============================================

TEST_F(PersistencePerformanceTest, BenchmarkSaveEntitiesBatch100_UnderThreshold) {
    InitializeSystem();
    auto& mgr = PersistenceManager::Instance();

    JsonSerializer serializer;
    RegisterSerializer(&serializer);

    const int BATCH_SIZE = 100;
    const int NUM_BATCHES = 100;
    std::vector<int64_t> batch_latencies_us;

    for (int batch = 0; batch < NUM_BATCHES; ++batch) {
        std::vector<std::tuple<uint64_t, std::string, std::vector<uint8_t>, uint32_t>> entities;

        for (int i = 0; i < BATCH_SIZE; ++i) {
            BenchmarkComponent comp{
                static_cast<uint64_t>(batch * BATCH_SIZE + i),
                "batch_" + std::to_string(batch) + "_" + std::to_string(i),
                {1.0, 2.0, 3.0}
            };

            auto result = serializer.Serialize(comp);
            if (result.success) {
                entities.push_back({
                    static_cast<uint64_t>(batch * BATCH_SIZE + i),
                    "benchmark",
                    result.data,
                    1
                });
            }
        }

        auto start = std::chrono::steady_clock::now();
        auto batch_result = mgr.SaveEntitiesBatch(entities);
        auto elapsed_us = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - start).count();

        ASSERT_TRUE(batch_result.success);
        batch_latencies_us.push_back(elapsed_us);
    }

    auto stats = CalculateLatencyStats(batch_latencies_us);
    PrintLatencyStats("SaveEntitiesBatch(100) Latency", stats);

    // Calculate throughput
    double total_entities = NUM_BATCHES * BATCH_SIZE;
    double total_time_s = std::accumulate(batch_latencies_us.begin(),
                                          batch_latencies_us.end(),
                                          0.0) / 1000000.0;
    double throughput = total_entities / total_time_s;

    std::cout << "\n=== Batch Throughput ===\n";
    std::cout << "  Total entities: " << total_entities << "\n";
    std::cout << "  Total time: " << total_time_s << " s\n";
    std::cout << "  Throughput: " << throughput << " entities/sec\n";

    // Performance assertions
    EXPECT_LT(stats.p50_ms, 100.0) << "P50 batch latency should be < 100ms";
    EXPECT_GT(throughput, 1000.0) << "Throughput should be > 1000 entities/sec";
}

// ============================================
// Benchmark 3: LoadEntity Performance
// ============================================

TEST_F(PersistencePerformanceTest, BenchmarkLoadEntity_CachedVsUncached) {
    InitializeSystem();
    auto& mgr = PersistenceManager::Instance();

    JsonSerializer serializer;
    RegisterSerializer(&serializer);

    // Prepare data
    const int NUM_ENTITIES = 1000;
    for (int i = 0; i < NUM_ENTITIES; ++i) {
        BenchmarkComponent comp{
            static_cast<uint64_t>(i),
            "entity_" + std::to_string(i),
            {1.0, 2.0, 3.0, 4.0, 5.0}
        };

        auto result = serializer.Serialize(comp);
        mgr.SaveEntity(i, "benchmark", result.data, 1);
    }

    // Benchmark: First load (uncached)
    std::vector<int64_t> uncached_latencies_us;

    for (int i = 0; i < NUM_ENTITIES; ++i) {
        auto start = std::chrono::steady_clock::now();
        auto load_result = mgr.LoadEntity(i);
        auto elapsed_us = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - start).count();

        ASSERT_TRUE(load_result.has_value());
        uncached_latencies_us.push_back(elapsed_us);
    }

    auto uncached_stats = CalculateLatencyStats(uncached_latencies_us);
    PrintLatencyStats("LoadEntity (Uncached)", uncached_stats);

    // Benchmark: Second load (potentially cached)
    std::vector<int64_t> cached_latencies_us;

    for (int i = 0; i < NUM_ENTITIES; ++i) {
        auto start = std::chrono::steady_clock::now();
        auto load_result = mgr.LoadEntity(i);
        auto elapsed_us = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - start).count();

        ASSERT_TRUE(load_result.has_value());
        cached_latencies_us.push_back(elapsed_us);
    }

    auto cached_stats = CalculateLatencyStats(cached_latencies_us);
    PrintLatencyStats("LoadEntity (Cached)", cached_stats);

    // Performance assertions
    EXPECT_LT(uncached_stats.p50_ms, 10.0) << "Uncached P50 should be < 10ms";
    EXPECT_LT(cached_stats.p50_ms, 10.0) << "Cached P50 should be < 10ms";
}

// ============================================
// Benchmark 4: Memory Usage Under Load
// ============================================

TEST_F(PersistencePerformanceTest, BenchmarkMemoryUsage_1000DirtyMarks) {
    InitializeSystem();
    auto& mgr = PersistenceManager::Instance();

    const int NUM_MARKS = 1000;
    const int NUM_ENTITIES = 100;

    // Baseline memory usage would require platform-specific code
    // For this test, we'll measure time overhead instead

    auto start = std::chrono::steady_clock::now();

    for (int i = 0; i < NUM_MARKS; ++i) {
        uint64_t entity_id = i % NUM_ENTITIES;
        mgr.MarkComponentDirty(entity_id, "component_" + std::to_string(i % 10), i % 2 == 0);
    }

    auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start).count();

    std::cout << "\n=== Dirty Marking Overhead ===\n";
    std::cout << "  Total marks: " << NUM_MARKS << "\n";
    std::cout << "  Total time: " << elapsed_ms << " ms\n";
    std::cout << "  Avg time per mark: " << (elapsed_ms * 1000.0 / NUM_MARKS) << " us\n";

    // Performance assertion
    EXPECT_LT(elapsed_ms, 100) << "1000 dirty marks should complete in < 100ms";
}

// ============================================
// Benchmark 5: Serialization Performance
// ============================================

TEST_F(PersistencePerformanceTest, BenchmarkSerialization_VariousDataSizes) {
    JsonSerializer serializer;
    RegisterSerializer(&serializer);

    std::vector<size_t> data_sizes = {1, 10, 100, 1000, 10000};

    for (size_t size : data_sizes) {
        BenchmarkComponent comp{
            123,
            "benchmark",
            std::vector<double>(size, 1.23)
        };

        std::vector<int64_t> latencies_us;
        const int ITERATIONS = 100;

        for (int i = 0; i < ITERATIONS; ++i) {
            auto start = std::chrono::steady_clock::now();
            auto result = serializer.Serialize(comp);
            auto elapsed_us = std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now() - start).count();

            ASSERT_TRUE(result.success);
            latencies_us.push_back(elapsed_us);
        }

        auto stats = CalculateLatencyStats(latencies_us);

        std::cout << "\n=== Serialization (data size: " << size << ") ===\n";
        std::cout << "  P50: " << stats.p50_ms << " ms\n";
        std::cout << "  P95: " << stats.p95_ms << " ms\n";
        std::cout << "  P99: " << stats.p99_ms << " ms\n";
    }
}

// ============================================
// Benchmark 6: Deserialization Performance
// ============================================

TEST_F(PersistencePerformanceTest, BenchmarkDeserialization_VariousDataSizes) {
    JsonSerializer serializer;
    RegisterSerializer(&serializer);

    std::vector<size_t> data_sizes = {1, 10, 100, 1000, 10000};

    for (size_t size : data_sizes) {
        BenchmarkComponent comp{
            123,
            "benchmark",
            std::vector<double>(size, 1.23)
        };

        auto serialize_result = serializer.Serialize(comp);
        ASSERT_TRUE(serialize_result.success);

        std::vector<int64_t> latencies_us;
        const int ITERATIONS = 100;

        for (int i = 0; i < ITERATIONS; ++i) {
            auto start = std::chrono::steady_clock::now();
            auto result = serializer.Deserialize<BenchmarkComponent>(serialize_result.data);
            auto elapsed_us = std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now() - start).count();

            ASSERT_TRUE(result.success);
            latencies_us.push_back(elapsed_us);
        }

        auto stats = CalculateLatencyStats(latencies_us);

        std::cout << "\n=== Deserialization (data size: " << size << ") ===\n";
        std::cout << "  P50: " << stats.p50_ms << " ms\n";
        std::cout << "  P95: " << stats.p95_ms << " ms\n";
        std::cout << "  P99: " << stats.p99_ms << " ms\n";
    }
}

// ============================================
// Benchmark 7: Concurrent Save Performance
// ============================================

TEST_F(PersistencePerformanceTest, BenchmarkConcurrentSaves_Throughput) {
    InitializeSystem();
    auto& mgr = PersistenceManager::Instance();

    JsonSerializer serializer;
    RegisterSerializer(&serializer);

    const int NUM_THREADS = 4;
    const int SAVES_PER_THREAD = 1000;

    std::atomic<int64_t> total_elapsed_us{0};
    std::vector<std::thread> threads;

    auto start = std::chrono::steady_clock::now();

    for (int t = 0; t < NUM_THREADS; ++t) {
        threads.emplace_back([&, t]() {
            for (int i = 0; i < SAVES_PER_THREAD; ++i) {
                BenchmarkComponent comp{
                    static_cast<uint64_t>(t * SAVES_PER_THREAD + i),
                    "thread_" + std::to_string(t),
                    {1.0, 2.0, 3.0}
                };

                auto result = serializer.Serialize(comp);
                if (result.success) {
                    mgr.SaveEntity(
                        t * SAVES_PER_THREAD + i,
                        "benchmark",
                        result.data,
                        1);
                }
            }
        });
    }

    for (auto& thread : threads) {
        thread.join();
    }

    auto total_elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start).count();

    double total_ops = NUM_THREADS * SAVES_PER_THREAD;
    double throughput = total_ops / (total_elapsed_ms / 1000.0);

    std::cout << "\n=== Concurrent Save Throughput ===\n";
    std::cout << "  Threads: " << NUM_THREADS << "\n";
    std::cout << "  Total saves: " << total_ops << "\n";
    std::cout << "  Total time: " << total_elapsed_ms << " ms\n";
    std::cout << "  Throughput: " << throughput << " ops/sec\n";

    EXPECT_GT(throughput, 1000.0) << "Concurrent throughput should be > 1000 ops/sec";
}

// ============================================
// Benchmark 8: LoadAllEntities Performance
// ============================================

TEST_F(PersistencePerformanceTest, BenchmarkLoadAllEntities_ScalabilityTest) {
    InitializeSystem();
    auto& mgr = PersistenceManager::Instance();

    JsonSerializer serializer;
    RegisterSerializer(&serializer);

    std::vector<size_t> entity_counts = {100, 500, 1000, 5000};

    for (size_t count : entity_counts) {
        // Clear previous data
        backend_ptr_->ClearEntities();

        // Populate database
        for (size_t i = 0; i < count; ++i) {
            BenchmarkComponent comp{
                i,
                "entity_" + std::to_string(i),
                {1.0, 2.0, 3.0}
            };

            auto result = serializer.Serialize(comp);
            mgr.SaveEntity(i, "benchmark", result.data, 1);
        }

        // Benchmark LoadAllEntities
        auto start = std::chrono::steady_clock::now();
        auto load_result = mgr.LoadAllEntities();
        auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start).count();

        ASSERT_TRUE(load_result.has_value());
        EXPECT_EQ(load_result->size(), count);

        std::cout << "\n=== LoadAllEntities (count: " << count << ") ===\n";
        std::cout << "  Time: " << elapsed_ms << " ms\n";
        std::cout << "  Throughput: " << (count * 1000.0 / elapsed_ms) << " entities/sec\n";
    }
}

}  // namespace mir2::persistence::test
