/**
 * @file batch_writer.cc
 * @brief Batch writer implementation
 */

#include "persistence/batch_writer.h"
#include "log/logger.h"
#include <algorithm>

namespace mir2::persistence {

BatchWriter::BatchWriter(
    std::shared_ptr<IStorageBackend> backend,
    uint32_t batch_size,
    uint32_t batch_timeout_ms)
    : backend_(backend),
      batch_size_(batch_size),
      batch_timeout_ms_(batch_timeout_ms),
      last_flush_time_(std::chrono::steady_clock::now()) {
}

BatchWriter::~BatchWriter() {
    Stop();
}

bool BatchWriter::Enqueue(
    uint64_t entity_id,
    const std::string& entity_type,
    const std::vector<uint8_t>& data,
    uint32_t version) {
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        write_queue_.emplace(entity_id, entity_type, data, version);

        {
            std::lock_guard<std::mutex> stats_lock(stats_mutex_);
            stats_.queued_entities = write_queue_.size();
        }

        // Signal flush if batch is full
        if (write_queue_.size() >= batch_size_) {
            flush_condition_.notify_one();
        }
    }

    return true;
}

bool BatchWriter::FlushBatch() {
    std::vector<WriteEntry> batch;

    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        while (!write_queue_.empty()) {
            batch.push_back(write_queue_.front());
            write_queue_.pop();
        }
    }

    if (batch.empty()) {
        return true;
    }

    bool result = ExecuteBatch(batch);

    {
        std::lock_guard<std::mutex> stats_lock(stats_mutex_);
        stats_.queued_entities = 0;
        if (result) {
            stats_.total_batches++;
        } else {
            stats_.failed_batches++;
        }
    }

    return result;
}

void BatchWriter::Start() {
    if (is_running_.exchange(true)) {
        SYSLOG_WARN("BatchWriter already running");
        return;
    }

    flush_thread_ = std::make_unique<std::thread>([this] { FlushLoop(); });
    SYSLOG_INFO("BatchWriter started (batch_size: {}, timeout: {}ms)",
                batch_size_, batch_timeout_ms_);
}

void BatchWriter::Stop() {
    if (!is_running_.exchange(false)) {
        return;
    }

    flush_condition_.notify_all();

    if (flush_thread_ && flush_thread_->joinable()) {
        flush_thread_->join();
    }

    // Final flush
    FlushBatch();

    SYSLOG_INFO("BatchWriter stopped");
}

void BatchWriter::FlushLoop() {
    SYSLOG_DEBUG("BatchWriter flush loop started");

    while (is_running_) {
        auto next_flush = last_flush_time_ + std::chrono::milliseconds(batch_timeout_ms_);
        auto now = std::chrono::steady_clock::now();
        auto wait_duration = next_flush - now;

        std::unique_lock<std::mutex> lock(queue_mutex_);

        // Wait for signal or timeout
        if (wait_duration.count() > 0) {
            flush_condition_.wait_for(lock, wait_duration);
        }

        // Check if we should flush
        if (!write_queue_.empty()) {
            lock.unlock();
            FlushBatch();
            last_flush_time_ = std::chrono::steady_clock::now();
        }
    }

    SYSLOG_DEBUG("BatchWriter flush loop ended");
}

bool BatchWriter::ExecuteBatch(const std::vector<WriteEntry>& entries) {
    auto batch_start = std::chrono::steady_clock::now();

    try {
        if (!backend_ || !backend_->IsReady()) {
            SYSLOG_WARN("Backend not ready for batch write");
            return false;
        }

        auto result = backend_->SaveEntitiesBatch(entries);
        if (!result.success) {
            SYSLOG_WARN("Batch write failed: {}", result.error_message);
            return false;
        }

        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - batch_start);

        {
            std::lock_guard<std::mutex> lock(stats_mutex_);
            stats_.last_batch_duration_ms = duration.count();
            stats_.last_queue_size = entries.size();
        }

        SYSLOG_DEBUG("Batch write successful: {} entities in {}ms",
                     entries.size(), duration.count());

        return true;
    } catch (const std::exception& e) {
        SYSLOG_ERROR("Batch write exception: {}", e.what());
        return false;
    }
}

}  // namespace mir2::persistence
