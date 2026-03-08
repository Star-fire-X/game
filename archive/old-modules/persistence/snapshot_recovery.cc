/**
 * @file snapshot_recovery.cc
 * @brief Automatic snapshot recovery implementation
 */

#include "persistence/snapshot_recovery.h"
#include "log/logger.h"
#include <sstream>
#include <iomanip>
#include <functional>

namespace mir2::persistence {

SnapshotRecovery::SnapshotRecovery(std::shared_ptr<IStorageBackend> backend)
    : backend_(std::move(backend)),
      validator_(std::make_shared<DataValidator>()) {
}

SnapshotRecovery::RecoveryAttempt SnapshotRecovery::RecoverWithAutomaticFallback(
    int max_fallback_attempts) {

    RecoveryAttempt attempt{
        .success = false,
        .snapshot_id = 0,
        .error_message = "",
        .entities = {},
        .fallback_attempts = 0
    };

    auto snapshots = GetSnapshotHistory();
    if (snapshots.empty()) {
        attempt.error_message = "No snapshots found";
        SYSLOG_WARN("Recovery: {}", attempt.error_message);
        // Empty state is valid for fresh start
        attempt.success = true;
        return attempt;
    }

    for (const auto& snapshot_info : snapshots) {
        if (attempt.fallback_attempts >= max_fallback_attempts) {
            attempt.error_message =
                "Exceeded maximum fallback attempts (" + std::to_string(max_fallback_attempts) + ")";
            break;
        }

        SYSLOG_INFO("Recovery attempt {} - snapshot_id: {}",
                    attempt.fallback_attempts + 1,
                    snapshot_info.snapshot_id);

        // Try to recover from this snapshot
        if (ValidateAndLoadSnapshot(snapshot_info.snapshot_id, attempt.entities)) {
            attempt.success = true;
            attempt.snapshot_id = snapshot_info.snapshot_id;
            SYSLOG_INFO("Recovery SUCCESS from snapshot {} ({} entities)",
                        snapshot_info.snapshot_id, attempt.entities.size());
            return attempt;
        }

        // Mark this snapshot as corrupted (would need backend support)
        SYSLOG_WARN("Snapshot {} validation failed, trying next...", snapshot_info.snapshot_id);
        attempt.fallback_attempts++;
    }

    // All snapshots failed
    if (!attempt.success) {
        attempt.error_message = "All " + std::to_string(snapshots.size()) + " snapshots failed validation";
        SYSLOG_ERROR("Recovery FAILED: {}", attempt.error_message);
    }

    return attempt;
}

std::vector<SnapshotInfo> SnapshotRecovery::GetSnapshotHistory() {
    std::vector<SnapshotInfo> history;

    // Get latest snapshot and work backwards
    auto result = backend_->GetLatestSnapshot();
    if (!result.success || !result.value) {
        return history;
    }

    auto& metadata = result.value.value();
    SnapshotInfo info{
        .snapshot_id = metadata.snapshot_id,
        .name = "",
        .entity_count = static_cast<int>(metadata.entity_count),
        .status = "valid",
        .timestamp = std::chrono::system_clock::now()  // Would need proper timestamp conversion
    };
    history.push_back(info);

    // Note: Full implementation would query snapshot_history table for multiple snapshots
    // For now, we only have the latest snapshot available via the interface

    return history;
}

bool SnapshotRecovery::ValidateAndLoadSnapshot(
    uint64_t snapshot_id,
    std::map<uint64_t, std::tuple<std::string, std::vector<uint8_t>, uint32_t>>& out_entities) {

    try {
        // Load all entities
        auto entities_result = backend_->LoadAllEntities();
        if (!entities_result.success) {
            SYSLOG_ERROR("Failed to load entities from snapshot {}: {}",
                        snapshot_id, entities_result.error_message);
            return false;
        }

        // Validate each entity using DataValidator
        for (const auto& [entity_id, entity_data] : entities_result.value) {
            const auto& [entity_type, data, version] = entity_data;

            auto validation = validator_->ValidateEntity(entity_id, entity_type, data);
            if (!validation.is_valid) {
                SYSLOG_ERROR("Entity {} validation failed: {}",
                            entity_id, validation.error_message);
                return false;
            }
        }

        // Calculate and verify checksum
        std::string calculated_checksum = CalculateSnapshotChecksum(entities_result.value);
        // Note: Would compare against stored checksum in full implementation

        out_entities = entities_result.value;
        SYSLOG_INFO("Snapshot {} validation passed, {} entities loaded",
                   snapshot_id, out_entities.size());
        return true;

    } catch (const std::exception& e) {
        SYSLOG_ERROR("Exception during snapshot validation: {}", e.what());
        return false;
    }
}

std::string SnapshotRecovery::CalculateSnapshotChecksum(
    const std::map<uint64_t, std::tuple<std::string, std::vector<uint8_t>, uint32_t>>& entities) {

    // Simple checksum based on entity count and data sizes
    // Full implementation would use SHA256
    size_t total_size = 0;
    size_t entity_count = entities.size();

    for (const auto& [entity_id, entity_data] : entities) {
        const auto& [entity_type, data, version] = entity_data;
        total_size += data.size();
    }

    std::ostringstream ss;
    ss << std::hex << std::setfill('0')
       << std::setw(8) << entity_count
       << std::setw(16) << total_size;

    return ss.str();
}

}  // namespace mir2::persistence
