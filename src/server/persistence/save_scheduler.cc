/**
 * @file save_scheduler.cc
 * @brief Save scheduler implementation
 */

#include "persistence/save_scheduler.h"
#include "log/logger.h"
#include <algorithm>

namespace mir2::persistence {

SaveScheduler::SaveScheduler(
    uint32_t save_interval_seconds,
    std::function<std::vector<uint64_t>()> get_all_entities,
    std::function<void(const std::vector<uint64_t>&)> save_entities)
    : save_interval_seconds_(save_interval_seconds),
      get_all_entities_(get_all_entities),
      save_entities_(save_entities),
      last_save_time_(std::chrono::steady_clock::now()) {
}

SaveScheduler::~SaveScheduler() {
    Stop();
}

void SaveScheduler::Start() {
    if (is_running_.exchange(true)) {
        SYSLOG_WARN("SaveScheduler already running");
        return;
    }

    scheduler_thread_ = std::make_unique<std::thread>([this] { SchedulerLoop(); });
    SYSLOG_INFO("SaveScheduler started (interval: {} seconds)", save_interval_seconds_);
}

void SaveScheduler::Stop() {
    if (!is_running_.exchange(false)) {
        return;
    }

    if (scheduler_thread_ && scheduler_thread_->joinable()) {
        scheduler_thread_->join();
    }

    SYSLOG_INFO("SaveScheduler stopped");
}

void SaveScheduler::SaveNow() {
    if (!is_running_) {
        SYSLOG_WARN("SaveScheduler not running, cannot save");
        return;
    }

    PerformSave();
}

void SaveScheduler::SchedulerLoop() {
    SYSLOG_DEBUG("SaveScheduler loop started");

    while (is_running_) {
        // Wait for interval or stop signal
        auto next_save = last_save_time_ + std::chrono::seconds(save_interval_seconds_);
        auto now = std::chrono::steady_clock::now();
        auto sleep_duration = next_save - now;

        if (sleep_duration.count() > 0) {
            std::this_thread::sleep_for(std::min(
                sleep_duration,
                std::chrono::milliseconds(100)  // Check is_running every 100ms
            ));
        }

        if (is_running_) {
            PerformSave();
        }
    }

    SYSLOG_DEBUG("SaveScheduler loop ended");
}

void SaveScheduler::PerformSave() {
    auto save_start = std::chrono::steady_clock::now();

    try {
        if (!get_all_entities_ || !save_entities_) {
            SYSLOG_WARN("Save callbacks not set, skipping save");
            return;
        }

        // Get all entities to save
        auto entities = get_all_entities_();
        if (entities.empty()) {
            SYSLOG_DEBUG("No entities to save");
            {
                std::lock_guard<std::mutex> lock(stats_mutex_);
                stats_.total_saves++;
                last_save_time_ = std::chrono::steady_clock::now();
            }
            return;
        }

        // Report progress
        if (progress_callback_) {
            progress_callback_(SaveProgress{
                .entities_saved = 0,
                .total_entities = static_cast<uint64_t>(entities.size()),
                .elapsed_ms = 0
            });
        }

        // Perform save
        save_entities_(entities);

        // Report completion
        auto save_duration = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - save_start);

        if (progress_callback_) {
            progress_callback_(SaveProgress{
                .entities_saved = static_cast<uint64_t>(entities.size()),
                .total_entities = static_cast<uint64_t>(entities.size()),
                .elapsed_ms = save_duration.count()
            });
        }

        {
            std::lock_guard<std::mutex> lock(stats_mutex_);
            stats_.total_saves++;
            stats_.last_save_duration_ms = save_duration.count();
            stats_.last_save_entity_count = entities.size();
            last_save_time_ = std::chrono::steady_clock::now();
        }

        SYSLOG_INFO("Auto-save completed: {} entities in {}ms",
                    entities.size(), save_duration.count());
    } catch (const std::exception& e) {
        {
            std::lock_guard<std::mutex> lock(stats_mutex_);
            stats_.failed_saves++;
        }
        SYSLOG_ERROR("Save failed: {}", e.what());
    }
}

}  // namespace mir2::persistence
