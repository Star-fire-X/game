#include "storage_engine/persistence/async_persistence_queue.h"

#include <chrono>
#include <thread>

#include <spdlog/spdlog.h>

namespace mir2::storage_engine::persistence {

AsyncPersistenceQueue::AsyncPersistenceQueue(IStorageBackend* backend)
    : AsyncPersistenceQueue(backend, Config{}) {}

AsyncPersistenceQueue::AsyncPersistenceQueue(IStorageBackend* backend,
                                             const Config& config)
    : backend_(backend), config_(config) {
    high_priority_queue_ =
        std::make_unique<BlockingQueue<PersistenceItem>>(config.queue_capacity);
    normal_priority_queue_ =
        std::make_unique<BlockingQueue<PersistenceItem>>(config.queue_capacity);

    worker_threads_.emplace_back([this] { HighPriorityWorker(); });

    for (size_t i = 1; i < config.worker_threads; ++i) {
        worker_threads_.emplace_back([this] { NormalBatchWorker(); });
    }

    auto logger = spdlog::get("mir2");
    if (logger) {
        logger->info("AsyncPersistenceQueue initialized with {} worker threads",
                     config_.worker_threads);
    }
}

AsyncPersistenceQueue::~AsyncPersistenceQueue() {
    FlushAll(5000);
    shutdown_.store(true, std::memory_order_release);
    high_priority_queue_->NotifyAll();
    normal_priority_queue_->NotifyAll();

    for (auto& thread : worker_threads_) {
        if (thread.joinable()) {
            thread.join();
        }
    }
}

bool AsyncPersistenceQueue::Enqueue(const std::string& key,
                                    const VersionedData& data,
                                    Priority priority) {
    if (shutdown_.load(std::memory_order_acquire)) {
        return false;
    }

    if (!backend_ || !backend_->IsHealthy()) {
        return false;
    }

    PersistenceItem item(key, data, data.version);

    bool success = false;
    if (priority == Priority::HIGH || priority == Priority::CRITICAL) {
        success = high_priority_queue_->Enqueue(item);
    } else {
        success = normal_priority_queue_->Enqueue(item);
    }

    if (success) {
        stats_.enqueued_total.fetch_add(1, std::memory_order_relaxed);
        pending_items_.fetch_add(1, std::memory_order_release);
    }

    return success;
}

bool AsyncPersistenceQueue::FlushAll(uint32_t timeout_ms) {
    std::unique_lock<std::mutex> lock(flush_mutex_);

    flush_requested_.store(true, std::memory_order_release);
    high_priority_queue_->NotifyAll();
    normal_priority_queue_->NotifyAll();

    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    const bool drained = flush_complete_.wait_until(
        lock, deadline, [this] { return IsFlushDrained(); });

    flush_requested_.store(false, std::memory_order_release);
    return drained;
}

AsyncPersistenceQueue::Stats AsyncPersistenceQueue::GetStats() const {
    return Stats{
        .enqueued_total = stats_.enqueued_total.load(std::memory_order_relaxed),
        .persisted_success = stats_.persisted_success.load(std::memory_order_relaxed),
        .persisted_failed = stats_.persisted_failed.load(std::memory_order_relaxed),
        .high_priority_queue_depth = high_priority_queue_->Size(),
        .normal_priority_queue_depth = normal_priority_queue_->Size(),
    };
}

int64_t AsyncPersistenceQueue::PendingCount() const {
    if (!high_priority_queue_ || !normal_priority_queue_) {
        return 0;
    }

    return static_cast<int64_t>(
        pending_items_.load(std::memory_order_acquire));
}

void AsyncPersistenceQueue::HighPriorityWorker() {
    auto logger = spdlog::get("mir2");

    while (!shutdown_.load(std::memory_order_acquire)) {
        const uint32_t pop_timeout_ms =
            flush_requested_.load(std::memory_order_acquire) ? 10 : 500;
        auto item = high_priority_queue_->Pop(pop_timeout_ms);

        if (!item) {
            NotifyFlushCompletionIfDrained();
            continue;
        }

        if (PersistSingleItem(item.value())) {
            stats_.persisted_success.fetch_add(1, std::memory_order_relaxed);
        } else {
            stats_.persisted_failed.fetch_add(1, std::memory_order_relaxed);
            if (logger) {
                logger->warn("HighPriorityWorker: Failed to persist key '{}'",
                             item.value().key);
            }
        }

        pending_items_.fetch_sub(1, std::memory_order_acq_rel);
        NotifyFlushCompletionIfDrained();
    }

    while (true) {
        auto item = high_priority_queue_->Pop(0);
        if (!item) {
            break;
        }

        if (PersistSingleItem(item.value())) {
            stats_.persisted_success.fetch_add(1, std::memory_order_relaxed);
        } else {
            stats_.persisted_failed.fetch_add(1, std::memory_order_relaxed);
        }

        pending_items_.fetch_sub(1, std::memory_order_acq_rel);
    }

    NotifyFlushCompletionIfDrained();
}

void AsyncPersistenceQueue::NormalBatchWorker() {
    auto logger = spdlog::get("mir2");
    std::vector<PersistenceItem> batch;
    batch.reserve(config_.batch_size);

    auto last_flush_time = std::chrono::steady_clock::now();
    auto persist_batch = [this, logger](const std::vector<PersistenceItem>& items) {
        if (items.empty()) {
            return;
        }

        if (PersistBatch(items)) {
            stats_.persisted_success.fetch_add(items.size(),
                                               std::memory_order_relaxed);
        } else {
            stats_.persisted_failed.fetch_add(items.size(),
                                              std::memory_order_relaxed);
            if (logger) {
                logger->warn("NormalBatchWorker: Batch persist failed, {} items",
                             items.size());
            }
        }

        pending_items_.fetch_sub(static_cast<uint64_t>(items.size()),
                                 std::memory_order_acq_rel);
        NotifyFlushCompletionIfDrained();
    };

    while (!shutdown_.load(std::memory_order_acquire)) {
        const bool flush_requested =
            flush_requested_.load(std::memory_order_acquire);
        auto item = normal_priority_queue_->Pop(flush_requested ? 10 : 100);

        if (item) {
            batch.push_back(item.value());
        }

        if (flush_requested) {
            while (batch.size() < config_.batch_size) {
                auto extra_item = normal_priority_queue_->Pop(0);
                if (!extra_item) {
                    break;
                }
                batch.push_back(extra_item.value());
            }
        }

        auto now = std::chrono::steady_clock::now();
        auto elapsed =
            std::chrono::duration_cast<std::chrono::milliseconds>(now - last_flush_time)
                .count();

        bool should_flush =
            batch.size() >= config_.batch_size ||
            (elapsed >= static_cast<int64_t>(config_.batch_interval_ms) && !batch.empty()) ||
            (flush_requested && !batch.empty());

        if (should_flush) {
            persist_batch(batch);
            batch.clear();
            last_flush_time = now;
        }

        if (flush_requested && !item && batch.empty()) {
            NotifyFlushCompletionIfDrained();
        }
    }

    while (true) {
        auto item = normal_priority_queue_->Pop(0);
        if (!item) {
            break;
        }
        batch.push_back(item.value());

        if (batch.size() >= config_.batch_size) {
            persist_batch(batch);
            batch.clear();
        }
    }

    if (!batch.empty()) {
        persist_batch(batch);
    }

    NotifyFlushCompletionIfDrained();
}

bool AsyncPersistenceQueue::IsFlushDrained() const {
    if (!high_priority_queue_ || !normal_priority_queue_) {
        return true;
    }

    return pending_items_.load(std::memory_order_acquire) == 0 &&
           high_priority_queue_->IsEmpty() &&
           normal_priority_queue_->IsEmpty();
}

void AsyncPersistenceQueue::NotifyFlushCompletionIfDrained() {
    if (!flush_requested_.load(std::memory_order_acquire)) {
        return;
    }

    if (IsFlushDrained()) {
        flush_complete_.notify_all();
    }
}

bool AsyncPersistenceQueue::PersistSingleItem(const PersistenceItem& item) {
    if (!backend_ || !backend_->IsHealthy()) {
        return false;
    }

    for (uint32_t retry = 0; retry <= config_.retry_count; ++retry) {
        auto result = backend_->Save(item.key, item.version, item.data.data);
        if (result.success) {
            return true;
        }

        if (retry < config_.retry_count) {
            std::this_thread::sleep_for(
                std::chrono::milliseconds(config_.retry_delay_ms * (retry + 1)));
        }
    }

    return false;
}

bool AsyncPersistenceQueue::PersistBatch(const std::vector<PersistenceItem>& batch) {
    if (batch.empty() || !backend_ || !backend_->IsHealthy()) {
        return false;
    }

    std::vector<std::tuple<std::string, uint64_t, std::vector<uint8_t>>> batch_data;
    batch_data.reserve(batch.size());

    for (const auto& item : batch) {
        batch_data.emplace_back(item.key, item.version, item.data.data);
    }

    for (uint32_t retry = 0; retry <= config_.retry_count; ++retry) {
        if (backend_->IsHealthy()) {
            auto result = backend_->SaveBatch(batch_data);
            if (result.success) {
                return true;
            }
        }

        if (retry < config_.retry_count) {
            std::this_thread::sleep_for(
                std::chrono::milliseconds(config_.retry_delay_ms * (retry + 1)));
        }
    }

    return false;
}

}  // namespace mir2::storage_engine::persistence
