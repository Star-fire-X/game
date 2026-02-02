#include "tiered_cache.h"

#include <chrono>
#include <spdlog/spdlog.h>

namespace mir2::cache {

// 获取当前系统时间（毫秒）
inline uint64_t GetCurrentTimeMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

TieredCache::TieredCache(std::shared_ptr<PostgresBackend> pg_backend,
                         const Config& config)
    : pg_backend_(pg_backend), config_(config) {
    auto logger = spdlog::get("mir2");

    // 初始化 L1 缓存
    l1_cache_ = std::make_shared<LocalLRUCache>(config_.l1_config);
    if (logger) {
        logger->info("TieredCache: L1 cache initialized with capacity={}",
                     config_.l1_config.capacity);
    }

    // 初始化 L2 缓存
    l2_cache_ = std::make_shared<RocksDBCache>(config_.l2_config);
    if (!l2_cache_->Initialize()) {
        if (logger) {
            logger->error("TieredCache: Failed to initialize L2 cache");
        }
        throw std::runtime_error("Failed to initialize L2 cache");
    }

    // 初始化 HLC
    hlc_ = std::make_unique<GlobalHybridClock>(l2_cache_.get());
    hlc_->RecoverFromPersistence();

    // 初始化 L2 熔断器
    l2_circuit_breaker_ = std::make_unique<CircuitBreaker>(
        config_.circuit_breaker_config);

    // 初始化异步持久化队列
    pg_queue_ = std::make_shared<AsyncPersistenceQueue>(pg_backend,
                                                        config_.pg_queue_config);

    if (logger) {
        logger->info("TieredCache: Fully initialized with all three layers");
    }
}

TieredCache::~TieredCache() {
    Shutdown();
}

bool TieredCache::SetState(const std::string& key,
                           const std::vector<uint8_t>& data,
                           Priority priority) {
    if (shutdown_.load(std::memory_order_acquire)) {
        return false;
    }

    // 生成新版本号
    uint64_t version = hlc_->Next();

    // 创建 VersionedData
    auto data_ptr =
        std::make_shared<const std::vector<uint8_t>>(data.begin(), data.end());
    VersionedData versioned_data(version, data_ptr, GetCurrentTimeMs());

    // 写入所有层
    return WriteToAllLayers(key, versioned_data, priority);
}

std::optional<VersionedData> TieredCache::Get(const std::string& key) {
    if (shutdown_.load(std::memory_order_acquire)) {
        return std::nullopt;
    }

    // 步骤 1: 查询 L1
    auto l1_result = l1_cache_->Get(key);
    if (l1_result) {
        stats_.l1_hits.fetch_add(1, std::memory_order_relaxed);
        return l1_result;
    }

    stats_.l1_misses.fetch_add(1, std::memory_order_relaxed);

    // 步骤 2: 查询 L2（带熔断检查）
    if (l2_circuit_breaker_->IsOpen()) {
        return std::nullopt;
    }

    auto l2_result = l2_circuit_breaker_->Execute([this, &key]() {
        return l2_cache_->Get(key);
    });

    if (!l2_result || !l2_result.value()) {
        stats_.l2_misses.fetch_add(1, std::memory_order_relaxed);
        return std::nullopt;
    }

    stats_.l2_hits.fetch_add(1, std::memory_order_relaxed);

    // 步骤 3: 回填 L1
    WriteToL1(key, l2_result.value().value());

    return l2_result.value();
}

std::future<std::optional<VersionedData>> TieredCache::LoadFromDBAsync(
    const std::string& key) {
    if (shutdown_.load(std::memory_order_acquire)) {
        auto promise = std::make_unique<std::promise<std::optional<VersionedData>>>();
        promise->set_value(std::nullopt);
        return promise->get_future();
    }

    // 使用 std::async 异步加载
    return std::async(std::launch::async, [this, key]() -> std::optional<VersionedData> {
        if (!pg_backend_ || !pg_backend_->IsHealthy()) {
            return std::nullopt;
        }

        // 从 PostgreSQL 加载
        auto data = pg_backend_->LoadEntity(key);
        if (!data) {
            return std::nullopt;
        }

        // 回填 L2 和 L1
        if (!WriteToL2(key, data.value())) {
            return std::nullopt;
        }

        WriteToL1(key, data.value());

        return data;
    });
}

bool TieredCache::IsHealthy() const {
    return !l2_circuit_breaker_->IsOpen() && pg_backend_ &&
           pg_backend_->IsHealthy();
}

TieredCache::HealthMetrics TieredCache::GetHealthMetrics() const {
    auto l1_stats = l1_cache_->GetStats();
    auto cb_stats = l2_circuit_breaker_->GetStats();

    double l1_hit_ratio = l1_stats.GetHitRatio();
    double l2_hit_ratio = 0.0;
    if (stats_.l2_hits.load(std::memory_order_relaxed) +
            stats_.l2_misses.load(std::memory_order_relaxed) >
        0) {
        uint64_t hits = stats_.l2_hits.load(std::memory_order_relaxed);
        uint64_t total = hits + stats_.l2_misses.load(std::memory_order_relaxed);
        l2_hit_ratio = static_cast<double>(hits) / total;
    }

    auto pg_stats = pg_queue_->GetStats();

    return HealthMetrics{
        .is_healthy = IsHealthy(),
        .l2_state = cb_stats.state,
        .l1_hit_ratio = l1_hit_ratio,
        .l2_hit_ratio = l2_hit_ratio,
        .hlc_time_drift_ms = 0,  // 可选：添加时钟漂移检测
        .pg_queue_depth = pg_stats.high_priority_queue_depth +
                          pg_stats.normal_priority_queue_depth,
        .l2_consecutive_failures = cb_stats.consecutive_failures,
    };
}

void TieredCache::Shutdown() {
    shutdown_.store(true, std::memory_order_release);

    // 优雅关闭异步队列
    if (pg_queue_) {
        pg_queue_->FlushAll(5000);
    }

    auto logger = spdlog::get("mir2");
    if (logger) {
        logger->info("TieredCache: Gracefully shutdown");
    }
}

bool TieredCache::WriteToAllLayers(const std::string& key,
                                   const VersionedData& data,
                                   Priority priority) {
    // 检查 L2 熔断器状态
    if (l2_circuit_breaker_->IsOpen()) {
        return WriteDegradedMode(key, data, priority);
    }

    // 正常模式：L2 -> L1 -> AsyncQueue
    // 步骤 1: 写入 L2（优先）
    if (!WriteToL2(key, data)) {
        return false;
    }

    // 步骤 2: 写入 L1
    WriteToL1(key, data);

    // 步骤 3: 写入异步队列
    return WriteToAsyncQueue(key, data, priority);
}

bool TieredCache::WriteToL2(const std::string& key, const VersionedData& data) {
    return l2_circuit_breaker_->Execute([this, &key, &data]() {
        return l2_cache_->Set(key, data);
    })
        .has_value();
}

void TieredCache::WriteToL1(const std::string& key, const VersionedData& data) {
    l1_cache_->Set(key, data);
}

bool TieredCache::WriteToAsyncQueue(const std::string& key,
                                    const VersionedData& data,
                                    Priority priority) {
    return pg_queue_->Enqueue(key, data, priority);
}

bool TieredCache::WriteDegradedMode(const std::string& key,
                                    const VersionedData& data,
                                    Priority priority) {
    auto logger = spdlog::get("mir2");

    if (logger) {
        logger->warn(
            "TieredCache: L2 circuit breaker OPEN, entering degraded mode for key '{}'",
            key);
    }

    // 降级模式：L1 + 高优先级 PG
    WriteToL1(key, data);

    // 使用高优先级确保数据持久化
    return WriteToAsyncQueue(key, data, Priority::HIGH);
}

}  // namespace mir2::cache
