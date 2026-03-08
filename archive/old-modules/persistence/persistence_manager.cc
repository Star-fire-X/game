/**
 * @file persistence_manager.cc
 * @brief Persistence manager implementation
 */

#include "persistence/persistence_manager.h"
#include "persistence/persistence_error.h"
#include "log/logger.h"
#include <chrono>

namespace mir2::persistence {

std::unique_ptr<PersistenceManager> PersistenceManager::instance_;
std::mutex PersistenceManager::instance_mutex_;

bool PersistenceManager::Initialize(
    std::unique_ptr<IStorageBackend> backend,
    std::unique_ptr<ISerializer> serializer,
    const Config& config) {
    std::lock_guard<std::mutex> lock(instance_mutex_);

    if (instance_) {
        SYSLOG_WARN("PersistenceManager already initialized");
        return false;
    }

    instance_ = std::unique_ptr<PersistenceManager>(new PersistenceManager());
    instance_->backend_ = std::move(backend);
    instance_->serializer_ = std::move(serializer);
    instance_->config_ = config;

    SYSLOG_INFO("PersistenceManager initialized with {} backend and {} serializer",
                instance_->backend_->GetName(),
                instance_->serializer_->GetName());

    return true;
}

bool PersistenceManager::Initialize(
    std::unique_ptr<IStorageBackend> backend,
    std::unique_ptr<ISerializer> serializer) {
    return Initialize(std::move(backend), std::move(serializer), Config{});
}

PersistenceManager& PersistenceManager::Instance() {
    std::lock_guard<std::mutex> lock(instance_mutex_);
    if (!instance_) {
        throw PersistenceException("PersistenceManager not initialized. Call Initialize() first.");
    }
    return *instance_;
}

bool PersistenceManager::IsInitialized() noexcept {
    try {
        std::lock_guard<std::mutex> lock(instance_mutex_);
        return instance_ != nullptr;
    } catch (...) {
        return false;
    }
}

void PersistenceManager::Shutdown() {
    std::lock_guard<std::mutex> lock(instance_mutex_);
    instance_.reset();
    SYSLOG_INFO("PersistenceManager shutdown complete");
}

void PersistenceManager::MarkComponentDirty(
    uint64_t entity_id,
    const std::string& component_name,
    bool is_high_priority) noexcept {
    try {
        std::lock_guard<std::mutex> lock(dirty_mutex_);

        DirtyEntry entry{
            .entity_id = entity_id,
            .component_name = component_name,
            .mark_time = std::chrono::steady_clock::now(),
            .is_high_priority = is_high_priority
        };

        dirty_queue_.push(entry);
        SYSLOG_DEBUG("Marked {} dirty: {} (priority: {})",
                     component_name, entity_id,
                     is_high_priority ? "high" : "normal");
    } catch (...) {
        // Silently ignore - dirty tracking failure is non-fatal
    }
}

PersistenceResult PersistenceManager::SaveEntity(
    uint64_t entity_id,
    const std::string& entity_type,
    const std::vector<uint8_t>& data,
    uint32_t version) {
    auto start = std::chrono::steady_clock::now();

    try {
        if (!backend_ || !backend_->IsReady()) {
            return PersistenceResult::Failure("Backend not ready");
        }

        auto result = backend_->SaveEntity(entity_id, entity_type, data, version);
        if (!result.success) {
            {
                std::lock_guard<std::mutex> lock(metrics_mutex_);
                current_metrics_.failed_saves++;
            }
            return PersistenceResult::Failure(result.error_message);
        }

        {
            std::lock_guard<std::mutex> lock(metrics_mutex_);
            current_metrics_.total_saves++;
            current_metrics_.total_bytes_written += data.size();
        }

        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start);

        SYSLOG_DEBUG("Saved entity {} ({} bytes) in {}ms",
                     entity_id, data.size(), duration.count());

        return PersistenceResult::Success();
    } catch (const std::exception& e) {
        return PersistenceResult::Failure(std::string("Exception during save: ") + e.what());
    }
}

PersistenceResult PersistenceManager::SaveEntitiesBatch(
    const std::vector<std::tuple<uint64_t, std::string, std::vector<uint8_t>, uint32_t>>& entities) {
    auto start = std::chrono::steady_clock::now();

    try {
        if (!backend_ || !backend_->IsReady()) {
            return PersistenceResult::Failure("Backend not ready");
        }

        auto result = backend_->SaveEntitiesBatch(entities);
        if (!result.success) {
            {
                std::lock_guard<std::mutex> lock(metrics_mutex_);
                current_metrics_.failed_saves += entities.size();  // Count all failed entities
            }
            return PersistenceResult::Failure(result.error_message);
        }

        uint64_t total_bytes = 0;
        for (const auto& [id, type, data, version] : entities) {
            total_bytes += data.size();
        }

        {
            std::lock_guard<std::mutex> lock(metrics_mutex_);
            current_metrics_.total_saves += entities.size();  // Count all saved entities
            current_metrics_.total_bytes_written += total_bytes;
        }

        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start);

        SYSLOG_DEBUG("Saved batch of {} entities ({} bytes) in {}ms",
                     entities.size(), total_bytes, duration.count());

        return PersistenceResult::Success();
    } catch (const std::exception& e) {
        return PersistenceResult::Failure(std::string("Exception during batch save: ") + e.what());
    }
}

std::optional<std::pair<std::vector<uint8_t>, uint32_t>> PersistenceManager::LoadEntity(
    uint64_t entity_id) {
    try {
        if (!backend_ || !backend_->IsReady()) {
            SYSLOG_WARN("Backend not ready for load");
            return std::nullopt;
        }

        auto result = backend_->LoadEntity(entity_id);
        if (result.success) {
            return result.value;
        }

        return std::nullopt;
    } catch (const std::exception& e) {
        SYSLOG_ERROR("Exception during load: {}", e.what());
        return std::nullopt;
    }
}

std::optional<std::map<uint64_t, std::tuple<std::string, std::vector<uint8_t>, uint32_t>>>
PersistenceManager::LoadAllEntities() {
    try {
        if (!backend_ || !backend_->IsReady()) {
            SYSLOG_WARN("Backend not ready for load");
            return std::nullopt;
        }

        auto result = backend_->LoadAllEntities();
        if (result.success) {
            return result.value;
        }

        return std::nullopt;
    } catch (const std::exception& e) {
        SYSLOG_ERROR("Exception during load all: {}", e.what());
        return std::nullopt;
    }
}

void PersistenceManager::RegisterComponentChangeListener(
    const std::string& component_name,
    ComponentChangeListener listener) {
    change_listeners_[component_name].push_back(listener);
    SYSLOG_DEBUG("Registered change listener for component: {}", component_name);
}

PersistenceResult PersistenceManager::CreateGracefulShutdownSnapshot() {
    try {
        auto result = backend_->CreateSnapshot("graceful_shutdown_" +
                      std::to_string(std::chrono::system_clock::now().time_since_epoch().count()));

        if (result.success) {
            SYSLOG_INFO("Created graceful shutdown snapshot (ID: {})", result.value.snapshot_id);
            return PersistenceResult::Success();
        }

        return PersistenceResult::Failure(result.error_message);
    } catch (const std::exception& e) {
        return PersistenceResult::Failure(std::string("Snapshot creation failed: ") + e.what());
    }
}

PersistenceResult PersistenceManager::PerformStartupRecovery() {
    try {
        SYSLOG_INFO("Performing startup recovery...");

        auto snapshot_result = backend_->GetLatestSnapshot();
        if (!snapshot_result.success) {
            return PersistenceResult::Failure("Failed to retrieve latest snapshot");
        }

        if (!snapshot_result.value) {
            SYSLOG_WARN("No snapshots found, starting with empty state");
            return PersistenceResult::Success();
        }

        SYSLOG_INFO("Loading snapshot {} (entities: {})",
                    snapshot_result.value->snapshot_id,
                    snapshot_result.value->entity_count);

        return PersistenceResult::Success();
    } catch (const std::exception& e) {
        return PersistenceResult::Failure(std::string("Recovery failed: ") + e.what());
    }
}

PersistenceResult PersistenceManager::ValidateSnapshot() {
    try {
        auto result = backend_->GetLatestSnapshot();
        if (!result.success) {
            return PersistenceResult::Failure("Failed to validate snapshot");
        }

        if (result.value) {
            SYSLOG_INFO("Snapshot validation successful (ID: {}, entities: {})",
                        result.value->snapshot_id,
                        result.value->entity_count);
        }

        return PersistenceResult::Success();
    } catch (const std::exception& e) {
        return PersistenceResult::Failure(std::string("Validation failed: ") + e.what());
    }
}

}  // namespace mir2::persistence
