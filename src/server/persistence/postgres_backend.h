/**
 * @file postgres_backend.h
 * @brief PostgreSQL storage backend implementation (Epic 1: Story 1.3)
 */

#ifndef MIR2_PERSISTENCE_POSTGRES_BACKEND_H
#define MIR2_PERSISTENCE_POSTGRES_BACKEND_H

#include "persistence/storage_backend.h"
#include "persistence/postgres_connection_pool.h"
#include <memory>
#include <string>
#include <map>
#include <mutex>

namespace mir2::persistence {

/**
 * @brief PostgreSQL storage backend implementation
 *
 * Features:
 * - Connection pooling (configurable max connections)
 * - Automatic retry with exponential backoff
 * - Transaction support for batch operations
 * - Snapshot capability for graceful shutdown
 *
 * Thread-safe for concurrent operations.
 */
class PostgresBackend : public IStorageBackend {
 public:
    /**
     * @brief Configuration for PostgreSQL backend
     */
    struct Config {
        std::string host = "localhost";
        int port = 5432;
        std::string database = "mir2";
        std::string user = "mir2_user";
        std::string password = "";
        uint32_t max_connections = 10;
        uint32_t connection_timeout_ms = 5000;
        uint32_t max_retries = 3;
    };

    /**
     * @brief Create PostgreSQL backend
     * @param config Connection configuration
     */
    explicit PostgresBackend(const Config& config);
    ~PostgresBackend() noexcept;

    // IStorageBackend implementation (all noexcept - errors via StorageResult)
    StorageResult<> SaveEntity(
        uint64_t entity_id,
        const std::string& entity_type,
        const std::vector<uint8_t>& data,
        uint32_t version) noexcept override;

    StorageResult<> SaveEntitiesBatch(
        const std::vector<std::tuple<uint64_t, std::string, std::vector<uint8_t>, uint32_t>>& entities) noexcept override;

    StorageResult<std::pair<std::vector<uint8_t>, uint32_t>> LoadEntity(
        uint64_t entity_id) noexcept override;

    StorageResult<std::map<uint64_t, std::pair<std::vector<uint8_t>, uint32_t>>>
    LoadEntitiesBatch(const std::vector<uint64_t>& entity_ids) noexcept override;

    StorageResult<std::map<uint64_t, std::tuple<std::string, std::vector<uint8_t>, uint32_t>>>
    LoadAllEntities() noexcept override;

    StorageResult<> DeleteEntity(uint64_t entity_id) noexcept override;

    StorageResult<SnapshotMetadata> CreateSnapshot(const std::string& snapshot_name) noexcept override;

    StorageResult<std::optional<SnapshotMetadata>> GetLatestSnapshot() noexcept override;

    StorageResult<std::vector<SnapshotMetadata>> GetSnapshotHistory(int limit = 10) noexcept override;

    StorageResult<std::map<uint64_t, std::tuple<std::string, std::vector<uint8_t>, uint32_t>>>
    LoadAllEntitiesFromSnapshot(uint64_t snapshot_id) noexcept override;

    StorageResult<> MarkSnapshotCorrupted(uint64_t snapshot_id, const std::string& reason) noexcept override;

    bool IsReady() noexcept override;

    const char* GetName() const noexcept override { return "PostgresBackend"; }

 private:
    Config config_;
    std::unique_ptr<PostgresConnectionPool> connection_pool_;
    uint32_t failed_attempts_ = 0;

    /**
     * @brief Build libpqxx connection string from config
     */
    std::string BuildConnectionString() const;

    /**
     * @brief Initialize database schema (create tables if not exist)
     */
    void InitializeSchema();
};

}  // namespace mir2::persistence

#endif  // MIR2_PERSISTENCE_POSTGRES_BACKEND_H
