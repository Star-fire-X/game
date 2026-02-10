#ifndef MIR2_STORAGE_ENGINE_PERSISTENCE_ASYNC_PERSISTENCE_QUEUE_H_
#define MIR2_STORAGE_ENGINE_PERSISTENCE_ASYNC_PERSISTENCE_QUEUE_H_

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "storage_engine/interfaces/storage_backend.h"
#include "storage_engine/persistence/blocking_queue.h"
#include "storage_engine/storage_engine.h"

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
    };

    explicit AsyncPersistenceQueue(IStorageBackend* backend);
    explicit AsyncPersistenceQueue(IStorageBackend* backend,
                                   const Config& config);

    ~AsyncPersistenceQueue();

    bool Enqueue(const std::string& key, const VersionedData& data,
                 Priority priority);

    bool FlushAll(uint32_t timeout_ms = 5000);

    bool Flush(uint32_t timeout_ms = 5000) {
        return FlushAll(timeout_ms);
    }

    struct Stats {
        uint64_t enqueued_total = 0;
        uint64_t persisted_success = 0;
        uint64_t persisted_failed = 0;
        size_t high_priority_queue_depth = 0;
        size_t normal_priority_queue_depth = 0;
    };

    Stats GetStats() const;

    int64_t PendingCount() const;

private:
    struct PersistenceItem {
        std::string key;
        VersionedData data;
        uint64_t version;

        PersistenceItem() = default;
        PersistenceItem(std::string k, VersionedData d, uint64_t v)
            : key(std::move(k)), data(std::move(d)), version(v) {}
    };

    void HighPriorityWorker();
    void NormalBatchWorker();
    bool IsFlushDrained() const;
    void NotifyFlushCompletionIfDrained();

    bool PersistSingleItem(const PersistenceItem& item);
    bool PersistBatch(const std::vector<PersistenceItem>& batch);

    IStorageBackend* backend_ = nullptr;
    Config config_;

    std::unique_ptr<BlockingQueue<PersistenceItem>> high_priority_queue_;
    std::unique_ptr<BlockingQueue<PersistenceItem>> normal_priority_queue_;

    std::vector<std::thread> worker_threads_;
    std::atomic<bool> shutdown_{false};
    std::atomic<bool> flush_requested_{false};
    mutable std::mutex flush_mutex_;
    std::condition_variable flush_complete_;
    std::atomic<uint64_t> pending_items_{0};

    struct Statistics {
        std::atomic<uint64_t> enqueued_total{0};
        std::atomic<uint64_t> persisted_success{0};
        std::atomic<uint64_t> persisted_failed{0};
    };
    Statistics stats_;
};

}  // namespace mir2::storage_engine::persistence

#endif  // MIR2_STORAGE_ENGINE_PERSISTENCE_ASYNC_PERSISTENCE_QUEUE_H_
