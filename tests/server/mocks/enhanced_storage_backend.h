/**
 * @file enhanced_storage_backend.h
 * @brief Enhanced mock storage backend with failure injection capabilities
 *
 * Provides comprehensive testing support for:
 * - Backend unavailability simulation
 * - Save/load failure scenarios
 * - Partial batch failures
 * - Latency simulation
 * - Data corruption simulation
 * - Thread-safe operation tracking
 */

#ifndef MIR2_TEST_ENHANCED_STORAGE_BACKEND_H
#define MIR2_TEST_ENHANCED_STORAGE_BACKEND_H

#include "persistence/storage_backend.h"
#include <atomic>
#include <chrono>
#include <map>
#include <mutex>
#include <random>
#include <thread>
#include <vector>

namespace mir2::persistence::test {

/**
 * @brief Failure injection configuration
 */
struct FailureConfig {
    // Backend availability
    bool backend_ready = true;

    // Failure rates (0.0 - 1.0)
    double save_failure_rate = 0.0;
    double load_failure_rate = 0.0;
    double batch_partial_failure_rate = 0.0;

    // Latency simulation (milliseconds)
    uint32_t min_latency_ms = 0;
    uint32_t max_latency_ms = 0;

    // Data corruption
    bool corrupt_data = false;
    double corruption_rate = 0.0;

    // Specific entity failures
    std::vector<uint64_t> failing_entity_ids;

    // Snapshot failures
    bool snapshot_creation_fails = false;
    bool snapshot_load_fails = false;
};

/**
 * @brief Enhanced mock storage backend with comprehensive failure injection
 */
class EnhancedStorageBackend : public IStorageBackend {
 public:
    EnhancedStorageBackend() : rng_(std::random_device{}()) {}

    ~EnhancedStorageBackend() override = default;

    // ============================================
    // IStorageBackend Implementation
    // ============================================

    StorageResult<> SaveEntity(
        uint64_t entity_id,
        const std::string& entity_type,
        const std::vector<uint8_t>& data,
        uint32_t version) noexcept override {

        std::lock_guard<std::mutex> lock(mutex_);

        if (!config_.backend_ready) {
            return StorageResult<>::Failure("Backend not ready");
        }

        SimulateLatency();

        // Check specific entity failure
        if (std::find(config_.failing_entity_ids.begin(),
                     config_.failing_entity_ids.end(),
                     entity_id) != config_.failing_entity_ids.end()) {
            stats_.failed_saves++;
            return StorageResult<>::Failure("Specific entity save failure");
        }

        // Random failure injection
        if (ShouldFail(config_.save_failure_rate)) {
            stats_.failed_saves++;
            return StorageResult<>::Failure("Injected save failure");
        }

        // Store entity data
        auto data_to_store = data;
        if (config_.corrupt_data && ShouldFail(config_.corruption_rate)) {
            CorruptData(data_to_store);
            stats_.corrupted_entities++;
        }

        entities_[entity_id] = EntityData{
            .entity_type = entity_type,
            .data = data_to_store,
            .version = version,
            .save_timestamp = std::chrono::system_clock::now()
        };

        stats_.total_saves++;
        stats_.total_bytes_written += data.size();

        return StorageResult<>::Success();
    }

    StorageResult<> SaveEntitiesBatch(
        const std::vector<std::tuple<uint64_t, std::string, std::vector<uint8_t>, uint32_t>>& entities) noexcept override {

        std::lock_guard<std::mutex> lock(mutex_);

        if (!config_.backend_ready) {
            return StorageResult<>::Failure("Backend not ready");
        }

        SimulateLatency();

        if (entities.empty()) {
            stats_.total_batch_saves++;
            return StorageResult<>::Success();
        }

        // Simulate partial batch failure
        size_t failed_count = 0;
        for (const auto& [id, type, data, version] : entities) {
            if (ShouldFail(config_.batch_partial_failure_rate)) {
                failed_count++;
                stats_.failed_saves++;
                continue;
            }

            auto data_copy = data;
            if (config_.corrupt_data && ShouldFail(config_.corruption_rate)) {
                CorruptData(data_copy);
                stats_.corrupted_entities++;
            }

            entities_[id] = EntityData{
                .entity_type = type,
                .data = data_copy,
                .version = version,
                .save_timestamp = std::chrono::system_clock::now()
            };

            stats_.total_saves++;
            stats_.total_bytes_written += data.size();
        }

        stats_.total_batch_saves++;

        if (failed_count > 0) {
            return StorageResult<>::Failure(
                "Partial batch failure: " + std::to_string(failed_count) + " entities failed");
        }

        return StorageResult<>::Success();
    }

    StorageResult<std::pair<std::vector<uint8_t>, uint32_t>> LoadEntity(
        uint64_t entity_id) noexcept override {

        std::lock_guard<std::mutex> lock(mutex_);

        if (!config_.backend_ready) {
            return StorageResult<std::pair<std::vector<uint8_t>, uint32_t>>::Failure(
                "Backend not ready");
        }

        SimulateLatency();

        if (ShouldFail(config_.load_failure_rate)) {
            stats_.failed_loads++;
            return StorageResult<std::pair<std::vector<uint8_t>, uint32_t>>::Failure(
                "Injected load failure");
        }

        auto it = entities_.find(entity_id);
        if (it == entities_.end()) {
            stats_.failed_loads++;
            return StorageResult<std::pair<std::vector<uint8_t>, uint32_t>>::Failure(
                "Entity not found");
        }

        stats_.total_loads++;
        stats_.total_bytes_read += it->second.data.size();

        return StorageResult<std::pair<std::vector<uint8_t>, uint32_t>>::Success(
            std::make_pair(it->second.data, it->second.version));
    }

    StorageResult<std::map<uint64_t, std::pair<std::vector<uint8_t>, uint32_t>>>
    LoadEntitiesBatch(const std::vector<uint64_t>& entity_ids) noexcept override {

        std::lock_guard<std::mutex> lock(mutex_);

        if (!config_.backend_ready) {
            return StorageResult<std::map<uint64_t, std::pair<std::vector<uint8_t>, uint32_t>>>::Failure(
                "Backend not ready");
        }

        SimulateLatency();

        std::map<uint64_t, std::pair<std::vector<uint8_t>, uint32_t>> result;

        for (uint64_t id : entity_ids) {
            auto it = entities_.find(id);
            if (it != entities_.end()) {
                result[id] = std::make_pair(it->second.data, it->second.version);
                stats_.total_loads++;
                stats_.total_bytes_read += it->second.data.size();
            }
        }

        return StorageResult<std::map<uint64_t, std::pair<std::vector<uint8_t>, uint32_t>>>::Success(result);
    }

    StorageResult<std::map<uint64_t, std::tuple<std::string, std::vector<uint8_t>, uint32_t>>>
    LoadAllEntities() noexcept override {

        std::lock_guard<std::mutex> lock(mutex_);

        if (!config_.backend_ready) {
            return StorageResult<std::map<uint64_t, std::tuple<std::string, std::vector<uint8_t>, uint32_t>>>::Failure(
                "Backend not ready");
        }

        SimulateLatency();

        std::map<uint64_t, std::tuple<std::string, std::vector<uint8_t>, uint32_t>> result;

        for (const auto& [id, entity_data] : entities_) {
            result[id] = std::make_tuple(
                entity_data.entity_type,
                entity_data.data,
                entity_data.version);
            stats_.total_loads++;
            stats_.total_bytes_read += entity_data.data.size();
        }

        return StorageResult<std::map<uint64_t, std::tuple<std::string, std::vector<uint8_t>, uint32_t>>>::Success(result);
    }

    StorageResult<> DeleteEntity(uint64_t entity_id) noexcept override {
        std::lock_guard<std::mutex> lock(mutex_);

        if (!config_.backend_ready) {
            return StorageResult<>::Failure("Backend not ready");
        }

        auto it = entities_.find(entity_id);
        if (it == entities_.end()) {
            return StorageResult<>::Failure("Entity not found");
        }

        entities_.erase(it);
        stats_.total_deletes++;

        return StorageResult<>::Success();
    }

    StorageResult<SnapshotMetadata> CreateSnapshot(
        const std::string& snapshot_name) noexcept override {

        std::lock_guard<std::mutex> lock(mutex_);

        if (!config_.backend_ready || config_.snapshot_creation_fails) {
            return StorageResult<SnapshotMetadata>::Failure("Snapshot creation failed");
        }

        SimulateLatency();

        SnapshotMetadata metadata{
            .snapshot_id = next_snapshot_id_++,
            .timestamp = static_cast<uint64_t>(
                std::chrono::system_clock::now().time_since_epoch().count()),
            .entity_count = static_cast<uint32_t>(entities_.size()),
            .component_count = static_cast<uint32_t>(entities_.size()),
            .version = "1.0",
            .checksum = "mock_checksum_" + std::to_string(next_snapshot_id_ - 1)
        };

        snapshots_.push_back(metadata);
        stats_.total_snapshots_created++;

        return StorageResult<SnapshotMetadata>::Success(metadata);
    }

    StorageResult<std::optional<SnapshotMetadata>> GetLatestSnapshot() noexcept override {
        std::lock_guard<std::mutex> lock(mutex_);

        if (!config_.backend_ready || config_.snapshot_load_fails) {
            return StorageResult<std::optional<SnapshotMetadata>>::Failure(
                "Failed to get latest snapshot");
        }

        if (snapshots_.empty()) {
            return StorageResult<std::optional<SnapshotMetadata>>::Success(std::nullopt);
        }

        return StorageResult<std::optional<SnapshotMetadata>>::Success(snapshots_.back());
    }

    StorageResult<std::vector<SnapshotMetadata>> GetSnapshotHistory(
        int limit = 10) noexcept override {

        std::lock_guard<std::mutex> lock(mutex_);

        if (!config_.backend_ready) {
            return StorageResult<std::vector<SnapshotMetadata>>::Failure(
                "Backend not ready");
        }

        std::vector<SnapshotMetadata> result;
        int count = std::min(limit, static_cast<int>(snapshots_.size()));

        for (int i = 0; i < count; ++i) {
            result.push_back(snapshots_[snapshots_.size() - 1 - i]);
        }

        return StorageResult<std::vector<SnapshotMetadata>>::Success(result);
    }

    StorageResult<std::map<uint64_t, std::tuple<std::string, std::vector<uint8_t>, uint32_t>>>
    LoadAllEntitiesFromSnapshot(uint64_t snapshot_id) noexcept override {

        std::lock_guard<std::mutex> lock(mutex_);

        if (!config_.backend_ready || config_.snapshot_load_fails) {
            return StorageResult<std::map<uint64_t, std::tuple<std::string, std::vector<uint8_t>, uint32_t>>>::Failure(
                "Failed to load snapshot");
        }

        // For mock, just return current entities
        return LoadAllEntities();
    }

    StorageResult<> MarkSnapshotCorrupted(
        uint64_t snapshot_id,
        const std::string& reason) noexcept override {

        std::lock_guard<std::mutex> lock(mutex_);

        if (!config_.backend_ready) {
            return StorageResult<>::Failure("Backend not ready");
        }

        stats_.corrupted_snapshots++;
        return StorageResult<>::Success();
    }

    bool IsReady() noexcept override {
        std::lock_guard<std::mutex> lock(mutex_);
        return config_.backend_ready;
    }

    const char* GetName() const noexcept override {
        return "EnhancedMockBackend";
    }

    // ============================================
    // Test Utilities
    // ============================================

    /**
     * @brief Configure failure injection
     */
    void SetFailureConfig(const FailureConfig& config) {
        std::lock_guard<std::mutex> lock(mutex_);
        config_ = config;
    }

    /**
     * @brief Get current failure configuration
     */
    FailureConfig GetFailureConfig() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return config_;
    }

    /**
     * @brief Backend statistics
     */
    struct Statistics {
        uint64_t total_saves = 0;
        uint64_t failed_saves = 0;
        uint64_t total_loads = 0;
        uint64_t failed_loads = 0;
        uint64_t total_deletes = 0;
        uint64_t total_batch_saves = 0;
        uint64_t total_bytes_written = 0;
        uint64_t total_bytes_read = 0;
        uint64_t corrupted_entities = 0;
        uint64_t corrupted_snapshots = 0;
        uint64_t total_snapshots_created = 0;
    };

    /**
     * @brief Get backend statistics
     */
    Statistics GetStatistics() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return stats_;
    }

    /**
     * @brief Reset statistics
     */
    void ResetStatistics() {
        std::lock_guard<std::mutex> lock(mutex_);
        stats_ = Statistics{};
    }

    /**
     * @brief Get stored entity count
     */
    size_t GetEntityCount() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return entities_.size();
    }

    /**
     * @brief Check if entity exists
     */
    bool HasEntity(uint64_t entity_id) const {
        std::lock_guard<std::mutex> lock(mutex_);
        return entities_.find(entity_id) != entities_.end();
    }

    /**
     * @brief Clear all stored entities
     */
    void ClearEntities() {
        std::lock_guard<std::mutex> lock(mutex_);
        entities_.clear();
    }

    /**
     * @brief Clear all snapshots
     */
    void ClearSnapshots() {
        std::lock_guard<std::mutex> lock(mutex_);
        snapshots_.clear();
    }

 private:
    struct EntityData {
        std::string entity_type;
        std::vector<uint8_t> data;
        uint32_t version;
        std::chrono::system_clock::time_point save_timestamp;
    };

    mutable std::mutex mutex_;
    FailureConfig config_;
    Statistics stats_;
    std::map<uint64_t, EntityData> entities_;
    std::vector<SnapshotMetadata> snapshots_;
    uint64_t next_snapshot_id_ = 1;
    std::mt19937 rng_;

    /**
     * @brief Determine if operation should fail based on rate
     */
    bool ShouldFail(double failure_rate) {
        if (failure_rate <= 0.0) return false;
        if (failure_rate >= 1.0) return true;

        std::uniform_real_distribution<double> dist(0.0, 1.0);
        return dist(rng_) < failure_rate;
    }

    /**
     * @brief Simulate latency
     */
    void SimulateLatency() {
        if (config_.min_latency_ms > 0 || config_.max_latency_ms > 0) {
            std::uniform_int_distribution<uint32_t> dist(
                config_.min_latency_ms, config_.max_latency_ms);
            std::this_thread::sleep_for(std::chrono::milliseconds(dist(rng_)));
        }
    }

    /**
     * @brief Corrupt data for testing
     */
    void CorruptData(std::vector<uint8_t>& data) {
        if (data.empty()) return;

        std::uniform_int_distribution<size_t> index_dist(0, data.size() - 1);
        std::uniform_int_distribution<uint8_t> byte_dist(0, 255);

        // Corrupt random byte
        size_t corrupt_index = index_dist(rng_);
        data[corrupt_index] = byte_dist(rng_);
    }
};

}  // namespace mir2::persistence::test

#endif  // MIR2_TEST_ENHANCED_STORAGE_BACKEND_H
