/**
 * @file snapshot_recovery.h
 * @brief Automatic snapshot recovery with fallback (Epic 3: Story 3.1)
 */

#ifndef MIR2_PERSISTENCE_SNAPSHOT_RECOVERY_H
#define MIR2_PERSISTENCE_SNAPSHOT_RECOVERY_H

#include "persistence/storage_backend.h"
#include "persistence/data_validator.h"
#include <vector>
#include <memory>
#include <mutex>
#include <chrono>

namespace mir2::persistence {

/**
 * @brief Snapshot information for recovery
 */
struct SnapshotInfo {
    uint64_t snapshot_id;
    std::string name;
    int entity_count;
    std::string status;  // valid, corrupted, partial
    std::chrono::system_clock::time_point timestamp;
};

/**
 * @brief Automatic snapshot recovery with fallback mechanism
 *
 * Features:
 * - Attempts recovery from latest snapshot
 * - Automatically falls back to previous snapshots on failure
 * - Validates checksum and data integrity
 * - Marks corrupted snapshots for future avoidance
 *
 * Thread safety: Not thread-safe. Called once at server startup.
 */
class SnapshotRecovery {
 public:
    /**
     * @brief Create recovery handler
     * @param backend Storage backend for snapshot access
     */
    explicit SnapshotRecovery(std::shared_ptr<IStorageBackend> backend);

    /**
     * @brief Recovery attempt result
     */
    struct RecoveryAttempt {
        bool success;
        uint64_t snapshot_id;
        std::string error_message;
        std::map<uint64_t, std::tuple<std::string, std::vector<uint8_t>, uint32_t>> entities;
        int fallback_attempts;
    };

    /**
     * @brief Recover with automatic fallback
     *
     * Tries to recover from the latest valid snapshot.
     * On failure, automatically falls back to previous snapshots.
     *
     * @param max_fallback_attempts Maximum number of fallback attempts
     * @return Recovery result with entities and status
     */
    RecoveryAttempt RecoverWithAutomaticFallback(int max_fallback_attempts = 5);

 private:
    /**
     * @brief Get snapshot history (newest to oldest)
     * @return List of snapshot metadata
     */
    std::vector<SnapshotInfo> GetSnapshotHistory();

    /**
     * @brief Validate and load a specific snapshot
     * @param snapshot_id Snapshot to validate
     * @param out_entities Output entities on success
     * @return true if validation and loading succeeded
     */
    bool ValidateAndLoadSnapshot(
        uint64_t snapshot_id,
        std::map<uint64_t, std::tuple<std::string, std::vector<uint8_t>, uint32_t>>& out_entities);

    /**
     * @brief Calculate checksum for snapshot data
     */
    std::string CalculateSnapshotChecksum(
        const std::map<uint64_t, std::tuple<std::string, std::vector<uint8_t>, uint32_t>>& entities);

    std::shared_ptr<IStorageBackend> backend_;
    std::shared_ptr<DataValidator> validator_;
    std::mutex backend_mutex_;
};

}  // namespace mir2::persistence

#endif  // MIR2_PERSISTENCE_SNAPSHOT_RECOVERY_H
