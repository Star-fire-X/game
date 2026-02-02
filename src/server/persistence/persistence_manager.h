/**
 * @file persistence_manager.h
 * @brief Core persistence manager interface
 */

#ifndef MIR2_PERSISTENCE_PERSISTENCE_MANAGER_H
#define MIR2_PERSISTENCE_PERSISTENCE_MANAGER_H

#include "persistence/storage_backend.h"
#include "persistence/serializer.h"
#include "persistence/component_registry.h"
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <vector>
#include <queue>
#include <chrono>
#include <atomic>

namespace mir2::persistence {

/**
 * @brief Persistence result type
 */
struct PersistenceResult {
    bool success;
    std::string error_message;
    int64_t duration_ms = 0;  // Operation duration

    static PersistenceResult Success() {
        return PersistenceResult{true, ""};
    }

    static PersistenceResult Failure(const std::string& error) {
        return PersistenceResult{false, error};
    }
};

/**
 * @brief Component change listener callback
 */
using ComponentChangeListener = std::function<void(uint64_t entity_id, const std::string& component_name)>;

/**
 * @brief Core persistence manager
 *
 * Provides unified interface for:
 * - Component registration (Story 1.1)
 * - Serialization/deserialization (Story 1.4)
 * - Storage operations (Story 1.3)
 * - Automatic saves (Story 2.1)
 * - Dirty tracking (Story 2.2)
 * - Startup recovery (Story 3.1)
 * - Monitoring (Story 4.3)
 *
 * Design pattern: Singleton with dependency injection for storage backend and serializer.
 */
class PersistenceManager {
 public:
    /**
     * @brief Configuration for persistence system
     */
    struct Config {
        // Save strategy
        uint32_t auto_save_interval_seconds = 300;  // 5 minutes
        uint32_t dirty_save_threshold_ms = 100;     // High-priority save threshold
        uint32_t batch_size = 100;                  // Batch write size
        uint32_t batch_timeout_ms = 200;            // Batch flush timeout
        uint32_t shutdown_timeout_ms = 30000;       // Graceful shutdown timeout

        // Monitoring
        bool enable_metrics = true;
        bool enable_audit_log = true;
    };

    /**
     * @brief Initialize persistent manager (must call once at startup)
     */
    static bool Initialize(
        std::unique_ptr<IStorageBackend> backend,
        std::unique_ptr<ISerializer> serializer,
        const Config& config = Config{});

    /**
     * @brief Get singleton instance
     */
    static PersistenceManager& Instance();

    /**
     * @brief Check if initialized
     * @note noexcept: simple state check
     */
    static bool IsInitialized() noexcept;

    /**
     * @brief Shutdown (cleanup resources)
     */
    static void Shutdown();

    /**
     * @brief Register a component type (Story 1.1)
     * @tparam T Component type
     * @param name Component name
     * @param serialize Function to serialize
     * @param deserialize Function to deserialize
     * @param validate Optional validation function
     * @return true if successful
     */
    template<typename T>
    bool RegisterComponent(
        const std::string& name,
        std::function<std::vector<uint8_t>(const T&)> serialize,
        std::function<T(const std::vector<uint8_t>&)> deserialize,
        std::function<bool(const T&)> validate = nullptr) {
        return registry_.RegisterComponent<T>(
            name,
            [serialize](const void* ptr) {
                return serialize(*static_cast<const T*>(ptr));
            },
            [deserialize](const std::vector<uint8_t>& data, void* ptr) {
                try {
                    *static_cast<T*>(ptr) = deserialize(data);
                    return true;
                } catch (...) {
                    return false;
                }
            },
            validate ? [validate](const void* ptr) {
                return validate(*static_cast<const T*>(ptr));
            } : nullptr
        );
    }

    /**
     * @brief Mark component as dirty (needs saving)
     * @param entity_id Entity identifier
     * @param component_name Component name
     * @param is_high_priority true for high-priority (immediate save), false for normal
     * @note noexcept: internal queue operation, never throws
     */
    void MarkComponentDirty(
        uint64_t entity_id,
        const std::string& component_name,
        bool is_high_priority = false) noexcept;

    /**
     * @brief Save entity immediately (synchronous)
     */
    PersistenceResult SaveEntity(
        uint64_t entity_id,
        const std::string& entity_type,
        const std::vector<uint8_t>& data,
        uint32_t version = 1);

    /**
     * @brief Save multiple entities in batch (synchronous)
     */
    PersistenceResult SaveEntitiesBatch(
        const std::vector<std::tuple<uint64_t, std::string, std::vector<uint8_t>, uint32_t>>& entities);

    /**
     * @brief Load entity (synchronous)
     */
    std::optional<std::pair<std::vector<uint8_t>, uint32_t>> LoadEntity(uint64_t entity_id);

    /**
     * @brief Load all entities (for startup recovery)
     */
    std::optional<std::map<uint64_t, std::tuple<std::string, std::vector<uint8_t>, uint32_t>>> LoadAllEntities();

    /**
     * @brief Register change listener for monitoring (Story 4.3)
     */
    void RegisterComponentChangeListener(
        const std::string& component_name,
        ComponentChangeListener listener);

    /**
     * @brief Get persistence metrics (Story 4.3)
     * @note noexcept: returns copy of internal state
     */
    struct Metrics {
        uint64_t total_saves = 0;
        uint64_t failed_saves = 0;
        uint64_t total_bytes_written = 0;
        double avg_save_latency_ms = 0.0;
    };

    Metrics GetMetrics() const noexcept {
        std::lock_guard<std::mutex> lock(metrics_mutex_);
        return current_metrics_;
    }

    /**
     * @brief Create graceful shutdown snapshot (Story 2.3)
     */
    PersistenceResult CreateGracefulShutdownSnapshot();

    /**
     * @brief Perform startup recovery (Story 3.1)
     */
    PersistenceResult PerformStartupRecovery();

    /**
     * @brief Validate persisted data (Story 3.2)
     */
    PersistenceResult ValidateSnapshot();

    // Private constructor - singleton pattern
 private:
    PersistenceManager() = default;
    ~PersistenceManager() = default;

    // Delete copy/move constructors
    PersistenceManager(const PersistenceManager&) = delete;
    PersistenceManager& operator=(const PersistenceManager&) = delete;
    PersistenceManager(PersistenceManager&&) = delete;
    PersistenceManager& operator=(PersistenceManager&&) = delete;

    static std::unique_ptr<PersistenceManager> instance_;
    static std::mutex instance_mutex_;

    // Components
    std::unique_ptr<IStorageBackend> backend_;
    std::unique_ptr<ISerializer> serializer_;
    ComponentRegistry registry_;
    Config config_;

    // Dirty tracking
    struct DirtyEntry {
        uint64_t entity_id;
        std::string component_name;
        std::chrono::steady_clock::time_point mark_time;
        bool is_high_priority;
    };

    std::mutex dirty_mutex_;
    std::queue<DirtyEntry> dirty_queue_;

    // Metrics
    mutable std::mutex metrics_mutex_;
    Metrics current_metrics_;

    // Change listeners
    std::map<std::string, std::vector<ComponentChangeListener>> change_listeners_;
};

}  // namespace mir2::persistence

#endif  // MIR2_PERSISTENCE_PERSISTENCE_MANAGER_H
