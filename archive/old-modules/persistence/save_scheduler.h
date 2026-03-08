/**
 * @file save_scheduler.h
 * @brief Automatic save scheduler (Epic 2: Story 2.1)
 */

#ifndef MIR2_PERSISTENCE_SAVE_SCHEDULER_H
#define MIR2_PERSISTENCE_SAVE_SCHEDULER_H

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace mir2::persistence {

/**
 * @brief Save progress callback
 */
struct SaveProgress {
    uint64_t entities_saved;
    uint64_t total_entities;
    int64_t elapsed_ms;
};

using SaveProgressCallback = std::function<void(const SaveProgress&)>;

/**
 * @brief Automatic save scheduler (Story 2.1)
 *
 * Features:
 * - Configurable save interval (default 5 minutes)
 * - Non-blocking async saves
 * - Progress tracking
 * - Performance metrics
 *
 * Thread-safe for concurrent operations.
 */
class SaveScheduler {
 public:
    /**
     * @brief Create save scheduler
     * @param save_interval_seconds Interval between saves (default 300 = 5 min)
     * @param save_callback Called to perform actual save
     */
    explicit SaveScheduler(
        uint32_t save_interval_seconds = 300,
        std::function<std::vector<uint64_t>()> get_all_entities = nullptr,
        std::function<void(const std::vector<uint64_t>&)> save_entities = nullptr);

    ~SaveScheduler();

    /**
     * @brief Start the scheduler (launches background thread)
     */
    void Start();

    /**
     * @brief Stop the scheduler
     */
    void Stop();

    /**
     * @brief Check if scheduler is running
     */
    bool IsRunning() const { return is_running_; }

    /**
     * @brief Trigger manual save immediately
     */
    void SaveNow();

    /**
     * @brief Register progress callback
     */
    void SetProgressCallback(SaveProgressCallback callback) {
        progress_callback_ = callback;
    }

    /**
     * @brief Get last save time
     */
    std::chrono::steady_clock::time_point GetLastSaveTime() const {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        return last_save_time_;
    }

    /**
     * @brief Get save statistics
     */
    struct SaveStats {
        uint64_t total_saves = 0;
        uint64_t failed_saves = 0;
        int64_t last_save_duration_ms = 0;
        uint64_t last_save_entity_count = 0;
    };

    SaveStats GetStats() const {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        return stats_;
    }

 private:
    uint32_t save_interval_seconds_;
    std::atomic<bool> is_running_{false};
    std::unique_ptr<std::thread> scheduler_thread_;

    std::function<std::vector<uint64_t>()> get_all_entities_;
    std::function<void(const std::vector<uint64_t>&)> save_entities_;
    SaveProgressCallback progress_callback_;

    mutable std::mutex stats_mutex_;
    SaveStats stats_;
    std::chrono::steady_clock::time_point last_save_time_;

    /**
     * @brief Main scheduler loop
     */
    void SchedulerLoop();

    /**
     * @brief Perform save operation
     */
    void PerformSave();
};

}  // namespace mir2::persistence

#endif  // MIR2_PERSISTENCE_SAVE_SCHEDULER_H
