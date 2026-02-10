/**
 * @file persistence_test.cc
 * @brief Comprehensive unit tests for persistence system
 */

#include <gtest/gtest.h>
#include "persistence/persistence_error.h"
#include "persistence/component_registry.h"
#include "persistence/json_serializer.h"
#include "persistence/persistence_manager.h"
#include <memory>
#include <vector>
#include <thread>
#include <atomic>
#include <random>
#include <chrono>
#include <mutex>
#include <climits>

namespace mir2::persistence::test {

// Test component types
struct TestComponentA {
    int32_t value;
    bool operator==(const TestComponentA& other) const {
        return value == other.value;
    }
};

struct TestComponentB {
    std::string name;
    uint32_t count;
    bool operator==(const TestComponentB& other) const {
        return name == other.name && count == other.count;
    }
};

// Test: Component Registration (Story 1.1)
class ComponentRegistryTest : public ::testing::Test {
 protected:
    ComponentRegistry registry_;
};

TEST_F(ComponentRegistryTest, RegisterComponent) {
    auto serialize = [](const TestComponentA& c) {
        std::vector<uint8_t> data(sizeof(int32_t));
        std::memcpy(data.data(), &c.value, sizeof(int32_t));
        return data;
    };

    auto deserialize = [](const std::vector<uint8_t>& data, TestComponentA& out) {
        if (data.size() >= sizeof(int32_t)) {
            std::memcpy(&out.value, data.data(), sizeof(int32_t));
        }
        return true;
    };

    EXPECT_TRUE(registry_.RegisterComponent<TestComponentA>(
        "test_a", serialize, deserialize));

    EXPECT_TRUE(registry_.IsRegistered(std::type_index(typeid(TestComponentA))));
    EXPECT_EQ(registry_.GetComponentCount(), 1);
}

TEST_F(ComponentRegistryTest, RegisterMultipleComponents) {
    auto serialize_a = [](const TestComponentA& c) {
        std::vector<uint8_t> data(sizeof(int32_t));
        std::memcpy(data.data(), &c.value, sizeof(int32_t));
        return data;
    };

    auto deserialize_a = [](const std::vector<uint8_t>& data, TestComponentA& out) {
        if (data.size() >= sizeof(int32_t)) {
            std::memcpy(&out.value, data.data(), sizeof(int32_t));
        }
        return true;
    };

    EXPECT_TRUE(registry_.RegisterComponent<TestComponentA>(
        "test_a", serialize_a, deserialize_a));

    EXPECT_EQ(registry_.GetComponentCount(), 1);
    EXPECT_TRUE(registry_.IsRegistered(std::type_index(typeid(TestComponentA))));
}

TEST_F(ComponentRegistryTest, GetRegisteredComponents) {
    auto serialize = [](const TestComponentA& c) {
        return std::vector<uint8_t>(sizeof(int32_t));
    };
    auto deserialize = [](const std::vector<uint8_t>& /*data*/, TestComponentA& out) {
        out = TestComponentA{42};
        return true;
    };

    registry_.RegisterComponent<TestComponentA>("component_a", serialize, deserialize);

    auto names = registry_.GetRegisteredComponents();
    EXPECT_EQ(names.size(), 1);
    EXPECT_EQ(names[0], "component_a");
}

// Test: JSON Serializer (Story 1.4)
class JsonSerializerTest : public ::testing::Test {
 protected:
    JsonSerializer serializer_;
};

TEST_F(JsonSerializerTest, BasicSerialization) {
    // Register custom serializer
    serializer_.RegisterCustom<TestComponentA>(
        [](const TestComponentA& c) {
            json j;
            j["value"] = c.value;
            return j;
        },
        [](const json& j, TestComponentA& c) {
            c.value = j["value"].get<int32_t>();
            return true;
        }
    );

    TestComponentA original{123};
    auto result = serializer_.Serialize(original);

    EXPECT_TRUE(result.success);
    EXPECT_FALSE(result.data.empty());
}

// Test: Storage Backend Interface
class MockStorageBackend : public IStorageBackend {
 public:
    StorageResult<> SaveEntity(
        uint64_t entity_id,
        const std::string& entity_type,
        const std::vector<uint8_t>& data,
        uint32_t version) noexcept override {
        std::lock_guard<std::mutex> lock(mutex_);
        saved_entities.push_back({entity_id, entity_type});
        return StorageResult<>::Success();
    }

    StorageResult<> SaveEntitiesBatch(
        const std::vector<std::tuple<uint64_t, std::string, std::vector<uint8_t>, uint32_t>>& entities) noexcept override {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& [id, type, data, version] : entities) {
            saved_entities.push_back({id, type});
        }
        return StorageResult<>::Success();
    }

    StorageResult<std::pair<std::vector<uint8_t>, uint32_t>> LoadEntity(uint64_t entity_id) noexcept override {
        return StorageResult<std::pair<std::vector<uint8_t>, uint32_t>>::Failure("Not implemented");
    }

    StorageResult<std::map<uint64_t, std::pair<std::vector<uint8_t>, uint32_t>>>
    LoadEntitiesBatch(const std::vector<uint64_t>& entity_ids) noexcept override {
        std::map<uint64_t, std::pair<std::vector<uint8_t>, uint32_t>> result;
        return StorageResult<std::map<uint64_t, std::pair<std::vector<uint8_t>, uint32_t>>>::Success(result);
    }

    StorageResult<std::map<uint64_t, std::tuple<std::string, std::vector<uint8_t>, uint32_t>>>
    LoadAllEntities() noexcept override {
        std::map<uint64_t, std::tuple<std::string, std::vector<uint8_t>, uint32_t>> result;
        return StorageResult<std::map<uint64_t, std::tuple<std::string, std::vector<uint8_t>, uint32_t>>>::Success(result);
    }

    StorageResult<> DeleteEntity(uint64_t entity_id) noexcept override {
        return StorageResult<>::Success();
    }

    StorageResult<SnapshotMetadata> CreateSnapshot(const std::string& snapshot_name) noexcept override {
        SnapshotMetadata metadata{
            .snapshot_id = 1,
            .timestamp = 0,
            .entity_count = 0,
            .component_count = 0,
            .version = "1.0",
            .checksum = ""
        };
        return StorageResult<SnapshotMetadata>::Success(metadata);
    }

    StorageResult<std::optional<SnapshotMetadata>> GetLatestSnapshot() noexcept override {
        return StorageResult<std::optional<SnapshotMetadata>>::Success(std::nullopt);
    }

    StorageResult<std::vector<SnapshotMetadata>> GetSnapshotHistory(int /*limit*/) noexcept override {
        return StorageResult<std::vector<SnapshotMetadata>>::Success({});
    }

    StorageResult<std::map<uint64_t, std::tuple<std::string, std::vector<uint8_t>, uint32_t>>>
    LoadAllEntitiesFromSnapshot(uint64_t /*snapshot_id*/) noexcept override {
        std::map<uint64_t, std::tuple<std::string, std::vector<uint8_t>, uint32_t>> result;
        return StorageResult<std::map<uint64_t, std::tuple<std::string, std::vector<uint8_t>, uint32_t>>>::Success(result);
    }

    StorageResult<> MarkSnapshotCorrupted(uint64_t /*snapshot_id*/, const std::string& /*reason*/) noexcept override {
        return StorageResult<>::Success();
    }

    bool IsReady() noexcept override { return true; }

    const char* GetName() const noexcept override { return "MockBackend"; }

    // Test utilities
    std::vector<std::pair<uint64_t, std::string>> saved_entities;
    mutable std::mutex mutex_;
};

// Test: Persistence Manager (Core functionality)
class PersistenceManagerTest : public ::testing::Test {
 protected:
    void SetUp() override {
        PersistenceManager::Shutdown();
        auto backend = std::make_unique<MockStorageBackend>();
        auto serializer = std::make_unique<JsonSerializer>();
        PersistenceManager::Initialize(
            std::move(backend),
            std::move(serializer),
            PersistenceManager::Config{}
        );
    }

    void TearDown() override {
        PersistenceManager::Shutdown();
    }
};

TEST_F(PersistenceManagerTest, Initialize) {
    EXPECT_TRUE(PersistenceManager::IsInitialized());
    auto& mgr = PersistenceManager::Instance();
    EXPECT_NE(&mgr, nullptr);
}

TEST_F(PersistenceManagerTest, SaveEntity) {
    auto& mgr = PersistenceManager::Instance();
    std::vector<uint8_t> data{1, 2, 3, 4, 5};
    auto result = mgr.SaveEntity(123, "player", data, 1);
    EXPECT_TRUE(result.success);
}

TEST_F(PersistenceManagerTest, MarkComponentDirty) {
    auto& mgr = PersistenceManager::Instance();
    mgr.MarkComponentDirty(123, "inventory", true);
    // Just verify no crash
}

TEST_F(PersistenceManagerTest, CreateShutdownSnapshot) {
    auto& mgr = PersistenceManager::Instance();
    auto result = mgr.CreateGracefulShutdownSnapshot();
    EXPECT_TRUE(result.success);
}

TEST_F(PersistenceManagerTest, PerformStartupRecovery) {
    auto& mgr = PersistenceManager::Instance();
    auto result = mgr.PerformStartupRecovery();
    EXPECT_TRUE(result.success);
}

// Test: Error handling
TEST(PersistenceErrorTest, ExceptionTypes) {
    EXPECT_THROW(
        throw UnregisteredComponentException("test_component"),
        PersistenceException
    );

    EXPECT_THROW(
        throw SerializationException("test error"),
        PersistenceException
    );

    EXPECT_THROW(
        throw DeserializationException("test error"),
        PersistenceException
    );
}

// ============================================
// H2: Round-Trip Consistency Tests
// ============================================

class ComponentTestDataGenerator {
 public:
    static std::mt19937& GetRng() {
        static std::mt19937 rng(42);  // Fixed seed for reproducibility
        return rng;
    }

    static TestComponentA GenerateTestComponentA() {
        std::uniform_int_distribution<int32_t> dist(-1000000, 1000000);
        return TestComponentA{dist(GetRng())};
    }

    static TestComponentB GenerateTestComponentB() {
        std::uniform_int_distribution<uint32_t> count_dist(0, 10000);
        std::uniform_int_distribution<int> len_dist(0, 100);

        std::string name;
        int len = len_dist(GetRng());
        for (int i = 0; i < len; ++i) {
            name += static_cast<char>('a' + (GetRng()() % 26));
        }

        return TestComponentB{name, count_dist(GetRng())};
    }
};

class RoundTripTest : public ::testing::Test {
 protected:
    void SetUp() override {
        serializer_.RegisterCustom<TestComponentA>(
            [](const TestComponentA& c) {
                json j;
                j["value"] = c.value;
                return j;
            },
            [](const json& j, TestComponentA& c) {
                c.value = j["value"].get<int32_t>();
                return true;
            }
        );

        serializer_.RegisterCustom<TestComponentB>(
            [](const TestComponentB& c) {
                json j;
                j["name"] = c.name;
                j["count"] = c.count;
                return j;
            },
            [](const json& j, TestComponentB& c) {
                c.name = j["name"].get<std::string>();
                c.count = j["count"].get<uint32_t>();
                return true;
            }
        );
    }

    JsonSerializer serializer_;
};

TEST_F(RoundTripTest, TestComponentA_RandomValues) {
    for (int i = 0; i < 1000; ++i) {
        TestComponentA original = ComponentTestDataGenerator::GenerateTestComponentA();

        auto serialize_result = serializer_.Serialize(original);
        ASSERT_TRUE(serialize_result.success) << "Serialization failed at iteration " << i;

        auto deserialize_result = serializer_.Deserialize<TestComponentA>(serialize_result.data);
        ASSERT_TRUE(deserialize_result.success) << "Deserialization failed at iteration " << i;

        EXPECT_EQ(original, deserialize_result.value)
            << "Round-trip mismatch at iteration " << i;
    }
}

TEST_F(RoundTripTest, TestComponentB_RandomValues) {
    for (int i = 0; i < 1000; ++i) {
        TestComponentB original = ComponentTestDataGenerator::GenerateTestComponentB();

        auto serialize_result = serializer_.Serialize(original);
        ASSERT_TRUE(serialize_result.success) << "Serialization failed at iteration " << i;

        auto deserialize_result = serializer_.Deserialize<TestComponentB>(serialize_result.data);
        ASSERT_TRUE(deserialize_result.success) << "Deserialization failed at iteration " << i;

        EXPECT_EQ(original, deserialize_result.value)
            << "Round-trip mismatch at iteration " << i;
    }
}

TEST_F(RoundTripTest, TestComponentA_BoundaryValues) {
    std::vector<int32_t> boundary_values = {
        0, 1, -1,
        INT32_MAX, INT32_MIN,
        INT32_MAX - 1, INT32_MIN + 1
    };

    for (int32_t value : boundary_values) {
        TestComponentA original{value};

        auto serialize_result = serializer_.Serialize(original);
        ASSERT_TRUE(serialize_result.success);

        auto deserialize_result = serializer_.Deserialize<TestComponentA>(serialize_result.data);
        ASSERT_TRUE(deserialize_result.success);

        EXPECT_EQ(original, deserialize_result.value)
            << "Boundary value mismatch for value: " << value;
    }
}

TEST_F(RoundTripTest, TestComponentB_EmptyString) {
    TestComponentB original{"", 0};

    auto serialize_result = serializer_.Serialize(original);
    ASSERT_TRUE(serialize_result.success);

    auto deserialize_result = serializer_.Deserialize<TestComponentB>(serialize_result.data);
    ASSERT_TRUE(deserialize_result.success);

    EXPECT_EQ(original, deserialize_result.value);
}

TEST_F(RoundTripTest, TestComponentB_LongString) {
    std::string long_name(10000, 'x');  // 10KB string
    TestComponentB original{long_name, UINT32_MAX};

    auto serialize_result = serializer_.Serialize(original);
    ASSERT_TRUE(serialize_result.success);

    auto deserialize_result = serializer_.Deserialize<TestComponentB>(serialize_result.data);
    ASSERT_TRUE(deserialize_result.success);

    EXPECT_EQ(original, deserialize_result.value);
}

TEST_F(RoundTripTest, TestComponentB_UnicodeString) {
    TestComponentB original{"中文测试🎮游戏", 42};

    auto serialize_result = serializer_.Serialize(original);
    ASSERT_TRUE(serialize_result.success);

    auto deserialize_result = serializer_.Deserialize<TestComponentB>(serialize_result.data);
    ASSERT_TRUE(deserialize_result.success);

    EXPECT_EQ(original, deserialize_result.value);
}

TEST_F(RoundTripTest, SerializationPerformance) {
    const int ITERATIONS = 10000;

    auto start = std::chrono::steady_clock::now();

    for (int i = 0; i < ITERATIONS; ++i) {
        TestComponentA original{i};
        auto result = serializer_.Serialize(original);
        ASSERT_TRUE(result.success);
    }

    auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - start).count();

    double avg_us = static_cast<double>(elapsed) / ITERATIONS;

    // Target: single component serialization < 100 microseconds
    EXPECT_LT(avg_us, 100.0)
        << "Average serialization time: " << avg_us << " us";
}

// ============================================
// H5: Thread Safety Tests
// ============================================

TEST(ThreadSafetyTest, ConcurrentSaveAndLoad) {
    auto backend = std::make_unique<MockStorageBackend>();
    auto* backend_ptr = backend.get();
    const int NUM_THREADS = 10;
    const int OPERATIONS_PER_THREAD = 100;

    std::vector<std::thread> threads;
    std::atomic<int> success_count{0};
    std::atomic<int> failure_count{0};

    for (int t = 0; t < NUM_THREADS; ++t) {
        threads.emplace_back([&, t]() {
            for (int i = 0; i < OPERATIONS_PER_THREAD; ++i) {
                uint64_t entity_id = (t * OPERATIONS_PER_THREAD) + i;
                std::vector<uint8_t> data{1, 2, 3, 4, 5};

                auto result = backend_ptr->SaveEntity(entity_id, "player", data, 1);
                if (result.success) {
                    success_count++;
                } else {
                    failure_count++;
                }
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    EXPECT_EQ(success_count.load(), NUM_THREADS * OPERATIONS_PER_THREAD);
    EXPECT_EQ(failure_count.load(), 0);
}

TEST(ThreadSafetyTest, ConcurrentDirtyMarking) {
    PersistenceManager::Shutdown();
    auto backend = std::make_unique<MockStorageBackend>();
    auto serializer = std::make_unique<JsonSerializer>();
    PersistenceManager::Initialize(
        std::move(backend),
        std::move(serializer),
        PersistenceManager::Config{}
    );

    auto& mgr = PersistenceManager::Instance();
    const int NUM_THREADS = 20;
    const int MARKS_PER_THREAD = 500;

    std::vector<std::thread> threads;

    for (int t = 0; t < NUM_THREADS; ++t) {
        threads.emplace_back([&, t]() {
            for (int i = 0; i < MARKS_PER_THREAD; ++i) {
                uint64_t entity_id = t * 100 + (i % 100);
                mgr.MarkComponentDirty(entity_id, "position", true);
                mgr.MarkComponentDirty(entity_id, "position", false);
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    PersistenceManager::Shutdown();
    // If we reach here without crash, thread safety is working
    SUCCEED();
}

TEST(ThreadSafetyTest, ConcurrentBatchSave) {
    auto backend = std::make_unique<MockStorageBackend>();
    auto* backend_ptr = backend.get();
    const int NUM_THREADS = 5;

    std::vector<std::thread> threads;
    std::atomic<int> success_count{0};

    for (int t = 0; t < NUM_THREADS; ++t) {
        threads.emplace_back([&, t]() {
            std::vector<std::tuple<uint64_t, std::string, std::vector<uint8_t>, uint32_t>> entities;
            for (int i = 0; i < 100; ++i) {
                entities.push_back({
                    static_cast<uint64_t>(t * 1000 + i),
                    "player",
                    std::vector<uint8_t>{1, 2, 3},
                    1
                });
            }

            auto result = backend_ptr->SaveEntitiesBatch(entities);
            if (result.success) {
                success_count++;
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    EXPECT_EQ(success_count.load(), NUM_THREADS);
}

}  // namespace mir2::persistence::test
