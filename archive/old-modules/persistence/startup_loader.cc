/**
 * @file startup_loader.cc
 * @brief Startup loader implementation
 */

#include "persistence/startup_loader.h"
#include "persistence/snapshot_recovery.h"
#include "log/logger.h"
#include <chrono>

namespace mir2::persistence {

StartupLoader::StartupLoader(std::shared_ptr<IStorageBackend> backend)
    : backend_(backend) {
}

StartupLoader::RecoveryResult StartupLoader::PerformRecovery() noexcept {
    auto recovery_start = std::chrono::steady_clock::now();
    RecoveryResult result{
        .success = false,
        .error_message = "",
        .entities = {},
        .recovered_entity_count = 0,
        .recovery_duration_ms = 0,
        .last_snapshot_id = ""
    };

    try {
        if (!backend_ || !backend_->IsReady()) {
            result.error_message = "Backend not ready";
            return result;
        }

        // Get latest snapshot metadata
        auto snapshot_result = backend_->GetLatestSnapshot();
        if (!snapshot_result.success) {
            result.error_message = "Failed to retrieve snapshot metadata";
            SYSLOG_WARN("Startup recovery: {}", result.error_message);
            return result;
        }

        if (!snapshot_result.value) {
            SYSLOG_WARN("No snapshots found, starting with empty state");
            result.success = true;
            result.recovered_entity_count = 0;
            result.recovery_duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - recovery_start).count();
            return result;
        }

        auto& metadata = snapshot_result.value.value();
        SYSLOG_INFO("Loading snapshot {} (entities: {}, checksum: {})",
                    metadata.snapshot_id, metadata.entity_count, metadata.checksum);

        // Validate snapshot
        if (!ValidateSnapshot(metadata)) {
            result.error_message = "Snapshot validation failed";
            SYSLOG_WARN("Startup recovery: {}", result.error_message);
            return result;
        }

        // Load all entities
        auto entities_result = backend_->LoadAllEntities();
        if (!entities_result.success) {
            result.error_message = "Failed to load entities";
            return result;
        }

        result.entities = entities_result.value;
        result.recovered_entity_count = entities_result.value.size();
        result.last_snapshot_id = std::to_string(metadata.snapshot_id);
        result.success = true;

        auto recovery_duration = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - recovery_start);
        result.recovery_duration_ms = recovery_duration.count();

        SYSLOG_INFO("Startup recovery complete: {} entities loaded in {}ms",
                    result.recovered_entity_count, result.recovery_duration_ms);

        return result;
    } catch (const std::exception& e) {
        result.error_message = std::string("Exception during recovery: ") + e.what();
        SYSLOG_ERROR("{}", result.error_message);
        return result;
    }
}

StartupLoader::RecoveryResult StartupLoader::PerformRecoveryWithFallback() noexcept {
    auto recovery_start = std::chrono::steady_clock::now();

    try {
        // Use SnapshotRecovery for automatic fallback
        SnapshotRecovery recovery(backend_);
        auto attempt = recovery.RecoverWithAutomaticFallback(5);

        RecoveryResult result{
            .success = attempt.success,
            .error_message = attempt.error_message,
            .entities = attempt.entities,
            .recovered_entity_count = attempt.entities.size(),
            .recovery_duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - recovery_start).count(),
            .last_snapshot_id = std::to_string(attempt.snapshot_id)
        };

        if (result.success) {
            SYSLOG_INFO("Recovery with fallback complete: {} entities from snapshot {} ({}ms, {} fallback attempts)",
                        result.recovered_entity_count, result.last_snapshot_id,
                        result.recovery_duration_ms, attempt.fallback_attempts);
        } else {
            SYSLOG_ERROR("Recovery with fallback failed after {} attempts: {}",
                        attempt.fallback_attempts, result.error_message);
        }

        return result;
    } catch (const std::exception& e) {
        RecoveryResult result{
            .success = false,
            .error_message = std::string("Exception during recovery: ") + e.what(),
            .entities = {},
            .recovered_entity_count = 0,
            .recovery_duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - recovery_start).count(),
            .last_snapshot_id = ""
        };
        SYSLOG_ERROR("{}", result.error_message);
        return result;
    }
}

bool StartupLoader::ValidateSnapshot(const SnapshotMetadata& metadata) noexcept {
    try {
        // Basic validation
        if (metadata.entity_count == 0 && metadata.component_count == 0) {
            // Empty snapshot is valid for fresh start
            return true;
        }

        // Validate checksum exists
        if (metadata.checksum.empty()) {
            SYSLOG_WARN("Snapshot {} missing checksum", metadata.snapshot_id);
            return false;
        }

        SYSLOG_DEBUG("Snapshot {} validation passed", metadata.snapshot_id);
        return true;
    } catch (...) {
        return false;
    }
}

}  // namespace mir2::persistence
