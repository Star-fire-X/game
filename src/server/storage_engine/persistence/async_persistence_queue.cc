#include "storage_engine/persistence/async_persistence_queue.h"

#include <algorithm>
#include <chrono>
#include <thread>

#include <spdlog/spdlog.h>

#include "monitor/metrics.h"
#include "storage_engine/l2/rocksdb_cache.h"

namespace mir2::storage_engine::persistence {
namespace {
constexpr const char* kOutboxDepthMetric = "storage.outbox.depth";
constexpr const char* kOutboxReplayMetric = "storage.outbox.replay_total";
constexpr const char* kOutboxAckMetric = "storage.outbox.ack_total";
constexpr const char* kOutboxFailMetric = "storage.outbox.fail_total";
constexpr const char* kOutboxRejectedMetric = "storage.outbox.rejected_total";
constexpr const char* kOutboxRejectedCriticalMetric =
    "storage.outbox.rejected_critical_total";
constexpr const char* kQueuePendingMetric = "storage.queue.pending";
constexpr const char* kQueueHighDepthMetric = "storage.queue.high_depth";
constexpr const char* kQueueNormalDepthMetric = "storage.queue.normal_depth";
constexpr const char* kDeadLetterDepthMetric = "storage.dead_letter.depth";
constexpr const char* kDeadLetterEnqueuedMetric = "storage.dead_letter.enqueued_total";
constexpr const char* kDeadLetterDroppedMetric = "storage.dead_letter.dropped_total";
constexpr const char* kPersistenceRetryMetric = "storage.persistence.retry_total";
}  // namespace

AsyncPersistenceQueue::AsyncPersistenceQueue(IStorageBackend* backend)
    : AsyncPersistenceQueue(backend, nullptr, Config{}) {}

AsyncPersistenceQueue::AsyncPersistenceQueue(IStorageBackend* backend,
                                             const Config& config)
    : AsyncPersistenceQueue(backend, nullptr, config) {}

AsyncPersistenceQueue::AsyncPersistenceQueue(
    IStorageBackend* backend,
    l2::RocksDBCache* durable_outbox_cache,
    const Config& config)
    : backend_(backend),
      durable_outbox_cache_(durable_outbox_cache),
      config_(config) {
    batch_interval_ms_.store(config_.batch_interval_ms, std::memory_order_release);
    batch_size_.store(config_.batch_size == 0 ? 1 : config_.batch_size,
                      std::memory_order_release);
    retry_count_.store(config_.retry_count, std::memory_order_release);
    retry_delay_ms_.store(config_.retry_delay_ms, std::memory_order_release);
    enable_metrics_.store(config_.enable_metrics, std::memory_order_release);

    high_priority_queue_ =
        std::make_unique<BlockingQueue<PersistenceItem>>(config.queue_capacity);
    normal_priority_queue_ =
        std::make_unique<BlockingQueue<PersistenceItem>>(config.queue_capacity);
    if (config_.enable_durable_outbox && durable_outbox_cache_ != nullptr) {
        durable_outbox_depth_cached_.store(
            durable_outbox_cache_->OutboxDepth(), std::memory_order_release);
        dead_letter_depth_cached_.store(
            durable_outbox_cache_->DeadLetterDepth(), std::memory_order_release);
    }

    const size_t replayed =
        ReplayDurableOutbox(config_.outbox_replay_limit);
    const size_t restored_dead_letters = RestoreDurableDeadLetters();
    RefreshOutboxDepthMetric();
    RefreshDeadLetterDepthMetric();
    RefreshQueueDepthMetrics();

    worker_threads_.emplace_back([this] { HighPriorityWorker(); });

    for (size_t i = 1; i < config.worker_threads; ++i) {
        worker_threads_.emplace_back([this] { NormalBatchWorker(); });
    }

    auto logger = spdlog::get("mir2");
    if (logger) {
        logger->info(
            "AsyncPersistenceQueue initialized with {} worker threads (durable_outbox={}, replayed_outbox={}, restored_dead_letters={})",
            config_.worker_threads,
            config_.enable_durable_outbox && durable_outbox_cache_ != nullptr,
            replayed,
            restored_dead_letters);
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

bool AsyncPersistenceQueue::ApplyRuntimeConfig(const RuntimeConfig& config) {
    if (config.batch_interval_ms.has_value()) {
        batch_interval_ms_.store(*config.batch_interval_ms,
                                 std::memory_order_release);
        config_.batch_interval_ms = *config.batch_interval_ms;
    }
    if (config.batch_size.has_value()) {
        const size_t safe_batch_size = std::max<size_t>(1, *config.batch_size);
        batch_size_.store(safe_batch_size, std::memory_order_release);
        config_.batch_size = safe_batch_size;
    }
    if (config.retry_count.has_value()) {
        retry_count_.store(*config.retry_count, std::memory_order_release);
        config_.retry_count = *config.retry_count;
    }
    if (config.retry_delay_ms.has_value()) {
        retry_delay_ms_.store(*config.retry_delay_ms, std::memory_order_release);
        config_.retry_delay_ms = *config.retry_delay_ms;
    }
    if (config.enable_metrics.has_value()) {
        enable_metrics_.store(*config.enable_metrics, std::memory_order_release);
        config_.enable_metrics = *config.enable_metrics;
    }
    RefreshOutboxDepthMetric();
    RefreshDeadLetterDepthMetric();
    RefreshQueueDepthMetrics();
    return true;
}

bool AsyncPersistenceQueue::Enqueue(const std::string& key,
                                    const VersionedData& data,
                                    Priority priority) {
    if (shutdown_.load(std::memory_order_acquire)) {
        return false;
    }

    if (!backend_) {
        return false;
    }

    const bool durable_enabled =
        config_.enable_durable_outbox && durable_outbox_cache_ != nullptr;
    if (durable_enabled) {
        if (!CanAppendOutbox()) {
            stats_.outbox_rejected.fetch_add(1, std::memory_order_relaxed);
            IncrementMetricCounter(kOutboxRejectedMetric);
            if (priority == Priority::CRITICAL) {
                stats_.outbox_rejected_critical.fetch_add(
                    1, std::memory_order_relaxed);
                IncrementMetricCounter(kOutboxRejectedCriticalMetric);
            }
            auto logger = spdlog::get("mir2");
            if (logger) {
                logger->warn(
                    "Durable outbox full, reject enqueue key={} depth={} limit={} priority={}",
                    key,
                    durable_outbox_depth_cached_.load(std::memory_order_acquire),
                    config_.outbox_max_items,
                    static_cast<int>(priority));
            }
            return false;
        }

        uint64_t outbox_id = 0;
        if (!durable_outbox_cache_->AppendOutbox(key, data, priority, &outbox_id)) {
            stats_.outbox_failed.fetch_add(1, std::memory_order_relaxed);
            IncrementMetricCounter(kOutboxFailMetric);
            return false;
        }
        durable_outbox_depth_cached_.fetch_add(1, std::memory_order_acq_rel);
        RefreshOutboxDepthMetric();

        PersistenceItem item(key, data, data.version, priority, outbox_id);
        if (!EnqueueItem(item, priority, true)) {
            const bool acked = durable_outbox_cache_->AckOutbox(outbox_id);
            if (!acked) {
                stats_.outbox_failed.fetch_add(1, std::memory_order_relaxed);
                IncrementMetricCounter(kOutboxFailMetric);
                // Ack failed means the outbox row still exists; keep cached depth.
                RefreshOutboxDepthMetric();
                return false;
            }
            durable_outbox_depth_cached_.fetch_sub(1, std::memory_order_acq_rel);
            RefreshOutboxDepthMetric();
            return false;
        }
        return true;
    }

    if (!backend_->IsHealthy()) {
        return false;
    }

    PersistenceItem item(key, data, data.version, priority);
    return EnqueueItem(item, priority, true);
}

bool AsyncPersistenceQueue::EnqueueItem(const PersistenceItem& item,
                                        Priority priority,
                                        bool count_as_enqueued) {
    bool success = false;
    if (priority == Priority::HIGH || priority == Priority::CRITICAL) {
        success = high_priority_queue_->Enqueue(item);
    } else {
        success = normal_priority_queue_->Enqueue(item);
    }

    if (success) {
        if (count_as_enqueued) {
            stats_.enqueued_total.fetch_add(1, std::memory_order_relaxed);
        }
        pending_items_.fetch_add(1, std::memory_order_release);
        RefreshQueueDepthMetrics();
    }
    return success;
}

size_t AsyncPersistenceQueue::ReplayDurableOutbox(size_t limit) {
    const bool durable_enabled =
        config_.enable_durable_outbox && durable_outbox_cache_ != nullptr;
    if (!durable_enabled) {
        return 0;
    }

    auto logger = spdlog::get("mir2");
    size_t replayed = 0;
    const size_t scanned = durable_outbox_cache_->ReplayOutbox(
        limit, [this, &replayed](const l2::RocksDBCache::OutboxEntry& entry) {
            PersistenceItem item(entry.key, entry.data, entry.data.version,
                                 entry.priority, entry.outbox_id);
            if (!EnqueueItem(item, entry.priority, false)) {
                return false;
            }
            ++replayed;
            return true;
        });

    stats_.outbox_replayed.fetch_add(replayed, std::memory_order_relaxed);
    if (replayed > 0) {
        IncrementMetricCounter(kOutboxReplayMetric, replayed);
    }
    if (logger && scanned > replayed) {
        logger->warn(
            "ReplayDurableOutbox stopped early: scanned={}, replayed={}",
            scanned, replayed);
    }
    return replayed;
}

size_t AsyncPersistenceQueue::RestoreDurableDeadLetters() {
    const bool durable_enabled =
        config_.enable_durable_outbox && durable_outbox_cache_ != nullptr;
    if (!durable_enabled) {
        return 0;
    }

    size_t restored = 0;
    durable_outbox_cache_->ReplayDeadLetter(
        0, [this, &restored](const l2::RocksDBCache::DeadLetterEntry& entry) {
            DeadLetterEntry memory_entry{
                .dead_letter_id = entry.dead_letter_id,
                .key = entry.key,
                .version = entry.data.version,
                .error_message = entry.error_message,
                .attempts = entry.attempts,
                .priority = entry.priority,
                .recorded_at_ms = entry.recorded_at_ms,
                .durable_outbox_id = entry.durable_outbox_id,
            };
            PushDeadLetterToMemoryQueue(memory_entry, false);
            ++restored;
            return true;
        });
    return restored;
}

bool AsyncPersistenceQueue::AckDurableOutboxItem(uint64_t outbox_id) {
    const bool durable_enabled =
        config_.enable_durable_outbox && durable_outbox_cache_ != nullptr;
    if (!durable_enabled || outbox_id == 0) {
        return true;
    }
    if (!durable_outbox_cache_->AckOutbox(outbox_id)) {
        stats_.outbox_failed.fetch_add(1, std::memory_order_relaxed);
        IncrementMetricCounter(kOutboxFailMetric);
        return false;
    }
    stats_.outbox_acked.fetch_add(1, std::memory_order_relaxed);
    IncrementMetricCounter(kOutboxAckMetric);
    size_t current_depth =
        durable_outbox_depth_cached_.load(std::memory_order_acquire);
    while (current_depth > 0) {
        if (durable_outbox_depth_cached_.compare_exchange_weak(
                current_depth, current_depth - 1,
                std::memory_order_acq_rel,
                std::memory_order_acquire)) {
            break;
        }
    }
    RefreshOutboxDepthMetric();
    return true;
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
    const size_t outbox_depth =
        durable_outbox_depth_cached_.load(std::memory_order_acquire);
    const size_t dead_letter_depth =
        dead_letter_depth_cached_.load(std::memory_order_acquire);
    return Stats{
        .enqueued_total = stats_.enqueued_total.load(std::memory_order_relaxed),
        .persisted_success = stats_.persisted_success.load(std::memory_order_relaxed),
        .persisted_failed = stats_.persisted_failed.load(std::memory_order_relaxed),
        .high_priority_queue_depth = high_priority_queue_->Size(),
        .normal_priority_queue_depth = normal_priority_queue_->Size(),
        .outbox_depth = outbox_depth,
        .outbox_replayed = stats_.outbox_replayed.load(std::memory_order_relaxed),
        .outbox_acked = stats_.outbox_acked.load(std::memory_order_relaxed),
        .outbox_failed = stats_.outbox_failed.load(std::memory_order_relaxed),
        .outbox_rejected = stats_.outbox_rejected.load(std::memory_order_relaxed),
        .outbox_rejected_critical =
            stats_.outbox_rejected_critical.load(std::memory_order_relaxed),
        .dead_letter_depth = dead_letter_depth,
        .dead_letter_enqueued =
            stats_.dead_letter_enqueued.load(std::memory_order_relaxed),
        .dead_letter_dropped =
            stats_.dead_letter_dropped.load(std::memory_order_relaxed),
    };
}

int64_t AsyncPersistenceQueue::PendingCount() const {
    if (!high_priority_queue_ || !normal_priority_queue_) {
        return 0;
    }

    return static_cast<int64_t>(
        pending_items_.load(std::memory_order_acquire));
}

std::vector<AsyncPersistenceQueue::DeadLetterEntry>
AsyncPersistenceQueue::GetDeadLetterSnapshot(size_t limit) const {
    std::lock_guard<std::mutex> lock(dead_letter_mutex_);
    if (dead_letter_queue_.empty() || limit == 0) {
        return {};
    }

    const size_t count = std::min(limit, dead_letter_queue_.size());
    std::vector<DeadLetterEntry> snapshot;
    snapshot.reserve(count);
    auto begin = dead_letter_queue_.end() - static_cast<std::ptrdiff_t>(count);
    for (auto it = begin; it != dead_letter_queue_.end(); ++it) {
        snapshot.push_back(*it);
    }
    return snapshot;
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

        uint32_t attempts = 0;
        std::string error_message;
        if (PersistSingleItem(item.value(), &attempts, &error_message)) {
            if (!AckDurableOutboxItem(item->durable_outbox_id) && logger) {
                logger->warn("HighPriorityWorker: Ack durable outbox failed id={}",
                             item->durable_outbox_id);
            }
            stats_.persisted_success.fetch_add(1, std::memory_order_relaxed);
        } else {
            stats_.persisted_failed.fetch_add(1, std::memory_order_relaxed);
            RecordDeadLetter(item.value(), attempts, error_message);
            if (logger) {
                logger->warn(
                    "HighPriorityWorker: Failed to persist key '{}' attempts={} err='{}'",
                    item.value().key, attempts, error_message);
            }
        }

        pending_items_.fetch_sub(1, std::memory_order_acq_rel);
        RefreshQueueDepthMetrics();
        NotifyFlushCompletionIfDrained();
    }

    while (true) {
        auto item = high_priority_queue_->Pop(0);
        if (!item) {
            break;
        }

        uint32_t attempts = 0;
        std::string error_message;
        if (PersistSingleItem(item.value(), &attempts, &error_message)) {
            AckDurableOutboxItem(item->durable_outbox_id);
            stats_.persisted_success.fetch_add(1, std::memory_order_relaxed);
        } else {
            stats_.persisted_failed.fetch_add(1, std::memory_order_relaxed);
            RecordDeadLetter(item.value(), attempts, error_message);
        }

        pending_items_.fetch_sub(1, std::memory_order_acq_rel);
        RefreshQueueDepthMetrics();
    }

    NotifyFlushCompletionIfDrained();
}

void AsyncPersistenceQueue::NormalBatchWorker() {
    auto logger = spdlog::get("mir2");
    std::vector<PersistenceItem> batch;
    batch.reserve(batch_size_.load(std::memory_order_acquire));

    auto last_flush_time = std::chrono::steady_clock::now();
    auto persist_batch = [this, logger](const std::vector<PersistenceItem>& items) {
        if (items.empty()) {
            return;
        }

        uint32_t attempts = 0;
        std::string error_message;
        if (PersistBatch(items, &attempts, &error_message)) {
            for (const auto& persisted_item : items) {
                if (!AckDurableOutboxItem(persisted_item.durable_outbox_id) &&
                    logger) {
                    logger->warn(
                        "NormalBatchWorker: Ack durable outbox failed id={}",
                        persisted_item.durable_outbox_id);
                }
            }
            stats_.persisted_success.fetch_add(items.size(),
                                               std::memory_order_relaxed);
        } else {
            stats_.persisted_failed.fetch_add(items.size(),
                                              std::memory_order_relaxed);
            for (const auto& failed_item : items) {
                RecordDeadLetter(failed_item, attempts, error_message);
            }
            if (logger) {
                logger->warn(
                    "NormalBatchWorker: Batch persist failed, {} items attempts={} err='{}'",
                    items.size(), attempts, error_message);
            }
        }

        pending_items_.fetch_sub(static_cast<uint64_t>(items.size()),
                                 std::memory_order_acq_rel);
        RefreshQueueDepthMetrics();
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
            const size_t batch_size_limit =
                batch_size_.load(std::memory_order_acquire);
            while (batch.size() < batch_size_limit) {
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
        const size_t batch_size_limit = batch_size_.load(std::memory_order_acquire);
        const uint32_t batch_interval_ms =
            batch_interval_ms_.load(std::memory_order_acquire);

        bool should_flush =
            batch.size() >= batch_size_limit ||
            (elapsed >= static_cast<int64_t>(batch_interval_ms) && !batch.empty()) ||
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

        if (batch.size() >= batch_size_.load(std::memory_order_acquire)) {
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

bool AsyncPersistenceQueue::PersistSingleItem(const PersistenceItem& item,
                                              uint32_t* attempts_out,
                                              std::string* error_message_out) {
    if (attempts_out) {
        *attempts_out = 0;
    }
    if (error_message_out) {
        error_message_out->clear();
    }

    if (!backend_) {
        if (error_message_out) {
            *error_message_out = "backend unavailable";
        }
        return false;
    }
    if (!backend_->IsHealthy()) {
        if (error_message_out) {
            *error_message_out = "backend unhealthy";
        }
        return false;
    }

    const uint32_t retry_limit = retry_count_.load(std::memory_order_acquire);
    const uint32_t retry_delay_ms = retry_delay_ms_.load(std::memory_order_acquire);
    for (uint32_t retry = 0; retry <= retry_limit; ++retry) {
        if (attempts_out) {
            *attempts_out = retry + 1;
        }
        auto result = backend_->Save(item.key, item.version, item.data.data);
        if (result.success) {
            return true;
        }
        if (error_message_out) {
            *error_message_out = result.error_message;
        }

        if (retry < retry_limit) {
            IncrementMetricCounter(kPersistenceRetryMetric);
            std::this_thread::sleep_for(
                std::chrono::milliseconds(retry_delay_ms * (retry + 1)));
        }
    }

    return false;
}

bool AsyncPersistenceQueue::PersistBatch(const std::vector<PersistenceItem>& batch,
                                         uint32_t* attempts_out,
                                         std::string* error_message_out) {
    if (attempts_out) {
        *attempts_out = 0;
    }
    if (error_message_out) {
        error_message_out->clear();
    }
    if (batch.empty()) {
        if (error_message_out) {
            *error_message_out = "batch empty";
        }
        return false;
    }
    if (!backend_) {
        if (error_message_out) {
            *error_message_out = "backend unavailable";
        }
        return false;
    }
    if (!backend_->IsHealthy()) {
        if (error_message_out) {
            *error_message_out = "backend unhealthy";
        }
        return false;
    }

    std::vector<std::tuple<std::string, uint64_t, std::vector<uint8_t>>> batch_data;
    batch_data.reserve(batch.size());

    for (const auto& item : batch) {
        batch_data.emplace_back(item.key, item.version, item.data.data);
    }

    const uint32_t retry_limit = retry_count_.load(std::memory_order_acquire);
    const uint32_t retry_delay_ms = retry_delay_ms_.load(std::memory_order_acquire);
    for (uint32_t retry = 0; retry <= retry_limit; ++retry) {
        if (attempts_out) {
            *attempts_out = retry + 1;
        }
        if (backend_->IsHealthy()) {
            auto* atomic_backend =
                dynamic_cast<IAtomicBatchStorageBackend*>(backend_);
            auto result = atomic_backend != nullptr
                              ? atomic_backend->SaveBatchAtomic(batch_data)
                              : backend_->SaveBatch(batch_data);
            if (result.success) {
                return true;
            }
            if (error_message_out) {
                *error_message_out = result.error_message;
            }
        } else if (error_message_out) {
            *error_message_out = "backend unhealthy";
        }

        if (retry < retry_limit) {
            IncrementMetricCounter(kPersistenceRetryMetric);
            std::this_thread::sleep_for(
                std::chrono::milliseconds(retry_delay_ms * (retry + 1)));
        }
    }

    return false;
}

bool AsyncPersistenceQueue::CanAppendOutbox() const {
    if (config_.outbox_max_items == 0) {
        return true;
    }
    return durable_outbox_depth_cached_.load(std::memory_order_acquire) <
           config_.outbox_max_items;
}

void AsyncPersistenceQueue::RefreshOutboxDepthMetric() {
    if (!enable_metrics_.load(std::memory_order_acquire)) {
        return;
    }
    monitor::Metrics::Instance().SetGauge(
        kOutboxDepthMetric,
        static_cast<double>(
            durable_outbox_depth_cached_.load(std::memory_order_acquire)));
}

void AsyncPersistenceQueue::RefreshDeadLetterDepthMetric() {
    if (!enable_metrics_.load(std::memory_order_acquire)) {
        return;
    }
    monitor::Metrics::Instance().SetGauge(
        kDeadLetterDepthMetric,
        static_cast<double>(
            dead_letter_depth_cached_.load(std::memory_order_acquire)));
}

void AsyncPersistenceQueue::RefreshQueueDepthMetrics() {
    if (!enable_metrics_.load(std::memory_order_acquire)) {
        return;
    }
    monitor::Metrics::Instance().SetGauge(
        kQueuePendingMetric,
        static_cast<double>(pending_items_.load(std::memory_order_acquire)));
    monitor::Metrics::Instance().SetGauge(
        kQueueHighDepthMetric,
        static_cast<double>(high_priority_queue_ ? high_priority_queue_->Size() : 0));
    monitor::Metrics::Instance().SetGauge(
        kQueueNormalDepthMetric,
        static_cast<double>(normal_priority_queue_ ? normal_priority_queue_->Size() : 0));
}

void AsyncPersistenceQueue::PushDeadLetterToMemoryQueue(
    const DeadLetterEntry& entry,
    bool count_drop_metric) {
    std::lock_guard<std::mutex> lock(dead_letter_mutex_);
    if (config_.dead_letter_max_items > 0 &&
        dead_letter_queue_.size() >= config_.dead_letter_max_items) {
        dead_letter_queue_.pop_front();
        if (count_drop_metric) {
            stats_.dead_letter_dropped.fetch_add(1, std::memory_order_relaxed);
            IncrementMetricCounter(kDeadLetterDroppedMetric);
        }
    }
    dead_letter_queue_.push_back(entry);
}

void AsyncPersistenceQueue::RecordDeadLetter(const PersistenceItem& item,
                                             uint32_t attempts,
                                             const std::string& error_message) {
    const bool durable_enabled =
        config_.enable_durable_outbox && durable_outbox_cache_ != nullptr;
    const uint64_t recorded_at_ms = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count());

    DeadLetterEntry memory_entry{
        .dead_letter_id = 0,
        .key = item.key,
        .version = item.version,
        .error_message = error_message,
        .attempts = attempts,
        .priority = item.priority,
        .recorded_at_ms = recorded_at_ms,
        .durable_outbox_id = item.durable_outbox_id,
    };

    bool durable_persisted = false;
    if (durable_enabled) {
        l2::RocksDBCache::DeadLetterEntry durable_entry{
            .dead_letter_id = 0,
            .key = item.key,
            .data = item.data,
            .priority = item.priority,
            .attempts = attempts,
            .durable_outbox_id = item.durable_outbox_id,
            .recorded_at_ms = recorded_at_ms,
            .error_message = error_message,
        };
        uint64_t dead_letter_id = 0;
        if (durable_outbox_cache_->AppendDeadLetter(durable_entry, &dead_letter_id)) {
            memory_entry.dead_letter_id = dead_letter_id;
            durable_persisted = true;
            dead_letter_depth_cached_.fetch_add(1, std::memory_order_acq_rel);
        } else {
            auto logger = spdlog::get("mir2");
            if (logger) {
                logger->warn(
                    "RecordDeadLetter: failed to persist dead letter key={} outbox_id={}",
                    item.key, item.durable_outbox_id);
            }
        }
    }
    PushDeadLetterToMemoryQueue(memory_entry, true);

    if (!durable_enabled || !durable_persisted) {
        std::lock_guard<std::mutex> lock(dead_letter_mutex_);
        dead_letter_depth_cached_.store(dead_letter_queue_.size(),
                                        std::memory_order_release);
    }

    stats_.dead_letter_enqueued.fetch_add(1, std::memory_order_relaxed);
    IncrementMetricCounter(kDeadLetterEnqueuedMetric);
    RefreshDeadLetterDepthMetric();
}

void AsyncPersistenceQueue::IncrementMetricCounter(const std::string& name,
                                                   uint64_t delta) const {
    if (!enable_metrics_.load(std::memory_order_acquire)) {
        return;
    }
    monitor::Metrics::Instance().IncrementCounter(name, delta);
}

}  // namespace mir2::storage_engine::persistence
