/**
 * @file batch_writer.h
 * @brief Batch write optimization (Epic 4: Story 4.1)
 */

#ifndef MIR2_PERSISTENCE_BATCH_WRITER_H
#define MIR2_PERSISTENCE_BATCH_WRITER_H

#include "persistence/storage_backend.h"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <tuple>
#include <vector>

namespace mir2::persistence {

/**
 * @brief Batch write optimizer (Story 4.1)
 *
 * Features:
 * - Accumulates writes into batches
 * - Flushes based on: batch size (100) OR timeout (200ms)
 * - Single database transaction per batch
 * - Per-entity error tracking with retry
 * - Performance: 1000 entities < 2 seconds
 *
 * Thread-safe for concurrent writes.
 */
class BatchWriter {
 public:
    /**
     * @brief Create batch writer
     * @param backend Storage backend for batch operations
     * @param batch_size Minimum entities before flush (default 100)
     * @param batch_timeout_ms Max wait time before flush (default 200ms)
     */
    explicit BatchWriter(
        std::shared_ptr<IStorageBackend> backend,
        uint32_t batch_size = 100,
        uint32_t batch_timeout_ms = 200);

    ~BatchWriter();

    /**
     * @brief Enqueue entity for batch write
     * @return true if queued, false if error
     */
    bool Enqueue(
        uint64_t entity_id,
        const std::string& entity_type,
        const std::vector<uint8_t>& data,
        uint32_t version = 1);

    /**
     * @brief Flush pending batch immediately
     */
    bool FlushBatch();

    /**
     * @brief Start batch writer background thread
     */
    void Start();

    /**
     * @brief Stop batch writer
     */
    void Stop();

    /**
     * @brief Get queue statistics
     */
    struct QueueStats {
        uint64_t queued_entities = 0;
        uint64_t total_batches = 0;
        uint64_t failed_batches = 0;
        int64_t last_batch_duration_ms = 0;
        int64_t last_queue_size = 0;
    };

    QueueStats GetStats() const {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        return stats_;
    }

 private:
    std::shared_ptr<IStorageBackend> backend_;
    uint32_t batch_size_;
    uint32_t batch_timeout_ms_;
    std::atomic<bool> is_running_{false};
    std::unique_ptr<std::thread> flush_thread_;

    using WriteEntry = std::tuple<uint64_t, std::string, std::vector<uint8_t>, uint32_t>;
    std::mutex queue_mutex_;
    std::queue<WriteEntry> write_queue_;
    std::condition_variable flush_condition_;

    mutable std::mutex stats_mutex_;
    QueueStats stats_;
    std::chrono::steady_clock::time_point last_flush_time_;

    /**
     * @brief Flush thread main loop
     */
    void FlushLoop();

    /**
     * @brief Execute batch write
     */
    bool ExecuteBatch(const std::vector<WriteEntry>& entries);
};

}  // namespace mir2::persistence

#endif  // MIR2_PERSISTENCE_BATCH_WRITER_H
