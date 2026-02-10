#ifndef MIR2_SERVER_CACHE_TIERED_CACHE_H_
#define MIR2_SERVER_CACHE_TIERED_CACHE_H_

#include <atomic>
#include <chrono>
#include <cstdint>
#include <future>
#include <memory>
#include <optional>
#include <shared_mutex>
#include <string>
#include <thread>
#include <vector>

#include "async_persistence_queue.h"
#include "cache_types.h"
#include "circuit_breaker.h"
#include "global_hybrid_clock.h"
#include "local_lru_cache.h"
#include "postgres_backend_interface.h"
#include "rocksdb_cache.h"

namespace mir2::cache {

// 多层缓存协调器
// 协调三层缓存：L1 内存 + L2 RocksDB + PostgreSQL
// 主要职责：
// - 提供统一的缓存读写接口
// - 版本控制与并发冲突处理
// - 熔断降级机制
// - 异步持久化
class TieredCache {
public:
    // 配置结构体
    struct Config {
        LocalLRUCache::Config l1_config;
        RocksDBCache::Config l2_config;
        AsyncPersistenceQueue::Config pg_queue_config;
        CircuitBreaker::Config circuit_breaker_config;
    };

    // 构造函数
    TieredCache(std::shared_ptr<PostgresBackend> pg_backend, const Config& config);

    // 析构函数
    ~TieredCache();

    // API 1: SetState - 直接覆盖写入（位置更新、快照）
    // 特点：无版本检查，直接覆盖
    // 性能目标：<1ms
    bool SetState(const std::string& key, const std::vector<uint8_t>& data,
                  Priority priority = Priority::NORMAL);

    // API 2: UpdateWithRetry - 条件更新（金币、物品栏）
    // 特点：基于当前值计算新值，自动重试处理版本冲突
    // 性能目标：<5ms（含重试）
    template <typename UpdateFunc>
    bool UpdateWithRetry(const std::string& key, UpdateFunc update_fn,
                         int max_retries = 5,
                         Priority priority = Priority::HIGH);

    // API 3: Get - 纯缓存读取（L1 -> L2，无 DB）
    // 特点：L2 命中时自动回填 L1
    // 性能目标：L1 <1ms，L2 <5ms
    std::optional<VersionedData> Get(const std::string& key);

    // API 4: LoadFromDBAsync - 异步加载数据（玩家登录、数据恢复）
    // 特点：异步操作，自动回填 L2 + L1
    // 返回 std::future，允许异步等待
    std::future<std::optional<VersionedData>> LoadFromDBAsync(
        const std::string& key);

    // 健康检查接口
    bool IsHealthy() const;

    // 获取健康指标
    struct HealthMetrics {
        bool is_healthy;
        CircuitBreaker::State l2_state;
        double l1_hit_ratio;
        double l2_hit_ratio;
        int64_t hlc_time_drift_ms;
        size_t pg_queue_depth;
        uint32_t l2_consecutive_failures;
    };

    HealthMetrics GetHealthMetrics() const;

    // 优雅关闭
    void Shutdown();

private:
    // 内部辅助函数：写入单条数据到所有层
    bool WriteToAllLayers(const std::string& key, const VersionedData& data,
                          Priority priority);

    // 内部辅助函数：写入到 L2（考虑熔断）
    bool WriteToL2(const std::string& key, const VersionedData& data);

    // 内部辅助函数：写入到 L1
    void WriteToL1(const std::string& key, const VersionedData& data);

    // 内部辅助函数：写入到异步队列
    bool WriteToAsyncQueue(const std::string& key, const VersionedData& data,
                           Priority priority);

    // 降级模式写入（L2 故障时）
    bool WriteDegradedMode(const std::string& key, const VersionedData& data,
                           Priority priority);

    // PostgreSQL 后端
    std::shared_ptr<PostgresBackend> pg_backend_;

    // 配置
    Config config_;

    // 三层缓存实例
    std::shared_ptr<LocalLRUCache> l1_cache_;
    std::shared_ptr<RocksDBCache> l2_cache_;
    std::shared_ptr<AsyncPersistenceQueue> pg_queue_;

    // 全局混合逻辑时钟
    std::unique_ptr<GlobalHybridClock> hlc_;

    // L2 熔断器
    std::unique_ptr<CircuitBreaker> l2_circuit_breaker_;

    // 关闭标志
    std::atomic<bool> shutdown_{false};

    // DB 加载工作线程池
    std::vector<std::thread> db_loaders_;
    std::atomic<size_t> active_loaders_{0};
    static constexpr size_t kMaxDBLoaders = 4;

    // 统计信息（原子访问）
    struct Statistics {
        std::atomic<uint64_t> l1_hits{0};
        std::atomic<uint64_t> l1_misses{0};
        std::atomic<uint64_t> l2_hits{0};
        std::atomic<uint64_t> l2_misses{0};
        std::atomic<uint64_t> update_retries{0};
        std::atomic<uint64_t> update_conflicts{0};
    };
    Statistics stats_;
};

// 内部辅助：获取当前时间毫秒数
inline uint64_t GetCurrentTimeMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

// 模板实现：UpdateWithRetry
template <typename UpdateFunc>
bool TieredCache::UpdateWithRetry(const std::string& key, UpdateFunc update_fn,
                                  int max_retries, Priority priority) {
    if (shutdown_.load(std::memory_order_acquire)) {
        return false;
    }

    for (int attempt = 0; attempt < max_retries; ++attempt) {
        // 步骤 1: 从 L1 读取当前值
        auto current_data = Get(key);

        uint64_t old_version = 0;
        if (current_data) {
            old_version = current_data->version;
        }

        // 步骤 2: 业务逻辑计算新值
        auto new_data_opt = update_fn(current_data);
        if (!new_data_opt) {
            return false;  // 业务逻辑返回失败
        }

        auto& new_data_content = new_data_opt.value();

        // 步骤 3: 生成新版本号
        uint64_t new_version = hlc_->Next();

        // 步骤 4: 版本检查（CAS）
        // 如果 L1 中的版本已经更新，则重试
        auto current_in_l1 = l1_cache_->Get(key);
        if (current_in_l1 && current_in_l1->version > old_version) {
            stats_.update_conflicts.fetch_add(1, std::memory_order_relaxed);
            stats_.update_retries.fetch_add(1, std::memory_order_relaxed);

            // 指数退避重试
            if (attempt < max_retries - 1) {
                std::this_thread::sleep_for(
                    std::chrono::milliseconds(10 * (attempt + 1)));
                continue;
            }
            return false;
        }

        // 步骤 5: L2 + L1 写入
        VersionedData new_versioned_data(new_version, new_data_content,
                                         GetCurrentTimeMs());

        if (!WriteToAllLayers(key, new_versioned_data, priority)) {
            return false;
        }

        return true;
    }

    return false;
}

}  // namespace mir2::cache

#endif  // MIR2_SERVER_CACHE_TIERED_CACHE_H_
