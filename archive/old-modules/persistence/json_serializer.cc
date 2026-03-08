/**
 * @file json_serializer.cc
 * @brief JSON serializer implementation
 */

#include "persistence/json_serializer.h"
#include "persistence/persistence_error.h"
#include <sstream>

namespace mir2::persistence {

SerializationResult JsonSerializer::SerializeImpl(const void* component, const char* type_name) {
    try {
        if (!type_name) {
            return SerializationResult::Failure("Type name not provided");
        }
        auto serializer = GetSerializer(type_name);

        if (!serializer) {
            return SerializationResult::Failure(
                std::string("No serializer registered for type: ") + type_name);
        }

        json j = serializer(component);
        std::string json_str = j.dump();
        std::vector<uint8_t> data(json_str.begin(), json_str.end());
        return SerializationResult::Success(std::move(data));
    } catch (const std::exception& e) {
        return SerializationResult::Failure(
            std::string("JSON serialization error: ") + e.what());
    }
}

bool JsonSerializer::DeserializeImpl(const std::vector<uint8_t>& data, void* component,
                                    const char* type_name) {
    try {
        if (data.empty()) {
            return false;
        }
        if (!type_name) {
            return false;
        }

        std::string json_str(data.begin(), data.end());
        json j = json::parse(json_str);

        auto deserializer = GetDeserializer(type_name);

        if (!deserializer) {
            return false;
        }

        return deserializer(j, component);
    } catch (const std::exception& e) {
        return false;
    }
}

JsonSerializer::SerializeFn JsonSerializer::GetSerializer(const std::string& type_name) {
    auto it = serializers_.find(type_name);
    if (it != serializers_.end()) {
        return it->second;
    }
    return nullptr;
}

JsonSerializer::DeserializeFn JsonSerializer::GetDeserializer(const std::string& type_name) {
    auto it = deserializers_.find(type_name);
    if (it != deserializers_.end()) {
        return it->second;
    }
    return nullptr;
}

}  // namespace mir2::persistence
