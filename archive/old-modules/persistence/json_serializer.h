/**
 * @file json_serializer.h
 * @brief JSON serialization implementation (Epic 1: Story 1.4)
 */

#ifndef MIR2_PERSISTENCE_JSON_SERIALIZER_H
#define MIR2_PERSISTENCE_JSON_SERIALIZER_H

#include "persistence/serializer.h"
#include "nlohmann/json.hpp"
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <typeinfo>

namespace mir2::persistence {

using json = nlohmann::json;

/**
 * @brief JSON serializer implementation using nlohmann/json
 *
 * Supports custom serialization hooks for complex types.
 * Thread-safe for multiple concurrent serializations (reads).
 */
class JsonSerializer : public ISerializer {
 public:
    using SerializeFn = std::function<json(const void*)>;
    using DeserializeFn = std::function<bool(const json&, void*)>;

    JsonSerializer() = default;
    ~JsonSerializer() override = default;

    /**
     * @brief Register a custom serializer for a component type
     * @tparam T Component type
     * @param serialize Function to serialize T to JSON
     * @param deserialize Function to deserialize JSON to T
     */
    template<typename T>
    void RegisterCustom(
        std::function<json(const T&)> serialize,
        std::function<bool(const json&, T&)> deserialize) {
        std::string key = typeid(T).name();

        // Wrap type-aware functions to type-erased versions
        serializers_[key] = [serialize](const void* ptr) {
            return serialize(*static_cast<const T*>(ptr));
        };

        deserializers_[key] = [deserialize](const json& j, void* ptr) {
            return deserialize(j, *static_cast<T*>(ptr));
        };
    }

    const char* GetName() const override { return "JsonSerializer"; }

 protected:
    SerializationResult SerializeImpl(const void* component, const char* type_name) override;
    bool DeserializeImpl(const std::vector<uint8_t>& data, void* component, const char* type_name) override;

 private:
    std::map<std::string, SerializeFn> serializers_;
    std::map<std::string, DeserializeFn> deserializers_;

    /**
     * @brief Get serializer for a type
     */
    SerializeFn GetSerializer(const std::string& type_name);

    /**
     * @brief Get deserializer for a type
     */
    DeserializeFn GetDeserializer(const std::string& type_name);
};

}  // namespace mir2::persistence

#endif  // MIR2_PERSISTENCE_JSON_SERIALIZER_H
