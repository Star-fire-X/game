#include "async_persistence_queue.h"

#include <chrono>
#include <spdlog/spdlog.h>
#include <thread>

namespace mir2::cache {

AsyncPersistenceQueue::AsyncPersistenceQueue(
    std::shared_ptr<PostgresBackend> pg_backend, const Config& config)
    : pg_backend_(pg_backend), config_(config) {
    high_priority_queue_ =
        std::make_unique<BlockingQueue<PersistenceItem>>(config.queue_capacity);
    normal_priority_queue_ =
        std::make_unique<BlockingQueue<PersistenceItem>>(config.queue_capacity);

    // 启动高优先级工作线程
    worker_threads_.emplace_back([this] { HighPriorityWorker(); });

    // 启动普通优先级批处理工作线程
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

    // 等待所有工作线程退出
    for (auto& thread : worker_threads_) {
        if (thread.joinable()) {
            thread.join();
        }
    }
}

bool AsyncPersistenceQueue::Enqueue(const std::string& key,
                                    const VersionedData& data,
                                    Priority priority) {
    if (!pg_backend_ || !pg_backend_->IsHealthy()) {
        return false;
    }

    PersistenceItem item(key, data, data.version);

    bool success = false;
    if (priority == Priority::HIGH) {
        success = high_priority_queue_->Enqueue(item);
    } else {
        success = normal_priority_queue_->Enqueue(item);
    }

    if (success) {
        stats_.enqueued_total.fetch_add(1, std::memory_order_relaxed);
    }

    return success;
}

bool AsyncPersistenceQueue::FlushAll(uint32_t timeout_ms) {
    auto start_time = std::chrono::steady_clock::now();

    // 标记关闭，停止接受新请求
    shutdown_.store(true, std::memory_order_release);

    // 等待高优先级队列清空
    while (!high_priority_queue_->IsEmpty()) {
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                           std::chrono::steady_clock::now() - start_time)
                           .count();
        if (elapsed > timeout_ms) {
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    // 等待普通优先级队列清空
    while (!normal_priority_queue_->IsEmpty()) {
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                           std::chrono::steady_clock::now() - start_time)
                           .count();
        if (elapsed > timeout_ms) {
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    return true;
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

void AsyncPersistenceQueue::HighPriorityWorker() {
    auto logger = spdlog::get("mir2");

    while (!shutdown_.load(std::memory_order_acquire)) {
        // 带 500ms 超时的阻塞等待
        auto item = high_priority_queue_->Pop(500);

        if (!item) {
            continue;
        }

        // 执行持久化
        if (PersistSingleItem(item.value())) {
            stats_.persisted_success.fetch_add(1, std::memory_order_relaxed);
        } else {
            stats_.persisted_failed.fetch_add(1, std::memory_order_relaxed);
            if (logger) {
                logger->warn("HighPriorityWorker: Failed to persist key '{}'",
                             item.value().key);
            }
        }
    }

    // 关闭后处理剩余项
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
    }
}

void AsyncPersistenceQueue::NormalBatchWorker() {
    auto logger = spdlog::get("mir2");
    std::vector<PersistenceItem> batch;
    batch.reserve(config_.batch_size);

    auto last_flush_time = std::chrono::steady_clock::now();

    while (!shutdown_.load(std::memory_order_acquire)) {
        auto item = normal_priority_queue_->Pop(100);  // 100ms 超时

        if (item) {
            batch.push_back(item.value());
        }

        auto now = std::chrono::steady_clock::now();
        auto elapsed =
            std::chrono::duration_cast<std::chrono::milliseconds>(now - last_flush_time)
                .count();

        // 满足任一条件则执行批处理：
        // 1. 积累达到 batch_size
        // 2. 时间超过 batch_interval_ms
        bool should_flush =
            batch.size() >= config_.batch_size ||
            (elapsed >= static_cast<int64_t>(config_.batch_interval_ms) && !batch.empty());

        if (should_flush) {
            if (PersistBatch(batch)) {
                stats_.persisted_success.fetch_add(batch.size(),
                                                   std::memory_order_relaxed);
            } else {
                stats_.persisted_failed.fetch_add(batch.size(),
                                                  std::memory_order_relaxed);
                if (logger) {
                    logger->warn("NormalBatchWorker: Batch persist failed, {} items",
                                 batch.size());
                }
            }

            batch.clear();
            last_flush_time = now;
        }
    }

    // 关闭后处理剩余批次
    while (true) {
        auto item = normal_priority_queue_->Pop(0);
        if (!item) {
            break;
        }
        batch.push_back(item.value());

        if (batch.size() >= config_.batch_size) {
            PersistBatch(batch);
            batch.clear();
        }
    }

    // 处理最后的剩余项
    if (!batch.empty()) {
        PersistBatch(batch);
    }
}

bool AsyncPersistenceQueue::PersistSingleItem(const PersistenceItem& item) {
    if (!pg_backend_ || !pg_backend_->IsHealthy()) {
        return false;
    }

    for (uint32_t retry = 0; retry <= config_.retry_count; ++retry) {
        if (pg_backend_->SaveEntity(item.key, item.data, item.version)) {
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
    if (batch.empty() || !pg_backend_ || !pg_backend_->IsHealthy()) {
        return false;
    }

    // 准备批量操作的数据格式
    std::vector<std::pair<std::string, VersionedData>> batch_data;
    batch_data.reserve(batch.size());

    for (const auto& item : batch) {
        batch_data.emplace_back(item.key, item.data);
    }

    for (uint32_t retry = 0; retry <= config_.retry_count; ++retry) {
        if (pg_backend_->IsHealthy()) {
            uint32_t saved_count = pg_backend_->BatchSaveEntities(batch_data);
            if (saved_count == batch.size()) {
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

}  // namespace mir2::cache
