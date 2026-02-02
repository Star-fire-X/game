/**
 * @file storage_backend.h
 * @brief Storage backend abstraction (Epic 1: Story 1.3)
 */

#ifndef MIR2_PERSISTENCE_STORAGE_BACKEND_H
#define MIR2_PERSISTENCE_STORAGE_BACKEND_H

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace mir2::persistence {

/**
 * @brief Storage result type
 */
template<typename T = void>
struct StorageResult {
    bool success;
    T value;
    std::string error_message;

    static StorageResult Success(T value = T{}) {
        return StorageResult{true, std::move(value), ""};
    }

    static StorageResult Failure(const std::string& error) {
        return StorageResult{false, T{}, error};
    }
};

// Specialization for void
template<>
struct StorageResult<void> {
    bool success;
    std::string error_message;

    static StorageResult Success() { return StorageResult{true, ""}; }

    static StorageResult Failure(const std::string& error) {
        return StorageResult{false, error};
    }
};

/**
 * @brief Entity snapshot metadata
 */
struct SnapshotMetadata {
    uint64_t snapshot_id;
    uint64_t timestamp;
    uint32_t entity_count;
    uint32_t component_count;
    std::string version;
    std::string checksum;  // SHA256
};

/**
 * @brief Storage backend interface
 *
 * Defines the contract for persistence backends (PostgreSQL, FileSystem, etc.)
 * Implementations must be thread-safe for concurrent reads.
 */
class IStorageBackend {
 public:
    virtual ~IStorageBackend() noexcept = default;

    /**
     * @brief Save a single entity with components
     * @param entity_id Unique entity identifier
     * @param entity_type Type name (player, npc, item, etc.)
     * @param data Serialized entity data
     * @param version Current version for conflict detection
     * @return Success/failure result
     * @note noexcept: errors returned via StorageResult, no exceptions thrown
     */
    virtual StorageResult<> SaveEntity(
        uint64_t entity_id,
        const std::string& entity_type,
        const std::vector<uint8_t>& data,
        uint32_t version) noexcept = 0;

    /**
     * @brief Save multiple entities in a single transaction
     * @param entities List of (entity_id, entity_type, data, version) tuples
     * @return Success/failure result
     * @note noexcept: errors returned via StorageResult, no exceptions thrown
     */
    virtual StorageResult<> SaveEntitiesBatch(
        const std::vector<std::tuple<uint64_t, std::string, std::vector<uint8_t>, uint32_t>>& entities) noexcept = 0;

    /**
     * @brief Load a single entity
     * @param entity_id Entity to load
     * @return Entity data and version on success
     * @note noexcept: errors returned via StorageResult, no exceptions thrown
     */
    virtual StorageResult<std::pair<std::vector<uint8_t>, uint32_t>> LoadEntity(
        uint64_t entity_id) noexcept = 0;

    /**
     * @brief Load multiple entities
     * @param entity_ids Entities to load
     * @return Map of entity_id -> (data, version)
     * @note noexcept: errors returned via StorageResult, no exceptions thrown
     */
    virtual StorageResult<std::map<uint64_t, std::pair<std::vector<uint8_t>, uint32_t>>>
    LoadEntitiesBatch(const std::vector<uint64_t>& entity_ids) noexcept = 0;

    /**
     * @brief Load all entities (for server startup)
     * @return Map of entity_id -> (entity_type, data, version)
     * @note noexcept: errors returned via StorageResult, no exceptions thrown
     */
    virtual StorageResult<std::map<uint64_t, std::tuple<std::string, std::vector<uint8_t>, uint32_t>>>
    LoadAllEntities() noexcept = 0;

    /**
     * @brief Delete an entity
     * @param entity_id Entity to delete
     * @return Success/failure result
     * @note noexcept: errors returned via StorageResult, no exceptions thrown
     */
    virtual StorageResult<> DeleteEntity(uint64_t entity_id) noexcept = 0;

    /**
     * @brief Create a snapshot of current state
     * @param snapshot_name User-friendly name
     * @return Snapshot metadata
     * @note noexcept: errors returned via StorageResult, no exceptions thrown
     */
    virtual StorageResult<SnapshotMetadata> CreateSnapshot(const std::string& snapshot_name) noexcept = 0;

    /**
     * @brief Get latest snapshot metadata
     * @return Latest snapshot or empty if none exists
     * @note noexcept: errors returned via StorageResult, no exceptions thrown
     */
    virtual StorageResult<std::optional<SnapshotMetadata>> GetLatestSnapshot() noexcept = 0;

    /**
     * @brief Get snapshot history
     * @param limit Maximum number of snapshots to return
     * @return List of snapshot metadata ordered by timestamp descending
     * @note noexcept: errors returned via StorageResult, no exceptions thrown
     */
    virtual StorageResult<std::vector<SnapshotMetadata>> GetSnapshotHistory(int limit = 10) noexcept = 0;

    /**
     * @brief Load all entities from a specific snapshot
     * @param snapshot_id Snapshot identifier
     * @return Map of entity_id -> (entity_type, data, version)
     * @note noexcept: errors returned via StorageResult, no exceptions thrown
     */
    virtual StorageResult<std::map<uint64_t, std::tuple<std::string, std::vector<uint8_t>, uint32_t>>>
    LoadAllEntitiesFromSnapshot(uint64_t snapshot_id) noexcept = 0;

    /**
     * @brief Mark a snapshot as corrupted
     * @param snapshot_id Snapshot to mark
     * @param reason Reason for corruption
     * @note noexcept: errors returned via StorageResult, no exceptions thrown
     */
    virtual StorageResult<> MarkSnapshotCorrupted(uint64_t snapshot_id, const std::string& reason) noexcept = 0;

    /**
     * @brief Check backend connectivity
     * @return true if backend is ready
     * @note noexcept: simple state check, no exceptions
     */
    virtual bool IsReady() noexcept = 0;

    /**
     * @brief Get backend name (for logging)
     * @note noexcept: returns static string, no exceptions
     */
    virtual const char* GetName() const noexcept = 0;
};

/**
 * @brief Typedef for entity storage entry
 */
using EntityStorageEntry = std::tuple<uint64_t, std::string, std::vector<uint8_t>, uint32_t>;

}  // namespace mir2::persistence

#endif  // MIR2_PERSISTENCE_STORAGE_BACKEND_H
