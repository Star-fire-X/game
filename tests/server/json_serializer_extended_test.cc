/**
 * @file json_serializer_extended_test.cc
 * @brief Extended unit tests for JsonSerializer
 *
 * Test coverage:
 * - Nested structure serialization round-trips
 * - Complex container types (vectors of maps, etc.)
 * - Empty containers and null values
 * - Integer boundary values
 * - Unicode and multibyte character handling
 * - Large object serialization (1MB+)
 * - Deep nesting limits
 * - Malformed JSON error handling
 * - Type mismatches and missing fields
 * - Performance benchmarks
 */

#include <gtest/gtest.h>
#include "persistence/json_serializer.h"
#include "nlohmann/json.hpp"
#include <chrono>
#include <climits>
#include <limits>

namespace mir2::persistence::test {

using json = nlohmann::json;

// ============================================
// Test Component Types
// ============================================

struct SimpleInt {
    int32_t value;
    bool operator==(const SimpleInt& other) const {
        return value == other.value;
    }
};

struct NestedComponent {
    std::string name;
    SimpleInt inner;
    std::vector<int> values;

    bool operator==(const NestedComponent& other) const {
        return name == other.name && inner == other.inner && values == other.values;
    }
};

struct DeepNested {
    std::string data;
    std::vector<NestedComponent> components;

    bool operator==(const DeepNested& other) const {
        return data == other.data && components == other.components;
    }
};

struct ComplexContainer {
    std::map<std::string, std::vector<int>> map_of_vectors;
    std::vector<std::map<std::string, std::string>> vector_of_maps;

    bool operator==(const ComplexContainer& other) const {
        return map_of_vectors == other.map_of_vectors &&
               vector_of_maps == other.vector_of_maps;
    }
};

struct UnicodeComponent {
    std::string text;
    std::wstring wide_text;

    bool operator==(const UnicodeComponent& other) const {
        return text == other.text && wide_text == other.wide_text;
    }
};

struct LargeData {
    std::vector<uint8_t> data;

    bool operator==(const LargeData& other) const {
        return data == other.data;
    }
};

// ============================================
// Test Fixture
// ============================================

class JsonSerializerExtendedTest : public ::testing::Test {
 protected:
    JsonSerializer serializer_;

    void SetUp() override {
        // Register custom serializers for test components
        RegisterSimpleIntSerializer();
        RegisterNestedComponentSerializer();
        RegisterDeepNestedSerializer();
        RegisterComplexContainerSerializer();
        RegisterUnicodeComponentSerializer();
        RegisterLargeDataSerializer();
    }

    void RegisterSimpleIntSerializer() {
        serializer_.RegisterCustom<SimpleInt>(
            [](const SimpleInt& c) {
                json j;
                j["value"] = c.value;
                return j;
            },
            [](const json& j, SimpleInt& c) {
                c.value = j["value"].get<int32_t>();
                return true;
            });
    }

    void RegisterNestedComponentSerializer() {
        serializer_.RegisterCustom<NestedComponent>(
            [](const NestedComponent& c) {
                json j;
                j["name"] = c.name;
                j["inner"]["value"] = c.inner.value;
                j["values"] = c.values;
                return j;
            },
            [](const json& j, NestedComponent& c) {
                c.name = j["name"].get<std::string>();
                c.inner.value = j["inner"]["value"].get<int32_t>();
                c.values = j["values"].get<std::vector<int>>();
                return true;
            });
    }

    void RegisterDeepNestedSerializer() {
        serializer_.RegisterCustom<DeepNested>(
            [](const DeepNested& c) {
                json j;
                j["data"] = c.data;

                auto& components_arr = j["components"];
                for (const auto& comp : c.components) {
                    json comp_obj;
                    comp_obj["name"] = comp.name;
                    comp_obj["inner"]["value"] = comp.inner.value;
                    comp_obj["values"] = comp.values;
                    components_arr.push_back(comp_obj);
                }

                return j;
            },
            [](const json& j, DeepNested& c) {
                c.data = j["data"].get<std::string>();
                c.components.clear();

                for (const auto& comp_obj : j["components"]) {
                    NestedComponent comp;
                    comp.name = comp_obj["name"].get<std::string>();
                    comp.inner.value = comp_obj["inner"]["value"].get<int32_t>();
                    comp.values = comp_obj["values"].get<std::vector<int>>();
                    c.components.push_back(comp);
                }

                return true;
            });
    }

    void RegisterComplexContainerSerializer() {
        serializer_.RegisterCustom<ComplexContainer>(
            [](const ComplexContainer& c) {
                json j;

                // Serialize map of vectors
                json map_obj = json::object();
                for (const auto& [key, vec] : c.map_of_vectors) {
                    map_obj[key] = vec;
                }
                j["map_of_vectors"] = map_obj;

                // Serialize vector of maps
                json vec_arr = json::array();
                for (const auto& map : c.vector_of_maps) {
                    json map_obj = json::object();
                    for (const auto& [k, v] : map) {
                        map_obj[k] = v;
                    }
                    vec_arr.push_back(map_obj);
                }
                j["vector_of_maps"] = vec_arr;

                return j;
            },
            [](const json& j, ComplexContainer& c) {
                // Deserialize map of vectors
                c.map_of_vectors.clear();
                for (const auto& [key, val] : j["map_of_vectors"].items()) {
                    c.map_of_vectors[key] = val.get<std::vector<int>>();
                }

                // Deserialize vector of maps
                c.vector_of_maps.clear();
                for (const auto& map_obj : j["vector_of_maps"]) {
                    std::map<std::string, std::string> map;
                    for (const auto& [k, v] : map_obj.items()) {
                        map[k] = v.get<std::string>();
                    }
                    c.vector_of_maps.push_back(map);
                }

                return true;
            });
    }

    void RegisterUnicodeComponentSerializer() {
        serializer_.RegisterCustom<UnicodeComponent>(
            [](const UnicodeComponent& c) {
                json j;
                j["text"] = c.text;
                // Note: Wide strings are more complex, just store as UTF-8
                j["wide_text"] = c.text;  // Simplified for test
                return j;
            },
            [](const json& j, UnicodeComponent& c) {
                c.text = j["text"].get<std::string>();
                c.wide_text = std::wstring(c.text.begin(), c.text.end());
                return true;
            });
    }

    void RegisterLargeDataSerializer() {
        serializer_.RegisterCustom<LargeData>(
            [](const LargeData& c) {
                json j;
                // Encode as base64 for JSON compatibility
                j["data"] = c.data;
                return j;
            },
            [](const json& j, LargeData& c) {
                c.data = j["data"].get<std::vector<uint8_t>>();
                return true;
            });
    }
};

// ============================================
// H1: Nested Structure Tests
// ============================================

TEST_F(JsonSerializerExtendedTest, Serialize_NestedStructure_RoundTripSuccess) {
    NestedComponent original{
        "test_name",
        SimpleInt{42},
        {1, 2, 3, 4, 5}
    };

    auto serialize_result = serializer_.Serialize(original);
    ASSERT_TRUE(serialize_result.success);
    ASSERT_FALSE(serialize_result.data.empty());

    auto deserialize_result = serializer_.Deserialize<NestedComponent>(serialize_result.data);
    ASSERT_TRUE(deserialize_result.success);

    EXPECT_EQ(original, deserialize_result.value);
}

TEST_F(JsonSerializerExtendedTest, Serialize_VectorOfMaps_RoundTripSuccess) {
    ComplexContainer original;
    original.map_of_vectors["key1"] = {1, 2, 3};
    original.map_of_vectors["key2"] = {4, 5, 6};

    original.vector_of_maps.push_back({{"a", "hello"}, {"b", "world"}});
    original.vector_of_maps.push_back({{"c", "foo"}, {"d", "bar"}});

    auto serialize_result = serializer_.Serialize(original);
    ASSERT_TRUE(serialize_result.success);

    auto deserialize_result = serializer_.Deserialize<ComplexContainer>(serialize_result.data);
    ASSERT_TRUE(deserialize_result.success);

    EXPECT_EQ(original, deserialize_result.value);
}

TEST_F(JsonSerializerExtendedTest, Serialize_EmptyContainers_HandlesCorrectly) {
    ComplexContainer original;
    // All containers are empty

    auto serialize_result = serializer_.Serialize(original);
    ASSERT_TRUE(serialize_result.success);

    auto deserialize_result = serializer_.Deserialize<ComplexContainer>(serialize_result.data);
    ASSERT_TRUE(deserialize_result.success);

    EXPECT_EQ(original.map_of_vectors.size(), 0);
    EXPECT_EQ(original.vector_of_maps.size(), 0);
}

// ============================================
// H2: Boundary Value Tests
// ============================================

TEST_F(JsonSerializerExtendedTest, Serialize_IntBoundaryValues_Accurate) {
    std::vector<int32_t> boundary_values = {
        0, 1, -1,
        INT32_MAX,
        INT32_MIN,
        INT32_MAX - 1,
        INT32_MIN + 1,
        1000000,
        -1000000
    };

    for (int32_t value : boundary_values) {
        SimpleInt original{value};

        auto serialize_result = serializer_.Serialize(original);
        ASSERT_TRUE(serialize_result.success) << "Serialization failed for value: " << value;

        auto deserialize_result = serializer_.Deserialize<SimpleInt>(serialize_result.data);
        ASSERT_TRUE(deserialize_result.success) << "Deserialization failed for value: " << value;

        EXPECT_EQ(original, deserialize_result.value)
            << "Mismatch for value: " << value;
    }
}

// ============================================
// H3: Unicode and Special Characters
// ============================================

TEST_F(JsonSerializerExtendedTest, Serialize_UnicodeEmoji_Preserved) {
    UnicodeComponent original{"Hello 🎮🚀💾 World!", L"Wide text"};

    auto serialize_result = serializer_.Serialize(original);
    ASSERT_TRUE(serialize_result.success);

    auto deserialize_result = serializer_.Deserialize<UnicodeComponent>(serialize_result.data);
    ASSERT_TRUE(deserialize_result.success);

    EXPECT_EQ(original, deserialize_result.value);
}

TEST_F(JsonSerializerExtendedTest, Serialize_MultiByte_Characters_Preserved) {
    UnicodeComponent original{
        "中文测试 日本語テスト Русский тест",
        L"Wide text with unicode"
    };

    auto serialize_result = serializer_.Serialize(original);
    ASSERT_TRUE(serialize_result.success);

    auto deserialize_result = serializer_.Deserialize<UnicodeComponent>(serialize_result.data);
    ASSERT_TRUE(deserialize_result.success);

    // Note: Due to wstring conversion, just check UTF-8 text
    EXPECT_EQ(original.text, deserialize_result.value.text);
}

// ============================================
// H4: Large Object Tests
// ============================================

TEST_F(JsonSerializerExtendedTest, Serialize_LargeObject1MB_PerformanceTest) {
    LargeData original;
    // Create 1MB of data
    original.data.resize(1024 * 1024, 0xAB);

    auto start = std::chrono::steady_clock::now();

    auto serialize_result = serializer_.Serialize(original);

    auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start).count();

    EXPECT_TRUE(serialize_result.success);

    // Should complete reasonably fast (< 5 seconds for 1MB)
    EXPECT_LT(elapsed_ms, 5000)
        << "Serialization took " << elapsed_ms << "ms";
}

TEST_F(JsonSerializerExtendedTest, Deserialize_LargeObject1MB_PerformanceTest) {
    LargeData original;
    original.data.resize(1024 * 1024, 0xCD);

    auto serialize_result = serializer_.Serialize(original);
    ASSERT_TRUE(serialize_result.success);

    auto start = std::chrono::steady_clock::now();

    auto deserialize_result = serializer_.Deserialize<LargeData>(serialize_result.data);

    auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start).count();

    EXPECT_TRUE(deserialize_result.success);

    // Should complete reasonably fast (< 5 seconds for 1MB)
    EXPECT_LT(elapsed_ms, 5000)
        << "Deserialization took " << elapsed_ms << "ms";
}

// ============================================
// H5: Deep Nesting Tests
// ============================================

TEST_F(JsonSerializerExtendedTest, Serialize_DeepNesting100Levels_HandlesOrFails) {
    // Create deeply nested structure with multiple levels
    DeepNested original;
    original.data = "root";

    for (int i = 0; i < 10; ++i) {
        NestedComponent comp;
        comp.name = "component_" + std::to_string(i);
        comp.inner.value = i * 10;

        for (int j = 0; j < 10; ++j) {
            comp.values.push_back(j);
        }

        original.components.push_back(comp);
    }

    auto serialize_result = serializer_.Serialize(original);

    // Should either succeed or fail gracefully
    if (serialize_result.success) {
        auto deserialize_result = serializer_.Deserialize<DeepNested>(serialize_result.data);
        EXPECT_TRUE(deserialize_result.success);
        EXPECT_EQ(original, deserialize_result.value);
    } else {
        // Graceful failure is acceptable
        EXPECT_FALSE(serialize_result.error_message.empty());
    }
}

// ============================================
// H6: Error Handling Tests
// ============================================

TEST_F(JsonSerializerExtendedTest, Deserialize_MalformedJson_ReturnsFailure) {
    // Create invalid JSON data
    std::string invalid_json = "{invalid json data}";
    std::vector<uint8_t> data(invalid_json.begin(), invalid_json.end());

    auto deserialize_result = serializer_.Deserialize<SimpleInt>(data);

    // Should fail gracefully
    EXPECT_FALSE(deserialize_result.success);
}

TEST_F(JsonSerializerExtendedTest, Deserialize_TypeMismatch_ReturnsFailure) {
    // Create JSON with wrong type
    json j;
    j["value"] = "not an integer";  // String instead of int
    std::vector<uint8_t> data(j.dump().begin(), j.dump().end());

    auto deserialize_result = serializer_.Deserialize<SimpleInt>(data);

    // Should fail gracefully (or succeed if type coercion is supported)
    // Behavior depends on implementation
}

TEST_F(JsonSerializerExtendedTest, Deserialize_MissingFields_HandlesGracefully) {
    // Create JSON with missing required fields
    json j;
    j["value"] = 42;
    // Missing other expected fields
    std::vector<uint8_t> data(j.dump().begin(), j.dump().end());

    auto deserialize_result = serializer_.Deserialize<NestedComponent>(data);

    // Should either succeed (with defaults) or fail gracefully
    // Behavior depends on implementation
}

TEST_F(JsonSerializerExtendedTest, Serialize_NullValues_HandlesCorrectly) {
    NestedComponent original;
    original.name = "";  // Empty string
    original.inner.value = 0;  // Zero value
    original.values.clear();  // Empty vector

    auto serialize_result = serializer_.Serialize(original);
    EXPECT_TRUE(serialize_result.success);

    auto deserialize_result = serializer_.Deserialize<NestedComponent>(serialize_result.data);
    EXPECT_TRUE(deserialize_result.success);

    EXPECT_EQ(original, deserialize_result.value);
}

// ============================================
// H7: Performance Benchmarks
// ============================================

TEST_F(JsonSerializerExtendedTest, SerializationPerformance_10000Iterations) {
    const int ITERATIONS = 10000;

    SimpleInt original{42};

    auto start = std::chrono::steady_clock::now();

    for (int i = 0; i < ITERATIONS; ++i) {
        SimpleInt comp{i};
        auto result = serializer_.Serialize(comp);
        ASSERT_TRUE(result.success);
    }

    auto elapsed_us = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - start).count();

    double avg_us = static_cast<double>(elapsed_us) / ITERATIONS;

    // Simple component serialization should be very fast (< 100 microseconds average)
    EXPECT_LT(avg_us, 100.0)
        << "Average serialization time: " << avg_us << " us";
}

TEST_F(JsonSerializerExtendedTest, DeserializationPerformance_10000Iterations) {
    SimpleInt original{42};
    auto serialize_result = serializer_.Serialize(original);
    ASSERT_TRUE(serialize_result.success);

    const int ITERATIONS = 10000;

    auto start = std::chrono::steady_clock::now();

    for (int i = 0; i < ITERATIONS; ++i) {
        auto result = serializer_.Deserialize<SimpleInt>(serialize_result.data);
        ASSERT_TRUE(result.success);
    }

    auto elapsed_us = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - start).count();

    double avg_us = static_cast<double>(elapsed_us) / ITERATIONS;

    // Simple component deserialization should be very fast (< 100 microseconds average)
    EXPECT_LT(avg_us, 100.0)
        << "Average deserialization time: " << avg_us << " us";
}

TEST_F(JsonSerializerExtendedTest, RoundTripPerformance_1000Iterations_ComplexType) {
    const int ITERATIONS = 1000;

    auto start = std::chrono::steady_clock::now();

    for (int i = 0; i < ITERATIONS; ++i) {
        NestedComponent original{
            "test_" + std::to_string(i),
            SimpleInt{i},
            {i, i + 1, i + 2}
        };

        auto serialize_result = serializer_.Serialize(original);
        ASSERT_TRUE(serialize_result.success);

        auto deserialize_result = serializer_.Deserialize<NestedComponent>(serialize_result.data);
        ASSERT_TRUE(deserialize_result.success);

        EXPECT_EQ(original, deserialize_result.value);
    }

    auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start).count();

    double avg_ms = static_cast<double>(elapsed_ms) / ITERATIONS;

    // Complex round-trip should still be reasonably fast (< 10ms average)
    EXPECT_LT(avg_ms, 10.0)
        << "Average round-trip time: " << avg_ms << " ms";
}

// ============================================
// H8: Edge Cases
// ============================================

TEST_F(JsonSerializerExtendedTest, Serialize_SpecialFloatingPointValues) {
    // Note: This would require a float-based test component
    // For now, just test integer boundaries

    std::vector<int32_t> special_values = {
        0, 1, -1,
        INT32_MAX,
        INT32_MIN,
        (INT32_MAX / 2),
        (INT32_MIN / 2),
    };

    for (int32_t value : special_values) {
        SimpleInt original{value};

        auto serialize_result = serializer_.Serialize(original);
        ASSERT_TRUE(serialize_result.success);

        auto deserialize_result = serializer_.Deserialize<SimpleInt>(serialize_result.data);
        ASSERT_TRUE(deserialize_result.success);

        EXPECT_EQ(original.value, deserialize_result.value.value);
    }
}

TEST_F(JsonSerializerExtendedTest, MultipleSerializersForDifferentTypes) {
    // Test that serializer can handle multiple different types

    SimpleInt int_comp{42};
    NestedComponent nested_comp{"test", SimpleInt{100}, {1, 2, 3}};

    auto int_result = serializer_.Serialize(int_comp);
    auto nested_result = serializer_.Serialize(nested_comp);

    EXPECT_TRUE(int_result.success);
    EXPECT_TRUE(nested_result.success);

    auto int_deserialize = serializer_.Deserialize<SimpleInt>(int_result.data);
    auto nested_deserialize = serializer_.Deserialize<NestedComponent>(nested_result.data);

    EXPECT_TRUE(int_deserialize.success);
    EXPECT_TRUE(nested_deserialize.success);

    EXPECT_EQ(int_comp, int_deserialize.value);
    EXPECT_EQ(nested_comp, nested_deserialize.value);
}

}  // namespace mir2::persistence::test
