/**
 * @file component_registry_extended_test.cc
 * @brief Extended unit tests for ComponentRegistry
 *
 * Test coverage:
 * - Duplicate type registration and overwriting behavior
 * - Concurrent registration thread safety
 * - Extremely long component names
 * - Special characters in component names
 * - Validation failure scenarios
 * - Performance tests with large registries
 * - Concurrent query thread safety
 * - Registry clearing
 * - Entry lookup by name and type
 */

#include <gtest/gtest.h>
#include "persistence/component_registry.h"
#include "persistence/persistence_error.h"
#include <thread>
#include <atomic>
#include <chrono>
#include <random>

namespace mir2::persistence::test {

// ============================================
// Test Component Types
// ============================================

struct SimpleComponent {
    int value;
};

struct ComplexComponent {
    std::string name;
    uint64_t id;
    std::vector<int> data;
};

struct LargeComponent {
    std::array<uint8_t, 10000> data;
};

// ============================================
// Test Fixture
// ============================================

class ComponentRegistryExtendedTest : public ::testing::Test {
 protected:
    void SetUp() override {
        registry_.Clear();
    }

    void TearDown() override {
        registry_.Clear();
    }

    ComponentRegistry registry_;
};

// ============================================
// H1: Registration Edge Cases
// ============================================

TEST_F(ComponentRegistryExtendedTest, RegisterComponent_DuplicateType_OverwritesPrevious) {
    // First registration
    auto serialize1 = [](const void* ptr) {
        auto* comp = static_cast<const SimpleComponent*>(ptr);
        std::vector<uint8_t> data(sizeof(int));
        std::memcpy(data.data(), &comp->value, sizeof(int));
        return data;
    };

    auto deserialize1 = [](const std::vector<uint8_t>& data, void* ptr) {
        auto* comp = static_cast<SimpleComponent*>(ptr);
        if (data.size() >= sizeof(int)) {
            std::memcpy(&comp->value, data.data(), sizeof(int));
        }
        return true;
    };

    EXPECT_TRUE(registry_.RegisterComponent<SimpleComponent>(
        "simple_v1", serialize1, deserialize1));
    EXPECT_EQ(registry_.GetComponentCount(), 1);

    // Second registration (should overwrite)
    auto serialize2 = [](const void* ptr) {
        auto* comp = static_cast<const SimpleComponent*>(ptr);
        std::vector<uint8_t> data(sizeof(int) + 1);  // Different size
        std::memcpy(data.data(), &comp->value, sizeof(int));
        data[sizeof(int)] = 0xFF;  // Extra byte
        return data;
    };

    auto deserialize2 = [](const std::vector<uint8_t>& data, void* ptr) {
        auto* comp = static_cast<SimpleComponent*>(ptr);
        if (data.size() >= sizeof(int)) {
            std::memcpy(&comp->value, data.data(), sizeof(int));
        }
        return true;
    };

    EXPECT_TRUE(registry_.RegisterComponent<SimpleComponent>(
        "simple_v2", serialize2, deserialize2));

    // Count should still be 1 (overwritten)
    EXPECT_EQ(registry_.GetComponentCount(), 1);

    // Entry name should be updated
    auto entry = registry_.GetEntry(std::type_index(typeid(SimpleComponent)));
    ASSERT_NE(entry, nullptr);
    EXPECT_EQ(entry->type_name, "simple_v2");
}

TEST_F(ComponentRegistryExtendedTest, RegisterComponent_ConcurrentRegistration_ThreadSafe) {
    const int NUM_THREADS = 20;
    std::atomic<int> success_count{0};
    std::vector<std::thread> threads;

    for (int t = 0; t < NUM_THREADS; ++t) {
        threads.emplace_back([&, t]() {
            // Each thread tries to register a different component type
            // Using thread index to create unique types

            auto serialize = [](const void* ptr) {
                return std::vector<uint8_t>{1, 2, 3};
            };

            auto deserialize = [](const std::vector<uint8_t>& data, void* ptr) {
                return true;
            };

            try {
                // All threads register the same type (should be thread-safe)
                bool result = registry_.RegisterComponent<SimpleComponent>(
                    "simple_thread_" + std::to_string(t),
                    serialize,
                    deserialize);

                if (result) {
                    success_count++;
                }
            } catch (...) {
                // Should not throw
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    EXPECT_EQ(success_count.load(), NUM_THREADS);
    // All registrations should succeed (last one wins due to overwrite)
    EXPECT_EQ(registry_.GetComponentCount(), 1);
}

TEST_F(ComponentRegistryExtendedTest, RegisterComponent_ExtremelyLongName_HandlesCorrectly) {
    // Create a very long component name (10KB)
    std::string long_name(10000, 'x');

    auto serialize = [](const void* ptr) {
        return std::vector<uint8_t>{1, 2, 3};
    };

    auto deserialize = [](const std::vector<uint8_t>& data, void* ptr) {
        return true;
    };

    EXPECT_TRUE(registry_.RegisterComponent<SimpleComponent>(
        long_name, serialize, deserialize));

    auto entry = registry_.GetEntryByName(long_name);
    ASSERT_NE(entry, nullptr);
    EXPECT_EQ(entry->type_name, long_name);
}

TEST_F(ComponentRegistryExtendedTest, RegisterComponent_SpecialCharacters_HandlesCorrectly) {
    // Test various special characters in component names
    std::vector<std::string> special_names = {
        "component-with-dashes",
        "component_with_underscores",
        "component.with.dots",
        "component:with:colons",
        "component/with/slashes",
        "component with spaces",
        "component\twith\ttabs",
        "组件名称",  // Chinese characters
        "компонент",  // Cyrillic
        "🎮🚀💾"  // Emojis
    };

    auto serialize = [](const void* ptr) {
        return std::vector<uint8_t>{1, 2, 3};
    };

    auto deserialize = [](const std::vector<uint8_t>& data, void* ptr) {
        return true;
    };

    for (const auto& name : special_names) {
        EXPECT_TRUE(registry_.RegisterComponent<ComplexComponent>(
            name, serialize, deserialize))
            << "Failed to register component with name: " << name;

        auto entry = registry_.GetEntryByName(name);
        ASSERT_NE(entry, nullptr) << "Failed to find component: " << name;
        EXPECT_EQ(entry->type_name, name);
    }
}

TEST_F(ComponentRegistryExtendedTest, RegisterComponent_ValidationFailure_ThrowsException) {
    auto serialize = [](const void* ptr) {
        return std::vector<uint8_t>{1, 2, 3};
    };

    auto deserialize = [](const std::vector<uint8_t>& data, void* ptr) {
        return true;
    };

    // Validator that always fails
    auto validate = [](const void* ptr) {
        return false;
    };

    EXPECT_THROW({
        registry_.RegisterComponent<SimpleComponent>(
            "invalid_component",
            serialize,
            deserialize,
            validate);
    }, RoundTripValidationException);

    // Registration should not succeed
    EXPECT_FALSE(registry_.IsRegistered(std::type_index(typeid(SimpleComponent))));
}

// ============================================
// H2: Performance Tests
// ============================================

TEST_F(ComponentRegistryExtendedTest, GetEntry_After10000Registrations_PerformanceTest) {
    // Register many component types by varying the name
    // (Note: In practice, we can't create 10000 unique types, so we'll reuse types
    // with different names, which will overwrite)

    auto serialize = [](const void* ptr) {
        return std::vector<uint8_t>{1, 2, 3};
    };

    auto deserialize = [](const std::vector<uint8_t>& data, void* ptr) {
        return true;
    };

    const int NUM_REGISTRATIONS = 10000;

    for (int i = 0; i < NUM_REGISTRATIONS; ++i) {
        registry_.RegisterComponent<SimpleComponent>(
            "component_" + std::to_string(i),
            serialize,
            deserialize);
    }

    // Measure lookup performance
    auto start = std::chrono::steady_clock::now();

    auto entry = registry_.GetEntry(std::type_index(typeid(SimpleComponent)));

    auto elapsed_us = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - start).count();

    ASSERT_NE(entry, nullptr);

    // Lookup should be very fast (< 100 microseconds)
    EXPECT_LT(elapsed_us, 100)
        << "Lookup took " << elapsed_us << " microseconds";
}

TEST_F(ComponentRegistryExtendedTest, IsRegistered_ConcurrentQueries_ThreadSafe) {
    // Register a component
    auto serialize = [](const void* ptr) {
        return std::vector<uint8_t>{1, 2, 3};
    };

    auto deserialize = [](const std::vector<uint8_t>& data, void* ptr) {
        return true;
    };

    registry_.RegisterComponent<SimpleComponent>(
        "simple", serialize, deserialize);

    const int NUM_THREADS = 50;
    const int QUERIES_PER_THREAD = 10000;
    std::atomic<int> success_count{0};

    std::vector<std::thread> threads;

    for (int t = 0; t < NUM_THREADS; ++t) {
        threads.emplace_back([&]() {
            for (int i = 0; i < QUERIES_PER_THREAD; ++i) {
                if (registry_.IsRegistered(std::type_index(typeid(SimpleComponent)))) {
                    success_count++;
                }
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    EXPECT_EQ(success_count.load(), NUM_THREADS * QUERIES_PER_THREAD);
}

// ============================================
// H3: Registry Management
// ============================================

TEST_F(ComponentRegistryExtendedTest, Clear_RemovesAllRegistrations) {
    auto serialize = [](const void* ptr) {
        return std::vector<uint8_t>{1, 2, 3};
    };

    auto deserialize = [](const std::vector<uint8_t>& data, void* ptr) {
        return true;
    };

    // Register multiple components
    registry_.RegisterComponent<SimpleComponent>(
        "simple", serialize, deserialize);
    registry_.RegisterComponent<ComplexComponent>(
        "complex", serialize, deserialize);
    registry_.RegisterComponent<LargeComponent>(
        "large", serialize, deserialize);

    EXPECT_EQ(registry_.GetComponentCount(), 3);

    // Clear registry
    registry_.Clear();

    EXPECT_EQ(registry_.GetComponentCount(), 0);
    EXPECT_FALSE(registry_.IsRegistered(std::type_index(typeid(SimpleComponent))));
    EXPECT_FALSE(registry_.IsRegistered(std::type_index(typeid(ComplexComponent))));
    EXPECT_FALSE(registry_.IsRegistered(std::type_index(typeid(LargeComponent))));
}

TEST_F(ComponentRegistryExtendedTest, GetRegisteredComponents_ReturnsAllNames) {
    auto serialize = [](const void* ptr) {
        return std::vector<uint8_t>{1, 2, 3};
    };

    auto deserialize = [](const std::vector<uint8_t>& data, void* ptr) {
        return true;
    };

    // Register components with unique names
    std::vector<std::string> expected_names = {
        "component_a",
        "component_b",
        "component_c"
    };

    registry_.RegisterComponent<SimpleComponent>(
        expected_names[0], serialize, deserialize);
    registry_.RegisterComponent<ComplexComponent>(
        expected_names[1], serialize, deserialize);
    registry_.RegisterComponent<LargeComponent>(
        expected_names[2], serialize, deserialize);

    auto registered_names = registry_.GetRegisteredComponents();

    EXPECT_EQ(registered_names.size(), 3);

    // Check all expected names are present
    for (const auto& name : expected_names) {
        EXPECT_NE(std::find(registered_names.begin(), registered_names.end(), name),
                  registered_names.end())
            << "Missing component name: " << name;
    }
}

TEST_F(ComponentRegistryExtendedTest, GetEntryByName_NonExistent_ReturnsNull) {
    auto entry = registry_.GetEntryByName("non_existent_component");
    EXPECT_EQ(entry, nullptr);
}

// ============================================
// H4: Serialization Callback Verification
// ============================================

TEST_F(ComponentRegistryExtendedTest, SerializeCallback_ExecutedCorrectly) {
    std::atomic<bool> serialize_called{false};

    auto serialize = [&](const void* ptr) {
        serialize_called = true;
        auto* comp = static_cast<const SimpleComponent*>(ptr);
        std::vector<uint8_t> data(sizeof(int));
        std::memcpy(data.data(), &comp->value, sizeof(int));
        return data;
    };

    auto deserialize = [](const std::vector<uint8_t>& data, void* ptr) {
        return true;
    };

    registry_.RegisterComponent<SimpleComponent>(
        "simple", serialize, deserialize);

    auto entry = registry_.GetEntry(std::type_index(typeid(SimpleComponent)));
    ASSERT_NE(entry, nullptr);

    // Execute serialize callback
    SimpleComponent comp{42};
    auto data = entry->serialize(&comp);

    EXPECT_TRUE(serialize_called.load());
    EXPECT_EQ(data.size(), sizeof(int));
}

TEST_F(ComponentRegistryExtendedTest, DeserializeCallback_ExecutedCorrectly) {
    std::atomic<bool> deserialize_called{false};

    auto serialize = [](const void* ptr) {
        auto* comp = static_cast<const SimpleComponent*>(ptr);
        std::vector<uint8_t> data(sizeof(int));
        std::memcpy(data.data(), &comp->value, sizeof(int));
        return data;
    };

    auto deserialize = [&](const std::vector<uint8_t>& data, void* ptr) {
        deserialize_called = true;
        auto* comp = static_cast<SimpleComponent*>(ptr);
        if (data.size() >= sizeof(int)) {
            std::memcpy(&comp->value, data.data(), sizeof(int));
        }
        return true;
    };

    registry_.RegisterComponent<SimpleComponent>(
        "simple", serialize, deserialize);

    auto entry = registry_.GetEntry(std::type_index(typeid(SimpleComponent)));
    ASSERT_NE(entry, nullptr);

    // Execute deserialize callback
    std::vector<uint8_t> data{1, 0, 0, 0};  // int value 1
    SimpleComponent comp{0};
    bool result = entry->deserialize(data, &comp);

    EXPECT_TRUE(result);
    EXPECT_TRUE(deserialize_called.load());
    EXPECT_EQ(comp.value, 1);
}

TEST_F(ComponentRegistryExtendedTest, ValidateCallback_ExecutedWhenProvided) {
    std::atomic<int> validate_call_count{0};

    auto serialize = [](const void* ptr) {
        return std::vector<uint8_t>{1, 2, 3};
    };

    auto deserialize = [](const std::vector<uint8_t>& data, void* ptr) {
        return true;
    };

    auto validate = [&](const void* ptr) {
        validate_call_count++;
        return true;  // Always succeed
    };

    registry_.RegisterComponent<SimpleComponent>(
        "simple", serialize, deserialize, validate);

    // Validate is called during registration
    EXPECT_GT(validate_call_count.load(), 0);
}

// ============================================
// H5: Edge Cases
// ============================================

TEST_F(ComponentRegistryExtendedTest, RegisterComponent_EmptyName_HandlesCorrectly) {
    auto serialize = [](const void* ptr) {
        return std::vector<uint8_t>{1, 2, 3};
    };

    auto deserialize = [](const std::vector<uint8_t>& data, void* ptr) {
        return true;
    };

    EXPECT_TRUE(registry_.RegisterComponent<SimpleComponent>(
        "", serialize, deserialize));

    auto entry = registry_.GetEntryByName("");
    ASSERT_NE(entry, nullptr);
    EXPECT_EQ(entry->type_name, "");
}

TEST_F(ComponentRegistryExtendedTest, GetComponentCount_MatchesRegistrations) {
    EXPECT_EQ(registry_.GetComponentCount(), 0);

    auto serialize = [](const void* ptr) {
        return std::vector<uint8_t>{1, 2, 3};
    };

    auto deserialize = [](const std::vector<uint8_t>& data, void* ptr) {
        return true;
    };

    registry_.RegisterComponent<SimpleComponent>(
        "simple", serialize, deserialize);
    EXPECT_EQ(registry_.GetComponentCount(), 1);

    registry_.RegisterComponent<ComplexComponent>(
        "complex", serialize, deserialize);
    EXPECT_EQ(registry_.GetComponentCount(), 2);

    registry_.RegisterComponent<LargeComponent>(
        "large", serialize, deserialize);
    EXPECT_EQ(registry_.GetComponentCount(), 3);

    registry_.Clear();
    EXPECT_EQ(registry_.GetComponentCount(), 0);
}

TEST_F(ComponentRegistryExtendedTest, ConcurrentClearAndRegister_ThreadSafe) {
    const int NUM_ITERATIONS = 100;
    std::vector<std::thread> threads;

    auto serialize = [](const void* ptr) {
        return std::vector<uint8_t>{1, 2, 3};
    };

    auto deserialize = [](const std::vector<uint8_t>& data, void* ptr) {
        return true;
    };

    // Thread 1: Continuously registers
    threads.emplace_back([&]() {
        for (int i = 0; i < NUM_ITERATIONS; ++i) {
            registry_.RegisterComponent<SimpleComponent>(
                "simple_" + std::to_string(i), serialize, deserialize);
        }
    });

    // Thread 2: Continuously clears
    threads.emplace_back([&]() {
        for (int i = 0; i < NUM_ITERATIONS; ++i) {
            registry_.Clear();
            std::this_thread::yield();
        }
    });

    // Thread 3: Continuously queries
    threads.emplace_back([&]() {
        for (int i = 0; i < NUM_ITERATIONS; ++i) {
            registry_.GetComponentCount();
            registry_.GetRegisteredComponents();
            std::this_thread::yield();
        }
    });

    for (auto& t : threads) {
        t.join();
    }

    // Should complete without crash or deadlock
    SUCCEED();
}

}  // namespace mir2::persistence::test
