#ifndef MIR2_STORAGE_ENGINE_PERSISTENCE_ASYNC_PERSISTENCE_QUEUE_H_
#define MIR2_STORAGE_ENGINE_PERSISTENCE_ASYNC_PERSISTENCE_QUEUE_H_

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#include "storage_engine/interfaces/storage_backend.h"
#include "storage_engine/persistence/blocking_queue.h"
#include "storage_engine/storage_engine.h"

namespace mir2::storage_engine::l2 {
class RocksDBCache;
}  // namespace mir2::storage_engine::l2

namespace mir2::storage_engine::persistence {

// Asynchronous persistence queue for StorageEngine.
class AsyncPersistenceQueue {
public:
    struct Config {
        size_t queue_capacity = 10000;
        uint32_t batch_interval_ms = 30000;
        size_t batch_size = 100;
        size_t worker_threads = 2;
        uint32_t retry_count = 3;
        uint32_t retry_delay_ms = 100;
        size_t dead_letter_max_items = 10000;
        bool enable_durable_outbox = false;
        size_t outbox_replay_limit = 0;  // 0 means replay all.
        size_t outbox_max_items = 200000;  // 0 means unlimited.
        bool enable_metrics = true;
    };

    struct RuntimeConfig {
        std::optional<uint32_t> batch_interval_ms;
        std::optional<size_t> batch_size;
        std::optional<uint32_t> retry_count;
        std::optional<uint32_t> retry_delay_ms;
        std::optional<bool> enable_metrics;
    };

    explicit AsyncPersistenceQueue(IStorageBackend* backend);
    explicit AsyncPersistenceQueue(IStorageBackend* backend,
                                   const Config& config);
    AsyncPersistenceQueue(IStorageBackend* backend,
                          l2::RocksDBCache* durable_outbox_cache,
                          const Config& config);

    ~AsyncPersistenceQueue();

    bool Enqueue(const std::string& key, const VersionedData& data,
                 Priority priority);

    // Cancel pending writes for one key and register a delete barrier so
    // stale versions (<= delete_version) are dropped before backend persist.
    size_t CancelPendingForKey(const std::string& key, uint64_t delete_version);

    bool FlushAll(uint32_t timeout_ms = 5000);

    bool Flush(uint32_t timeout_ms = 5000) {
        return FlushAll(timeout_ms);
    }

    bool ApplyRuntimeConfig(const RuntimeConfig& config);

    struct Stats {
        uint64_t enqueued_total = 0;
        uint64_t persisted_success = 0;
        uint64_t persisted_failed = 0;
        size_t high_priority_queue_depth = 0;
        size_t normal_priority_queue_depth = 0;
        size_t outbox_depth = 0;
        uint64_t outbox_replayed = 0;
        uint64_t outbox_acked = 0;
        uint64_t outbox_failed = 0;
        uint64_t outbox_rejected = 0;
        uint64_t outbox_rejected_critical = 0;
        size_t dead_letter_depth = 0;
        uint64_t dead_letter_enqueued = 0;
        uint64_t dead_letter_dropped = 0;
    };

    Stats GetStats() const;

    int64_t PendingCount() const;

    struct DeadLetterEntry {
        uint64_t dead_letter_id = 0;
        std::string key;
        uint64_t version = 0;
        std::string error_message;
        uint32_t attempts = 0;
        Priority priority = Priority::NORMAL;
        uint64_t recorded_at_ms = 0;
        uint64_t durable_outbox_id = 0;
    };

    std::vector<DeadLetterEntry> GetDeadLetterSnapshot(size_t limit = 100) const;

private:
    struct PersistenceItem {
        std::string key;
        VersionedData data;
        uint64_t version;
        Priority priority = Priority::NORMAL;
        uint64_t durable_outbox_id = 0;

        PersistenceItem() = default;
        PersistenceItem(std::string k, VersionedData d, uint64_t v,
                        Priority p = Priority::NORMAL,
                        uint64_t outbox_id = 0)
            : key(std::move(k)),
              data(std::move(d)),
              version(v),
              priority(p),
              durable_outbox_id(outbox_id) {}
    };

    void HighPriorityWorker();
    void NormalBatchWorker();
    bool IsFlushDrained() const;
    void NotifyFlushCompletionIfDrained();

    bool PersistSingleItem(const PersistenceItem& item,
                           uint32_t* attempts_out,
                           std::string* error_message_out);
    bool PersistBatch(const std::vector<PersistenceItem>& batch,
                      uint32_t* attempts_out,
                      std::string* error_message_out);
    bool AckDurableOutboxItem(uint64_t outbox_id);
    bool EnqueueItem(const PersistenceItem& item, Priority priority,
                     bool count_as_enqueued);
    size_t ReplayDurableOutbox(size_t limit);
    size_t RestoreDurableDeadLetters();
    bool CanAppendOutbox() const;
    void RefreshOutboxDepthMetric();
    void RefreshDeadLetterDepthMetric();
    void RefreshQueueDepthMetrics();
    void PushDeadLetterToMemoryQueue(const DeadLetterEntry& entry,
                                     bool count_drop_metric);
    void RecordDeadLetter(const PersistenceItem& item,
                          uint32_t attempts,
                          const std::string& error_message);
    void RegisterDeleteBarrier(const std::string& key, uint64_t delete_version);
    bool IsBlockedByDeleteBarrier(const PersistenceItem& item) const;
    void IncrementMetricCounter(const std::string& name, uint64_t delta = 1) const;

    IStorageBackend* backend_ = nullptr;
    l2::RocksDBCache* durable_outbox_cache_ = nullptr;
    Config config_;

    std::unique_ptr<BlockingQueue<PersistenceItem>> high_priority_queue_;
    std::unique_ptr<BlockingQueue<PersistenceItem>> normal_priority_queue_;

    std::vector<std::thread> worker_threads_;
    std::atomic<bool> shutdown_{false};
    std::atomic<bool> flush_requested_{false};
    mutable std::mutex flush_mutex_;
    std::condition_variable flush_complete_;
    std::atomic<uint64_t> pending_items_{0};
    std::atomic<size_t> durable_outbox_depth_cached_{0};
    std::atomic<uint32_t> batch_interval_ms_{30000};
    std::atomic<size_t> batch_size_{100};
    std::atomic<uint32_t> retry_count_{3};
    std::atomic<uint32_t> retry_delay_ms_{100};
    std::atomic<bool> enable_metrics_{true};
    std::atomic<size_t> dead_letter_depth_cached_{0};
    mutable std::mutex dead_letter_mutex_;
    std::deque<DeadLetterEntry> dead_letter_queue_;
    mutable std::mutex delete_barrier_mutex_;
    std::unordered_map<std::string, uint64_t> delete_barriers_;

    struct Statistics {
        std::atomic<uint64_t> enqueued_total{0};
        std::atomic<uint64_t> persisted_success{0};
        std::atomic<uint64_t> persisted_failed{0};
        std::atomic<uint64_t> outbox_replayed{0};
        std::atomic<uint64_t> outbox_acked{0};
        std::atomic<uint64_t> outbox_failed{0};
        std::atomic<uint64_t> outbox_rejected{0};
        std::atomic<uint64_t> outbox_rejected_critical{0};
        std::atomic<uint64_t> dead_letter_enqueued{0};
        std::atomic<uint64_t> dead_letter_dropped{0};
    };
    Statistics stats_;
};

}  // namespace mir2::storage_engine::persistence

#endif  // MIR2_STORAGE_ENGINE_PERSISTENCE_ASYNC_PERSISTENCE_QUEUE_H_
