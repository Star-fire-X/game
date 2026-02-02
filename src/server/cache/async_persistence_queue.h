#ifndef MIR2_SERVER_CACHE_ASYNC_PERSISTENCE_QUEUE_H_
#define MIR2_SERVER_CACHE_ASYNC_PERSISTENCE_QUEUE_H_

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "blocking_queue.h"
#include "cache_types.h"
#include "postgres_backend_interface.h"

namespace mir2::cache {

// 异步持久化队列
// 负责将缓存数据异步持久化到 PostgreSQL
// 支持两个优先级队列：
// - 高优先级：立即单条提交 (<100ms)
// - 普通优先级：批量提交 (30s 或 100 条触发)
class AsyncPersistenceQueue {
public:
    // 队列配置结构体
    struct Config {
        // 队列容量限制
        size_t queue_capacity = 10000;

        // 普通优先级批处理间隔（毫秒）
        uint32_t batch_interval_ms = 30000;

        // 普通优先级批处理大小
        size_t batch_size = 100;

        // 工作线程数
        size_t worker_threads = 2;

        // 失败重试次数
        uint32_t retry_count = 3;

        // 重试延迟（毫秒）
        uint32_t retry_delay_ms = 100;
    };

    // 构造函数
    explicit AsyncPersistenceQueue(std::shared_ptr<PostgresBackend> pg_backend,
                                   const Config& config = Config());

    // 析构函数
    ~AsyncPersistenceQueue();

    // 入队操作
    // 返回 true 表示入队成功，false 表示队列已满或其他错误
    bool Enqueue(const std::string& key, const VersionedData& data,
                 Priority priority);

    // 优雅关闭，等待所有待处理任务完成
    // timeout_ms: 最多等待时间（毫秒）
    // 返回 true 表示所有任务完成，false 表示超时
    bool FlushAll(uint32_t timeout_ms = 5000);

    // 获取队列统计信息
    struct Stats {
        uint64_t enqueued_total = 0;        // 入队总数
        uint64_t persisted_success = 0;     // 持久化成功数
        uint64_t persisted_failed = 0;      // 持久化失败数
        size_t high_priority_queue_depth = 0;   // 高优先级队列深度
        size_t normal_priority_queue_depth = 0; // 普通优先级队列深度
    };

    Stats GetStats() const;

private:
    // 持久化队列中的条目
    struct PersistenceItem {
        std::string key;
        VersionedData data;
        uint64_t version;

        PersistenceItem() = default;
        PersistenceItem(std::string k, VersionedData d, uint64_t v)
            : key(std::move(k)), data(std::move(d)), version(v) {}
    };

    // 高优先级工作线程处理函数
    void HighPriorityWorker();

    // 普通优先级批处理工作线程处理函数
    void NormalBatchWorker();

    // 执行单条持久化，带重试逻辑
    bool PersistSingleItem(const PersistenceItem& item);

    // 执行批量持久化，带重试逻辑
    bool PersistBatch(const std::vector<PersistenceItem>& batch);

    // PostgreSQL 后端
    std::shared_ptr<PostgresBackend> pg_backend_;

    // 配置信息
    Config config_;

    // 高优先级队列
    std::unique_ptr<BlockingQueue<PersistenceItem>> high_priority_queue_;

    // 普通优先级队列
    std::unique_ptr<BlockingQueue<PersistenceItem>> normal_priority_queue_;

    // 工作线程
    std::vector<std::thread> worker_threads_;

    // 关闭标志
    std::atomic<bool> shutdown_{false};

    // 统计信息
    struct Statistics {
        std::atomic<uint64_t> enqueued_total{0};
        std::atomic<uint64_t> persisted_success{0};
        std::atomic<uint64_t> persisted_failed{0};
    };
    Statistics stats_;
};

}  // namespace mir2::cache

#endif  // MIR2_SERVER_CACHE_ASYNC_PERSISTENCE_QUEUE_H_
