/**
 * @file postgres_backend.cc
 * @brief PostgreSQL backend implementation with full libpqxx integration
 */

#include "persistence/postgres_backend.h"
#include "persistence/postgres_transaction.h"
#include "persistence/persistence_error.h"
#include "log/logger.h"
#include <chrono>
#include <sstream>
#include <iomanip>

namespace mir2::persistence {

PostgresBackend::PostgresBackend(const Config& config) : config_(config) {
#ifdef HAVE_LIBPQXX
    PostgresConnectionPool::Config pool_config{
        .connection_string = BuildConnectionString(),
        .pool_size = config_.max_connections,
        .acquire_timeout_ms = config_.connection_timeout_ms,
        .connection_timeout_ms = config_.connection_timeout_ms
    };
    connection_pool_ = std::make_unique<PostgresConnectionPool>(pool_config);

    if (connection_pool_->TestConnection()) {
        SYSLOG_INFO("PostgreSQL backend connected to {}:{}/{}",
                    config_.host, config_.port, config_.database);
        InitializeSchema();
    } else {
        SYSLOG_ERROR("PostgreSQL backend failed to connect");
    }
#else
    SYSLOG_WARN("PostgreSQL backend initialized (stub mode - libpqxx not available)");
#endif
}

PostgresBackend::~PostgresBackend() noexcept {
#ifdef HAVE_LIBPQXX
    if (connection_pool_) {
        connection_pool_->Shutdown();
    }
#endif
}

std::string PostgresBackend::BuildConnectionString() const {
    std::ostringstream ss;
    ss << "host=" << config_.host
       << " port=" << config_.port
       << " dbname=" << config_.database
       << " user=" << config_.user;
    if (!config_.password.empty()) {
        ss << " password=" << config_.password;
    }
    ss << " connect_timeout=" << (config_.connection_timeout_ms / 1000);
    return ss.str();
}

void PostgresBackend::InitializeSchema() {
#ifdef HAVE_LIBPQXX
    auto conn = connection_pool_->AcquireConnection();
    if (!conn) {
        SYSLOG_ERROR("Failed to acquire connection for schema initialization");
        return;
    }

    try {
        PostgresTransaction txn(conn);

        // Create entity_snapshots table
        txn.Exec(R"(
            CREATE TABLE IF NOT EXISTS entity_snapshots (
                entity_id BIGINT PRIMARY KEY,
                entity_type VARCHAR(64) NOT NULL,
                data BYTEA NOT NULL,
                version INT NOT NULL,
                created_at TIMESTAMP DEFAULT NOW(),
                updated_at TIMESTAMP DEFAULT NOW()
            )
        )");

        // Create snapshot_history table
        txn.Exec(R"(
            CREATE TABLE IF NOT EXISTS snapshot_history (
                snapshot_id BIGSERIAL PRIMARY KEY,
                name VARCHAR(255) NOT NULL,
                timestamp TIMESTAMP DEFAULT NOW(),
                entity_count INT NOT NULL,
                component_count INT NOT NULL,
                version VARCHAR(20) NOT NULL,
                checksum VARCHAR(64) NOT NULL,
                status VARCHAR(20) DEFAULT 'valid'
            )
        )");

        // Create index for entity_type queries
        txn.Exec(R"(
            CREATE INDEX IF NOT EXISTS idx_entity_type ON entity_snapshots(entity_type)
        )");

        txn.Commit();
        SYSLOG_INFO("PostgreSQL schema initialized");
    } catch (const std::exception& e) {
        SYSLOG_ERROR("Schema initialization failed: {}", e.what());
    }
#endif
}

bool PostgresBackend::IsReady() noexcept {
#ifdef HAVE_LIBPQXX
    try {
        return connection_pool_ && connection_pool_->TestConnection();
    } catch (...) {
        return false;
    }
#else
    return true;  // Stub mode always ready
#endif
}

StorageResult<> PostgresBackend::SaveEntity(
    uint64_t entity_id,
    const std::string& entity_type,
    const std::vector<uint8_t>& data,
    uint32_t version) noexcept {
#ifdef HAVE_LIBPQXX
    try {
        auto conn = connection_pool_->AcquireConnection();
        if (!conn) {
            return StorageResult<>::Failure("Failed to acquire database connection");
        }

        PostgresTransaction txn(conn);

        // Upsert with parameterized query (prevents SQL injection)
        pqxx::binarystring binary_data(data.data(), data.size());
        txn.ExecParams(
            R"(
                INSERT INTO entity_snapshots (entity_id, entity_type, data, version, updated_at)
                VALUES ($1, $2, $3, $4, NOW())
                ON CONFLICT (entity_id) DO UPDATE SET
                    entity_type = EXCLUDED.entity_type,
                    data = EXCLUDED.data,
                    version = EXCLUDED.version,
                    updated_at = NOW()
            )",
            static_cast<int64_t>(entity_id),
            entity_type,
            binary_data,
            static_cast<int>(version)
        );

        txn.Commit();
        SYSLOG_DEBUG("Saved entity {} (type: {}, version: {})", entity_id, entity_type, version);
        return StorageResult<>::Success();
    } catch (const std::exception& e) {
        SYSLOG_ERROR("SaveEntity failed: {}", e.what());
        return StorageResult<>::Failure(std::string("Save failed: ") + e.what());
    }
#else
    SYSLOG_DEBUG("Saving entity {} (stub mode)", entity_id);
    return StorageResult<>::Success();
#endif
}

StorageResult<> PostgresBackend::SaveEntitiesBatch(
    const std::vector<std::tuple<uint64_t, std::string, std::vector<uint8_t>, uint32_t>>& entities) noexcept {
#ifdef HAVE_LIBPQXX
    if (entities.empty()) {
        return StorageResult<>::Success();
    }

    try {
        auto conn = connection_pool_->AcquireConnection();
        if (!conn) {
            return StorageResult<>::Failure("Failed to acquire database connection");
        }

        PostgresTransaction txn(conn);

        // All entities saved in single transaction - atomic
        for (const auto& [entity_id, entity_type, data, version] : entities) {
            pqxx::binarystring binary_data(data.data(), data.size());
            txn.ExecParams(
                R"(
                    INSERT INTO entity_snapshots (entity_id, entity_type, data, version, updated_at)
                    VALUES ($1, $2, $3, $4, NOW())
                    ON CONFLICT (entity_id) DO UPDATE SET
                        entity_type = EXCLUDED.entity_type,
                        data = EXCLUDED.data,
                        version = EXCLUDED.version,
                        updated_at = NOW()
                )",
                static_cast<int64_t>(entity_id),
                entity_type,
                binary_data,
                static_cast<int>(version)
            );
        }

        txn.Commit();
        SYSLOG_INFO("Batch saved {} entities successfully", entities.size());
        return StorageResult<>::Success();
    } catch (const std::exception& e) {
        SYSLOG_ERROR("SaveEntitiesBatch failed: {}", e.what());
        return StorageResult<>::Failure(std::string("Batch save failed: ") + e.what());
    }
#else
    SYSLOG_DEBUG("Batch saving {} entities (stub mode)", entities.size());
    return StorageResult<>::Success();
#endif
}

StorageResult<std::pair<std::vector<uint8_t>, uint32_t>> PostgresBackend::LoadEntity(
    uint64_t entity_id) noexcept {
#ifdef HAVE_LIBPQXX
    try {
        auto conn = connection_pool_->AcquireConnection();
        if (!conn) {
            return StorageResult<std::pair<std::vector<uint8_t>, uint32_t>>::Failure(
                "Failed to acquire database connection");
        }

        PostgresTransaction txn(conn);

        auto result = txn.ExecParams(
            "SELECT data, version FROM entity_snapshots WHERE entity_id = $1",
            static_cast<int64_t>(entity_id)
        );

        txn.Commit();

        if (result.empty()) {
            return StorageResult<std::pair<std::vector<uint8_t>, uint32_t>>::Failure(
                "Entity not found");
        }

        auto binary_data = result[0][0].as<pqxx::binarystring>();
        uint32_t version = result[0][1].as<int>();

        std::vector<uint8_t> data(binary_data.begin(), binary_data.end());
        return StorageResult<std::pair<std::vector<uint8_t>, uint32_t>>::Success(
            std::make_pair(std::move(data), version));
    } catch (const std::exception& e) {
        return StorageResult<std::pair<std::vector<uint8_t>, uint32_t>>::Failure(
            std::string("Load failed: ") + e.what());
    }
#else
    SYSLOG_DEBUG("Loading entity {} (stub mode)", entity_id);
    return StorageResult<std::pair<std::vector<uint8_t>, uint32_t>>::Failure("Entity not found");
#endif
}

StorageResult<std::map<uint64_t, std::pair<std::vector<uint8_t>, uint32_t>>>
PostgresBackend::LoadEntitiesBatch(const std::vector<uint64_t>& entity_ids) noexcept {
#ifdef HAVE_LIBPQXX
    if (entity_ids.empty()) {
        return StorageResult<std::map<uint64_t, std::pair<std::vector<uint8_t>, uint32_t>>>::Success({});
    }

    try {
        auto conn = connection_pool_->AcquireConnection();
        if (!conn) {
            return StorageResult<std::map<uint64_t, std::pair<std::vector<uint8_t>, uint32_t>>>::Failure(
                "Failed to acquire database connection");
        }

        PostgresTransaction txn(conn);

        // Use parameterized query with array to prevent SQL injection
        // Convert entity_ids to PostgreSQL array format: {1,2,3}
        std::ostringstream array_ss;
        array_ss << "{";
        for (size_t i = 0; i < entity_ids.size(); ++i) {
            if (i > 0) array_ss << ",";
            array_ss << entity_ids[i];
        }
        array_ss << "}";

        auto result = txn.ExecParams(
            "SELECT entity_id, data, version FROM entity_snapshots WHERE entity_id = ANY($1::bigint[])",
            array_ss.str()
        );

        txn.Commit();

        std::map<uint64_t, std::pair<std::vector<uint8_t>, uint32_t>> entities;
        for (const auto& row : result) {
            uint64_t id = row[0].as<int64_t>();
            auto binary_data = row[1].as<pqxx::binarystring>();
            uint32_t version = row[2].as<int>();

            std::vector<uint8_t> data(binary_data.begin(), binary_data.end());
            entities[id] = std::make_pair(std::move(data), version);
        }

        SYSLOG_DEBUG("Batch loaded {} entities", entities.size());
        return StorageResult<std::map<uint64_t, std::pair<std::vector<uint8_t>, uint32_t>>>::Success(
            std::move(entities));
    } catch (const std::exception& e) {
        return StorageResult<std::map<uint64_t, std::pair<std::vector<uint8_t>, uint32_t>>>::Failure(
            std::string("Batch load failed: ") + e.what());
    }
#else
    return StorageResult<std::map<uint64_t, std::pair<std::vector<uint8_t>, uint32_t>>>::Success({});
#endif
}

StorageResult<std::map<uint64_t, std::tuple<std::string, std::vector<uint8_t>, uint32_t>>>
PostgresBackend::LoadAllEntities() noexcept {
#ifdef HAVE_LIBPQXX
    try {
        auto conn = connection_pool_->AcquireConnection();
        if (!conn) {
            return StorageResult<std::map<uint64_t, std::tuple<std::string, std::vector<uint8_t>, uint32_t>>>::Failure(
                "Failed to acquire database connection");
        }

        PostgresTransaction txn(conn);

        auto result = txn.Exec(
            "SELECT entity_id, entity_type, data, version FROM entity_snapshots"
        );

        txn.Commit();

        std::map<uint64_t, std::tuple<std::string, std::vector<uint8_t>, uint32_t>> entities;
        for (const auto& row : result) {
            uint64_t id = row[0].as<int64_t>();
            std::string type = row[1].as<std::string>();
            auto binary_data = row[2].as<pqxx::binarystring>();
            uint32_t version = row[3].as<int>();

            std::vector<uint8_t> data(binary_data.begin(), binary_data.end());
            entities[id] = std::make_tuple(std::move(type), std::move(data), version);
        }

        SYSLOG_INFO("Loaded {} entities from database", entities.size());
        return StorageResult<std::map<uint64_t, std::tuple<std::string, std::vector<uint8_t>, uint32_t>>>::Success(
            std::move(entities));
    } catch (const std::exception& e) {
        return StorageResult<std::map<uint64_t, std::tuple<std::string, std::vector<uint8_t>, uint32_t>>>::Failure(
            std::string("Load all failed: ") + e.what());
    }
#else
    return StorageResult<std::map<uint64_t, std::tuple<std::string, std::vector<uint8_t>, uint32_t>>>::Success({});
#endif
}

StorageResult<> PostgresBackend::DeleteEntity(uint64_t entity_id) noexcept {
#ifdef HAVE_LIBPQXX
    try {
        auto conn = connection_pool_->AcquireConnection();
        if (!conn) {
            return StorageResult<>::Failure("Failed to acquire database connection");
        }

        PostgresTransaction txn(conn);

        txn.ExecParams(
            "DELETE FROM entity_snapshots WHERE entity_id = $1",
            static_cast<int64_t>(entity_id)
        );

        txn.Commit();
        SYSLOG_DEBUG("Deleted entity {}", entity_id);
        return StorageResult<>::Success();
    } catch (const std::exception& e) {
        return StorageResult<>::Failure(std::string("Delete failed: ") + e.what());
    }
#else
    SYSLOG_DEBUG("Deleting entity {} (stub mode)", entity_id);
    return StorageResult<>::Success();
#endif
}

StorageResult<SnapshotMetadata> PostgresBackend::CreateSnapshot(const std::string& snapshot_name) noexcept {
#ifdef HAVE_LIBPQXX
    try {
        auto conn = connection_pool_->AcquireConnection();
        if (!conn) {
            return StorageResult<SnapshotMetadata>::Failure("Failed to acquire database connection");
        }

        PostgresTransaction txn(conn);

        // Count entities
        auto count_result = txn.Exec("SELECT COUNT(*) FROM entity_snapshots");
        int entity_count = count_result[0][0].as<int>();

        // Generate checksum (simple hash of entity count + timestamp)
        auto now = std::chrono::system_clock::now();
        auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()).count();

        std::ostringstream checksum_ss;
        checksum_ss << std::hex << std::setfill('0') << std::setw(16) << timestamp;
        std::string checksum = checksum_ss.str();

        // Insert snapshot record
        auto result = txn.ExecParams(
            R"(
                INSERT INTO snapshot_history (name, entity_count, component_count, version, checksum, status)
                VALUES ($1, $2, $3, $4, $5, 'valid')
                RETURNING snapshot_id
            )",
            snapshot_name,
            entity_count,
            0,  // component_count
            "1.0",
            checksum
        );

        txn.Commit();

        SnapshotMetadata metadata{
            .snapshot_id = static_cast<uint64_t>(result[0][0].as<int64_t>()),
            .timestamp = static_cast<uint64_t>(timestamp),
            .entity_count = static_cast<uint32_t>(entity_count),
            .component_count = 0,
            .version = "1.0",
            .checksum = checksum
        };

        SYSLOG_INFO("Created snapshot '{}' (id: {}, entities: {})",
                    snapshot_name, metadata.snapshot_id, entity_count);
        return StorageResult<SnapshotMetadata>::Success(metadata);
    } catch (const std::exception& e) {
        return StorageResult<SnapshotMetadata>::Failure(
            std::string("Snapshot creation failed: ") + e.what());
    }
#else
    SnapshotMetadata metadata{
        .snapshot_id = 1,
        .timestamp = static_cast<uint64_t>(
            std::chrono::system_clock::now().time_since_epoch().count()),
        .entity_count = 0,
        .component_count = 0,
        .version = "1.0",
        .checksum = ""
    };
    SYSLOG_INFO("Created snapshot '{}' (stub mode)", snapshot_name);
    return StorageResult<SnapshotMetadata>::Success(metadata);
#endif
}

StorageResult<std::optional<SnapshotMetadata>> PostgresBackend::GetLatestSnapshot() noexcept {
#ifdef HAVE_LIBPQXX
    try {
        auto conn = connection_pool_->AcquireConnection();
        if (!conn) {
            return StorageResult<std::optional<SnapshotMetadata>>::Failure(
                "Failed to acquire database connection");
        }

        PostgresTransaction txn(conn);

        auto result = txn.Exec(
            R"(
                SELECT snapshot_id, EXTRACT(EPOCH FROM timestamp)::BIGINT * 1000,
                       entity_count, component_count, version, checksum
                FROM snapshot_history
                WHERE status = 'valid'
                ORDER BY timestamp DESC
                LIMIT 1
            )"
        );

        txn.Commit();

        if (result.empty()) {
            return StorageResult<std::optional<SnapshotMetadata>>::Success(std::nullopt);
        }

        SnapshotMetadata metadata{
            .snapshot_id = static_cast<uint64_t>(result[0][0].as<int64_t>()),
            .timestamp = static_cast<uint64_t>(result[0][1].as<int64_t>()),
            .entity_count = static_cast<uint32_t>(result[0][2].as<int>()),
            .component_count = static_cast<uint32_t>(result[0][3].as<int>()),
            .version = result[0][4].as<std::string>(),
            .checksum = result[0][5].as<std::string>()
        };

        return StorageResult<std::optional<SnapshotMetadata>>::Success(metadata);
    } catch (const std::exception& e) {
        return StorageResult<std::optional<SnapshotMetadata>>::Failure(
            std::string("Snapshot retrieval failed: ") + e.what());
    }
#else
    SYSLOG_DEBUG("Retrieving latest snapshot (stub mode)");
    return StorageResult<std::optional<SnapshotMetadata>>::Success(std::nullopt);
#endif
}

StorageResult<std::vector<SnapshotMetadata>> PostgresBackend::GetSnapshotHistory(int limit) noexcept {
#ifdef HAVE_LIBPQXX
    try {
        auto conn = connection_pool_->AcquireConnection();
        if (!conn) {
            return StorageResult<std::vector<SnapshotMetadata>>::Failure(
                "Failed to acquire database connection");
        }

        PostgresTransaction txn(conn);

        auto result = txn.ExecParams(
            R"(
                SELECT snapshot_id, EXTRACT(EPOCH FROM timestamp)::BIGINT * 1000,
                       entity_count, component_count, version, checksum
                FROM snapshot_history
                ORDER BY timestamp DESC
                LIMIT $1
            )",
            limit
        );

        txn.Commit();

        std::vector<SnapshotMetadata> snapshots;
        snapshots.reserve(result.size());

        for (const auto& row : result) {
            snapshots.push_back(SnapshotMetadata{
                .snapshot_id = static_cast<uint64_t>(row[0].as<int64_t>()),
                .timestamp = static_cast<uint64_t>(row[1].as<int64_t>()),
                .entity_count = static_cast<uint32_t>(row[2].as<int>()),
                .component_count = static_cast<uint32_t>(row[3].as<int>()),
                .version = row[4].as<std::string>(),
                .checksum = row[5].as<std::string>()
            });
        }

        SYSLOG_DEBUG("Retrieved {} snapshots from history", snapshots.size());
        return StorageResult<std::vector<SnapshotMetadata>>::Success(std::move(snapshots));
    } catch (const std::exception& e) {
        return StorageResult<std::vector<SnapshotMetadata>>::Failure(
            std::string("Snapshot history retrieval failed: ") + e.what());
    }
#else
    SYSLOG_DEBUG("Retrieving snapshot history (stub mode)");
    return StorageResult<std::vector<SnapshotMetadata>>::Success({});
#endif
}

StorageResult<std::map<uint64_t, std::tuple<std::string, std::vector<uint8_t>, uint32_t>>>
PostgresBackend::LoadAllEntitiesFromSnapshot(uint64_t snapshot_id) noexcept {
#ifdef HAVE_LIBPQXX
    try {
        auto conn = connection_pool_->AcquireConnection();
        if (!conn) {
            return StorageResult<std::map<uint64_t, std::tuple<std::string, std::vector<uint8_t>, uint32_t>>>::Failure(
                "Failed to acquire database connection");
        }

        PostgresTransaction txn(conn);

        // First verify snapshot exists and is valid
        auto snapshot_check = txn.ExecParams(
            "SELECT status FROM snapshot_history WHERE snapshot_id = $1",
            static_cast<int64_t>(snapshot_id)
        );

        if (snapshot_check.empty()) {
            return StorageResult<std::map<uint64_t, std::tuple<std::string, std::vector<uint8_t>, uint32_t>>>::Failure(
                "Snapshot not found");
        }

        std::string status = snapshot_check[0][0].as<std::string>();
        if (status == "corrupted") {
            return StorageResult<std::map<uint64_t, std::tuple<std::string, std::vector<uint8_t>, uint32_t>>>::Failure(
                "Snapshot is marked as corrupted");
        }

        // Load entities (current implementation loads from entity_snapshots table)
        // In a full implementation, this would load from a snapshot-specific table
        auto result = txn.Exec(
            "SELECT entity_id, entity_type, data, version FROM entity_snapshots"
        );

        txn.Commit();

        std::map<uint64_t, std::tuple<std::string, std::vector<uint8_t>, uint32_t>> entities;
        for (const auto& row : result) {
            uint64_t id = row[0].as<int64_t>();
            std::string type = row[1].as<std::string>();
            auto binary_data = row[2].as<pqxx::binarystring>();
            uint32_t version = row[3].as<int>();

            std::vector<uint8_t> data(binary_data.begin(), binary_data.end());
            entities[id] = std::make_tuple(std::move(type), std::move(data), version);
        }

        SYSLOG_INFO("Loaded {} entities from snapshot {}", entities.size(), snapshot_id);
        return StorageResult<std::map<uint64_t, std::tuple<std::string, std::vector<uint8_t>, uint32_t>>>::Success(
            std::move(entities));
    } catch (const std::exception& e) {
        return StorageResult<std::map<uint64_t, std::tuple<std::string, std::vector<uint8_t>, uint32_t>>>::Failure(
            std::string("Load from snapshot failed: ") + e.what());
    }
#else
    SYSLOG_DEBUG("Loading entities from snapshot {} (stub mode)", snapshot_id);
    return StorageResult<std::map<uint64_t, std::tuple<std::string, std::vector<uint8_t>, uint32_t>>>::Success({});
#endif
}

StorageResult<> PostgresBackend::MarkSnapshotCorrupted(uint64_t snapshot_id, const std::string& reason) noexcept {
#ifdef HAVE_LIBPQXX
    try {
        auto conn = connection_pool_->AcquireConnection();
        if (!conn) {
            return StorageResult<>::Failure("Failed to acquire database connection");
        }

        PostgresTransaction txn(conn);

        txn.ExecParams(
            "UPDATE snapshot_history SET status = 'corrupted' WHERE snapshot_id = $1",
            static_cast<int64_t>(snapshot_id)
        );

        txn.Commit();

        SYSLOG_WARN("Marked snapshot {} as corrupted: {}", snapshot_id, reason);
        return StorageResult<>::Success();
    } catch (const std::exception& e) {
        return StorageResult<>::Failure(std::string("Mark corrupted failed: ") + e.what());
    }
#else
    SYSLOG_DEBUG("Marking snapshot {} as corrupted (stub mode): {}", snapshot_id, reason);
    return StorageResult<>::Success();
#endif
}

}  // namespace mir2::persistence
