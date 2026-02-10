/**
 * @file startup_loader.h
 * @brief Startup recovery system (Epic 3: Story 3.1)
 */

#ifndef MIR2_PERSISTENCE_STARTUP_LOADER_H
#define MIR2_PERSISTENCE_STARTUP_LOADER_H

#include "persistence/storage_backend.h"
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <tuple>
#include <vector>

namespace mir2::persistence {

/**
 * @brief Startup recovery system (Story 3.1)
 *
 * Responsibilities:
 * - Load latest snapshot from database
 * - Validate snapshot integrity
 * - Reconstruct EnTT registry
 * - Handle fallback to previous snapshot on failure
 * - Measure recovery time
 *
 * Single-threaded (called once at server startup).
 */
class StartupLoader {
 public:
    /**
     * @brief Recovery result
     */
    struct RecoveryResult {
        bool success;
        std::string error_message;
        std::map<uint64_t, std::tuple<std::string, std::vector<uint8_t>, uint32_t>> entities;
        uint64_t recovered_entity_count = 0;
        int64_t recovery_duration_ms = 0;
        std::string last_snapshot_id;
    };

    /**
     * @brief Create startup loader
     */
    explicit StartupLoader(std::shared_ptr<IStorageBackend> backend);

    /**
     * @brief Perform full startup recovery
     * @return Recovery result with entities and status
     * @note noexcept: errors returned via RecoveryResult, no exceptions thrown
     */
    RecoveryResult PerformRecovery() noexcept;

    /**
     * @brief Perform recovery with fallback strategy
     *
     * Attempts to load latest snapshot, falls back to previous on failure.
     * @note noexcept: errors returned via RecoveryResult, no exceptions thrown
     */
    RecoveryResult PerformRecoveryWithFallback() noexcept;

 private:
    std::shared_ptr<IStorageBackend> backend_;

    /**
     * @brief Validate snapshot integrity
     * @note noexcept: validation never throws
     */
    bool ValidateSnapshot(const SnapshotMetadata& metadata) noexcept;
};

}  // namespace mir2::persistence

#endif  // MIR2_PERSISTENCE_STARTUP_LOADER_H
