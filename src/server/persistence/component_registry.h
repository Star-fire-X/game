/**
 * @file component_registry.h
 * @brief Component serialization registry (Epic 1: Story 1.1)
 */

#ifndef MIR2_PERSISTENCE_COMPONENT_REGISTRY_H
#define MIR2_PERSISTENCE_COMPONENT_REGISTRY_H

#include "persistence/serializer.h"
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <typeindex>

namespace mir2::persistence {

/**
 * @brief Component serialization registry
 *
 * Manages serialize/deserialize functions for all component types.
 * Supports versioned serializers for schema evolution.
 * Thread-safe for registration and lookup.
 */
class ComponentRegistry {
 public:
    using SerializeCallback = std::function<std::vector<uint8_t>(const void*)>;
    using DeserializeCallback = std::function<bool(const std::vector<uint8_t>&, void*)>;
    using ValidateCallback = std::function<bool(const void*)>;

    /**
     * @brief Serializer entry with version and validation
     */
    struct SerializerEntry {
        SerializeCallback serialize;
        DeserializeCallback deserialize;
        ValidateCallback validate;  // Optional round-trip validation
        uint32_t version = 1;
        std::string type_name;
    };

    ComponentRegistry() = default;
    ~ComponentRegistry() = default;

    /**
     * @brief Register a component type serializer (Story 1.1)
     * @tparam T Component type
     * @param name Component name for logging
     * @param serialize Function to serialize T to bytes
     * @param deserialize Function to deserialize bytes to T
     * @param validate Optional round-trip validation function
     * @return true if registration successful
     * @throws UnregisteredComponentException if validation fails
     */
    template<typename T>
    bool RegisterComponent(
        const std::string& name,
        SerializeCallback serialize,
        DeserializeCallback deserialize,
        ValidateCallback validate = nullptr) {
        std::lock_guard<std::mutex> lock(mutex_);

        // Validate round-trip consistency if validator provided
        if (validate) {
            // Create a dummy instance and validate
            T dummy{};
            if (!validate(&dummy)) {
                throw RoundTripValidationException(name);
            }
        }

        SerializerEntry entry{
            .serialize = std::move(serialize),
            .deserialize = std::move(deserialize),
            .validate = validate,
            .version = 1,
            .type_name = name
        };

        std::type_index idx(typeid(T));
        auto [it, inserted] = registry_.emplace(idx, entry);

        if (!inserted) {
            SYSLOG_WARN("Component '{}' already registered, replacing", name);
        }

        SYSLOG_INFO("Registered component serializer: {}", name);
        return true;
    }

    /**
     * @brief Check if component is registered
     */
    bool IsRegistered(const std::type_index& idx) const {
        std::lock_guard<std::mutex> lock(mutex_);
        return registry_.find(idx) != registry_.end();
    }

    /**
     * @brief Get serializer entry for component type
     */
    const SerializerEntry* GetEntry(const std::type_index& idx) const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = registry_.find(idx);
        if (it == registry_.end()) {
            return nullptr;
        }
        return &it->second;
    }

    /**
     * @brief Get entry by component name
     */
    const SerializerEntry* GetEntryByName(const std::string& name) const {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& [idx, entry] : registry_) {
            if (entry.type_name == name) {
                return &entry;
            }
        }
        return nullptr;
    }

    /**
     * @brief Get all registered component names
     */
    std::vector<std::string> GetRegisteredComponents() const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<std::string> names;
        for (const auto& [idx, entry] : registry_) {
            names.push_back(entry.type_name);
        }
        return names;
    }

    /**
     * @brief Clear all registrations (for testing)
     */
    void Clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        registry_.clear();
    }

    /**
     * @brief Get total number of registered components
     */
    size_t GetComponentCount() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return registry_.size();
    }

 private:
    mutable std::mutex mutex_;
    std::map<std::type_index, SerializerEntry> registry_;
};

}  // namespace mir2::persistence

#endif  // MIR2_PERSISTENCE_COMPONENT_REGISTRY_H
