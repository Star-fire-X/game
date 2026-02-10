/**
 * @file serializer.h
 * @brief Serialization abstraction layer (Epic 1: Story 1.2)
 */

#ifndef MIR2_PERSISTENCE_SERIALIZER_H
#define MIR2_PERSISTENCE_SERIALIZER_H

#include <cstdint>
#include <string>
#include <typeinfo>
#include <vector>

namespace mir2::persistence {

/**
 * @brief Serialization result
 */
struct SerializationResult {
    bool success;
    std::vector<uint8_t> data;
    std::string error_message;

    static SerializationResult Success(std::vector<uint8_t> data) {
        return SerializationResult{true, std::move(data), ""};
    }

    static SerializationResult Failure(const std::string& error) {
        return SerializationResult{false, {}, error};
    }
};

/**
 * @brief Deserialization result
 */
template<typename T>
struct DeserializationResult {
    bool success;
    T value;
    std::string error_message;

    static DeserializationResult Success(T value) {
        return DeserializationResult{true, std::move(value), ""};
    }

    static DeserializationResult Failure(const std::string& error) {
        return DeserializationResult{false, T{}, error};
    }
};

/**
 * @brief Serializer abstraction interface (Story 1.2)
 *
 * Supports multiple serialization formats (JSON, Protobuf, etc.)
 * Implemented as abstract base class for polymorphism.
 */
class ISerializer {
 public:
    virtual ~ISerializer() = default;

    /**
     * @brief Serialize a generic component to bytes
     * @tparam T Component type
     * @param component Component data
     * @return Serialization result with data or error
     */
    template<typename T>
    SerializationResult Serialize(const T& component) {
        return SerializeImpl(&component, typeid(T).name());
    }

    /**
     * @brief Deserialize bytes back to component
     * @tparam T Component type
     * @param data Serialized bytes
     * @return Deserialization result with component or error
     */
    template<typename T>
    DeserializationResult<T> Deserialize(const std::vector<uint8_t>& data) {
        T component;
        bool success = DeserializeImpl(data, &component, typeid(T).name());
        if (success) {
            return DeserializationResult<T>::Success(std::move(component));
        } else {
            return DeserializationResult<T>::Failure("Deserialization failed");
        }
    }

    /**
     * @brief Get serializer name (for logging/debugging)
     */
    virtual const char* GetName() const = 0;

 protected:
    /**
     * @brief Implementation-specific serialization (type-erased)
     */
    virtual SerializationResult SerializeImpl(const void* component, const char* type_name) = 0;

    /**
     * @brief Implementation-specific deserialization (type-erased)
     */
    virtual bool DeserializeImpl(const std::vector<uint8_t>& data, void* component, const char* type_name) = 0;
};

}  // namespace mir2::persistence

#endif  // MIR2_PERSISTENCE_SERIALIZER_H
