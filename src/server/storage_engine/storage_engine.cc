/**
 * @file storage_engine.cc
 * @brief StorageEngine实现 - PIMPL + 分段锁
 */

#include "storage_engine/storage_engine.h"
#include "logic/coroutine_executor.h"
#include "monitor/metrics.h"
#include "storage_engine/l2/rocksdb_cache.h"
#include "storage_engine/l1/memory_cache.h"
#include "storage_engine/interfaces/storage_backend.h"
#include "storage_engine/persistence/async_persistence_queue.h"
#include "storage_engine/utils/circuit_breaker.h"
#include "storage_engine/utils/global_hybrid_clock.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <chrono>
#include <mutex>
#include <shared_mutex>
#include <string_view>
#include <thread>
#include <vector>

namespace mir2::storage_engine {

// ===== FNV-1a哈希算法 =====
namespace detail {

inline uint64_t fnv1a_hash(const std::string& str) noexcept {
    constexpr uint64_t kFNVPrime = 1099511628211ULL;
    constexpr uint64_t kFNVOffsetBasis = 14695981039346656037ULL;

    uint64_t hash = kFNVOffsetBasis;
    for (unsigned char c : str) {
        hash ^= static_cast<uint64_t>(c);
        hash *= kFNVPrime;
    }
    return hash;
}

inline uint64_t GetCurrentTimeMs() noexcept {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
}

}  // namespace detail

namespace {
constexpr const char* kL1HitRatioMetric = "storage.l1_hit_ratio";
constexpr size_t kMaxStorageKeyLength = 512;
constexpr size_t kMaxStorageValueSize = 4 * 1024 * 1024;  // 4MB
constexpr uint64_t kL1MetricPublishIntervalMs = 1000;
constexpr uint64_t kL1MetricPublishRequestInterval = 256;
constexpr size_t kUpdateLatencySampleWindow = 512;
constexpr int kMaxConflictYieldRounds = 64;
constexpr std::string_view kSensitiveAccountKeyPrefix = "account:username:";

bool IsValidStorageKey(const std::string& key) {
    if (key.empty() || key.size() > kMaxStorageKeyLength) {
        return false;
    }
    return key.find('\0') == std::string::npos;
}

bool IsSensitiveStorageKey(const std::string& key) {
    return std::string_view(key).starts_with(kSensitiveAccountKeyPrefix);
}

// Lock-free fast path for StorageEngine::Instance().
std::atomic<StorageEngine*> g_instance_ptr{nullptr};
}  // namespace

// ===== PIMPL实现类 =====
class StorageEngine::Impl {
public:
    explicit Impl(std::unique_ptr<IStorageBackend> backend,
                  const Config& config)
        : backend_(std::move(backend))
        , config_(config)
        , shutdown_(false) {
        auto logger = spdlog::get("mir2");

        l1::MemoryCache::Config l1_config;
        l1_config.capacity = config_.l1_max_entries;
        l1_config.ttl_seconds = config_.l1_ttl_seconds;
        l1_cache_ = std::make_unique<l1::MemoryCache>(l1_config);
        l2::RocksDBCache::Config l2_config;
        l2_config.db_path = config_.l2_path;
        l2_config.block_cache_size =
            static_cast<size_t>(config_.l2_max_size_mb) * 1024 * 1024;
        l2_config.ttl_seconds = static_cast<int32_t>(config_.l2_ttl_seconds);
        l2_cache_ = std::make_unique<l2::RocksDBCache>(l2_config);
        if (!l2_cache_->Initialize()) {
            if (logger) {
                logger->error("Failed to initialize L2 cache at {}", config_.l2_path);
            }
            l2_cache_.reset();
        }
        hlc_ = std::make_unique<utils::GlobalHybridClock>(l2_cache_.get());
        utils::CircuitBreaker::Config cb_config;
        cb_config.failure_threshold = config_.circuit_breaker_threshold;
        cb_config.open_timeout_ms = config_.circuit_breaker_timeout_ms;
        circuit_breaker_ = std::make_unique<utils::CircuitBreaker>(cb_config);
        backend_circuit_breaker_ =
            std::make_unique<utils::CircuitBreaker>(cb_config);
        if (backend_) {
            persistence::AsyncPersistenceQueue::Config queue_config;
            queue_config.batch_size = config_.batch_size;
            queue_config.batch_interval_ms = config_.auto_sync_interval_ms;
            async_queue_ = std::make_unique<persistence::AsyncPersistenceQueue>(
                backend_.get(), queue_config);
        }

        if (logger) {
            logger->info("StorageEngine initialized: l1_max={}, l2_path={}",
                         config_.l1_max_entries, config_.l2_path);
        }
    }

    ~Impl() {
        Shutdown();
    }

    void Shutdown() {
        if (shutdown_.exchange(true)) {
            return;  // 已关闭
        }

        auto logger = spdlog::get("mir2");
        if (logger) {
            logger->info("StorageEngine shutting down...");
        }

        if (async_queue_) {
            async_queue_->Flush();
        }

        if (l2_cache_) {
            // RocksDB handles its own cleanup on destruction.
            l2_cache_.reset();
        }

        if (logger) {
            logger->info("StorageEngine shutdown complete");
        }
    }

    // ===== 分段锁配置 =====
    static constexpr size_t kLockStripes = 64;
    std::array<std::shared_mutex, kLockStripes> locks_;

    size_t GetStripe(const std::string& key) const noexcept {
        uint64_t hash = detail::fnv1a_hash(key);
        return hash & (kLockStripes - 1);  // 位掩码，等价于 % 64
    }

    std::shared_mutex& GetSegmentLock(const std::string& key) noexcept {
        return locks_[GetStripe(key)];
    }

    // ===== Get实现 =====
    std::optional<VersionedData> GetFromL1Internal(const std::string& key,
                                                   bool update_stats) {
        if (shutdown_.load(std::memory_order_acquire)) {
            return std::nullopt;
        }

        if (IsSensitiveStorageKey(key)) {
            if (l1_cache_) {
                l1_cache_->Delete(key);
            }
            if (update_stats) {
                stats_.l1_misses.fetch_add(1, std::memory_order_relaxed);
                MaybePublishL1HitRatio();
            }
            return std::nullopt;
        }

        if (l1_cache_) {
            auto cached = l1_cache_->Get(key);
            if (cached) {
                if (update_stats) {
                    stats_.l1_hits.fetch_add(1, std::memory_order_relaxed);
                    MaybePublishL1HitRatio();
                }
                return cached;
            }
        }

        if (update_stats) {
            stats_.l1_misses.fetch_add(1, std::memory_order_relaxed);
            MaybePublishL1HitRatio();
        }
        return std::nullopt;
    }

    std::optional<VersionedData> GetFromL2Internal(const std::string& key) {
        if (shutdown_.load(std::memory_order_acquire)) {
            return std::nullopt;
        }

        if (IsSensitiveStorageKey(key)) {
            if (l2_cache_ && (!circuit_breaker_ || !circuit_breaker_->IsOpen())) {
                stats_.l2_misses.fetch_add(1, std::memory_order_relaxed);
            }
            return std::nullopt;
        }

        if (l2_cache_ && (!circuit_breaker_ || !circuit_breaker_->IsOpen())) {
            auto l2_result = l2_cache_->Get(key);
            if (l2_result) {
                stats_.l2_hits.fetch_add(1, std::memory_order_relaxed);
                if (l1_cache_) {
                    l1_cache_->Set(key, *l2_result);
                }
                return l2_result;
            }
            stats_.l2_misses.fetch_add(1, std::memory_order_relaxed);
        }

        return std::nullopt;
    }

    std::optional<VersionedData> GetInternal(const std::string& key,
                                             bool update_l1_stats = true) {
        auto cached = GetFromL1Internal(key, update_l1_stats);
        if (cached) {
            return cached;
        }

        return GetFromL2Internal(key);
    }

    // ===== Set实现 =====
    bool SetInternal(const std::string& key,
                     const VersionedData& data,
                     Priority priority) {
        if (shutdown_.load(std::memory_order_acquire)) {
            return false;
        }

        if (IsSensitiveStorageKey(key)) {
            if (!backend_ || !backend_->IsHealthy()) {
                auto logger = spdlog::get("mir2");
                if (logger) {
                    logger->warn(
                        "StorageEngine Set rejected for sensitive key: backend unavailable, key={}",
                        key);
                }
                return false;
            }

            auto save_result = backend_->Save(key, data.version, data.data);
            if (!save_result.success) {
                auto logger = spdlog::get("mir2");
                if (logger) {
                    logger->warn(
                        "StorageEngine Set failed for sensitive key={}, error={}",
                        key, save_result.error_message);
                }
                return false;
            }

            if (l1_cache_) {
                l1_cache_->Delete(key);
            }

            stats_.total_sets.fetch_add(1, std::memory_order_relaxed);
            return true;
        }

        bool l2_persisted = false;
        bool queued_for_persistence = false;

        if (l2_cache_ && (!circuit_breaker_ || !circuit_breaker_->IsOpen())) {
            l2_persisted = l2_cache_->Set(key, data);
            if (circuit_breaker_) {
                if (l2_persisted) {
                    circuit_breaker_->OnSuccess();
                } else {
                    circuit_breaker_->OnFailure();
                }
            }
        }

        if (async_queue_) {
            queued_for_persistence = async_queue_->Enqueue(key, data, priority);
        }

        if (!l2_persisted && !queued_for_persistence) {
            auto logger = spdlog::get("mir2");
            if (logger) {
                logger->warn(
                    "StorageEngine Set rejected: no persistence path available, key={}",
                    key);
            }
            return false;
        }

        if (l1_cache_) {
            l1_cache_->Set(key, data);
        }

        stats_.total_sets.fetch_add(1, std::memory_order_relaxed);
        return true;
    }

    bool SetSyncInternal(const std::string& key,
                         const VersionedData& data,
                         Priority priority) {
        if (shutdown_.load(std::memory_order_acquire)) {
            return false;
        }

        if (IsSensitiveStorageKey(key)) {
            if (!backend_ || !backend_->IsHealthy()) {
                auto logger = spdlog::get("mir2");
                if (logger) {
                    logger->error(
                        "StorageEngine SetSync failed for sensitive key: backend unavailable, key={}",
                        key);
                }
                return false;
            }

            auto save_result = backend_->Save(key, data.version, data.data);
            if (!save_result.success) {
                auto logger = spdlog::get("mir2");
                if (logger) {
                    logger->error(
                        "StorageEngine SetSync failed for sensitive key={}, error={}",
                        key, save_result.error_message);
                }
                return false;
            }

            if (l1_cache_) {
                l1_cache_->Delete(key);
            }

            stats_.total_sets.fetch_add(1, std::memory_order_relaxed);
            return true;
        }

        if (!l2_cache_) {
            auto logger = spdlog::get("mir2");
            if (logger) {
                logger->error("StorageEngine SetSync failed: L2 cache unavailable, key={}", key);
            }
            return false;
        }

        bool l2_success = l2_cache_->SetSync(key, data);
        if (circuit_breaker_) {
            if (l2_success) {
                circuit_breaker_->OnSuccess();
            } else {
                circuit_breaker_->OnFailure();
            }
        }
        if (!l2_success) {
            return false;
        }

        if (l1_cache_) {
            l1_cache_->Set(key, data);
        }

        if (async_queue_) {
            async_queue_->Enqueue(key, data, priority);
        }

        stats_.total_sets.fetch_add(1, std::memory_order_relaxed);
        return true;
    }

    // ===== Update实现 - 类型擦除版本 =====
    bool UpdateImpl(const std::string& key,
                    StorageEngine::UpdateFunction update_fn,
                    int max_retries,
                    Priority priority) {
        const auto update_start = std::chrono::steady_clock::now();
        if (shutdown_.load(std::memory_order_acquire)) {
            RecordUpdateLatency(update_start);
            return false;
        }

        auto logger = spdlog::get("mir2");
        size_t stripe = GetStripe(key);

        for (int attempt = 0; attempt < max_retries; ++attempt) {
            // 步骤1: 读锁 - 获取当前值
            std::optional<VersionedData> current_data;
            uint64_t old_version = 0;
            {
                std::shared_lock<std::shared_mutex> read_lock(locks_[stripe]);
                current_data = GetInternal(key);
                if (current_data) {
                    old_version = current_data->version;
                }
            }
            // 读锁已释放

            // 步骤2: 无锁计算 - 业务逻辑（纯计算）
#ifdef MIR2_DEBUG_MODE
            auto calc_start = std::chrono::high_resolution_clock::now();
#endif

            auto new_data_opt = update_fn(current_data);

#ifdef MIR2_DEBUG_MODE
            auto calc_duration = std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::high_resolution_clock::now() - calc_start
            );

            constexpr int64_t kMaxCalcTimeMicros = 100;
            if (calc_duration.count() > kMaxCalcTimeMicros) {
                if (logger) {
                    logger->warn("update_fn took {}μs (threshold: {}μs), key={}",
                                 calc_duration.count(), kMaxCalcTimeMicros, key);
                }
                stats_.slow_update_fn_count.fetch_add(1, std::memory_order_relaxed);
            }
#endif

            if (!new_data_opt) {
                // 业务逻辑主动放弃
                stats_.update_aborted.fetch_add(1, std::memory_order_relaxed);
                RecordUpdateLatency(update_start);
                return true;
            }

            // 步骤3: 写锁 - CAS检查 + 写入
            {
                std::unique_lock<std::shared_mutex> write_lock(locks_[stripe]);

                // CAS验证
                auto verify_data = GetInternal(key, false);
                uint64_t current_version = verify_data ? verify_data->version : 0;

                if (current_version != old_version) {
                    // 版本冲突
                    stats_.update_conflicts.fetch_add(1, std::memory_order_relaxed);
                    stats_.update_retries.fetch_add(1, std::memory_order_relaxed);

                    if (attempt < max_retries - 1) {
                        // Use cooperative yielding instead of sleep-based blocking.
                        // This reduces retry contention without pinning worker threads.
                        write_lock.unlock();
                        ApplyConflictBackoff(attempt);
                        continue;
                    }

                    if (logger) {
                        logger->warn("Update max retries exceeded, key={}", key);
                    }
                    RecordUpdateLatency(update_start);
                    return false;
                }

                // CAS成功，写入新值
                uint64_t new_version = NextVersion();
                VersionedData new_versioned_data{
                    new_version,
                    std::move(new_data_opt.value()),
                    detail::GetCurrentTimeMs()
                };

                bool write_success = SetInternal(key, new_versioned_data, priority);
                if (!write_success) {
                    RecordUpdateLatency(update_start);
                    return false;
                }

                stats_.successful_updates.fetch_add(1, std::memory_order_relaxed);
                stats_.total_updates.fetch_add(1, std::memory_order_relaxed);
                RecordUpdateLatency(update_start);
                return true;
            }
        }

        RecordUpdateLatency(update_start);
        return false;
    }

    // ===== LoadFromDB实现 =====
    std::optional<VersionedData> LoadFromDBInternal(const std::string& key) {
        if (!backend_ || shutdown_.load(std::memory_order_acquire)) {
            return std::nullopt;
        }

        if (backend_circuit_breaker_ && backend_circuit_breaker_->IsOpen()) {
            return std::nullopt;
        }

        if (!backend_->IsHealthy()) {
            if (backend_circuit_breaker_) {
                backend_circuit_breaker_->OnFailure();
            }
            return std::nullopt;
        }

        auto result = backend_->Load(key);
        if (backend_circuit_breaker_) {
            if (backend_->IsHealthy()) {
                backend_circuit_breaker_->OnSuccess();
            } else {
                backend_circuit_breaker_->OnFailure();
            }
        }
        if (!result) {
            return std::nullopt;
        }

        auto& [version, data] = *result;
        VersionedData loaded{version, std::move(data), detail::GetCurrentTimeMs()};
        if (IsSensitiveStorageKey(key)) {
            if (l1_cache_) {
                l1_cache_->Delete(key);
            }
            return loaded;
        }

        {
            auto& seg = GetSegmentLock(key);
            std::unique_lock<std::shared_mutex> lock(seg);

            if (l2_cache_) {
                l2_cache_->Set(key, loaded);
            }
            if (l1_cache_) {
                l1_cache_->Set(key, loaded);
            }
        }
        return loaded;
    }

    // ===== Flush =====
    bool FlushInternal(uint32_t timeout_ms) {
        if (async_queue_) {
            return async_queue_->FlushAll(timeout_ms);
        }
        return true;
    }

    // ===== Startup Recovery =====
    bool PerformStartupRecoveryInternal() {
        if (!l2_cache_ || !backend_) {
            return true;  // Nothing to recover.
        }

        auto logger = spdlog::get("mir2");
        size_t recovered = 0;
        size_t errors = 0;

        l2_cache_->ForEach([&](const std::string& key, const VersionedData& l2_data) -> bool {
            auto pg_result = backend_->Load(key);
            uint64_t pg_version = pg_result ? pg_result->first : 0;

            if (l2_data.version > pg_version) {
                auto result = backend_->Save(key, l2_data.version, l2_data.data);
                if (result.success) {
                    ++recovered;
                } else {
                    ++errors;
                    if (logger) {
                        logger->error("Recovery failed for key={}: {}",
                                      key, result.error_message);
                    }
                }
            }
            return true;  // Continue iteration.
        });

        if (logger) {
            logger->info("Startup recovery: recovered={}, errors={}", recovered, errors);
        }
        return errors == 0;
    }

    // ===== 健康指标 =====
    StorageEngine::HealthMetrics GetHealthMetrics() const noexcept {
        uint64_t l1_hits = stats_.l1_hits.load(std::memory_order_relaxed);
        uint64_t l1_misses = stats_.l1_misses.load(std::memory_order_relaxed);
        uint64_t total_l1_requests = l1_hits + l1_misses;
        double l1_hit_ratio = total_l1_requests > 0
                                  ? static_cast<double>(l1_hits) / total_l1_requests
                                  : 0.0;

        uint64_t l2_hits = stats_.l2_hits.load(std::memory_order_relaxed);
        uint64_t l2_misses = stats_.l2_misses.load(std::memory_order_relaxed);
        uint64_t total_l2_requests = l2_hits + l2_misses;
        double l2_hit_ratio = total_l2_requests > 0
                                  ? static_cast<double>(l2_hits) / total_l2_requests
                                  : 0.0;

        size_t l1_size = l1_cache_ ? l1_cache_->GetSize() : 0;
        size_t l2_size = l2_cache_ ? l2_cache_->GetApproximateSizeBytes() : 0;

        int64_t pending_syncs = async_queue_ ? async_queue_->PendingCount() : 0;
        uint32_t breaker_failures = 0;
        if (circuit_breaker_) {
            breaker_failures += circuit_breaker_->FailureCount();
        }
        if (backend_circuit_breaker_) {
            breaker_failures += backend_circuit_breaker_->FailureCount();
        }

        double avg_update_latency_ms = 0.0;
        double p99_update_latency_ms = 0.0;
        {
            std::lock_guard<std::mutex> lock(update_latency_mutex_);
            if (update_latency_total_count_ > 0) {
                avg_update_latency_ms =
                    update_latency_total_ms_ /
                    static_cast<double>(update_latency_total_count_);
            }
            if (update_latency_sample_count_ > 0) {
                std::vector<double> samples;
                samples.reserve(update_latency_sample_count_);
                for (size_t i = 0; i < update_latency_sample_count_; ++i) {
                    samples.push_back(update_latency_samples_[i]);
                }
                std::sort(samples.begin(), samples.end());
                const size_t p99_index = static_cast<size_t>(
                    std::ceil(samples.size() * 0.99)) - 1;
                p99_update_latency_ms = samples[p99_index];
            }
        }

        return StorageEngine::HealthMetrics{
            .is_healthy = !shutdown_.load(std::memory_order_acquire),
            .l1_hit_ratio = l1_hit_ratio,
            .l2_hit_ratio = l2_hit_ratio,
            .l1_size = l1_size,
            .l2_size = l2_size,
            .pending_syncs = pending_syncs,
            .circuit_breaker_failures = breaker_failures,
            .total_updates = stats_.total_updates.load(std::memory_order_relaxed),
            .successful_updates = stats_.successful_updates.load(std::memory_order_relaxed),
            .update_conflicts = stats_.update_conflicts.load(std::memory_order_relaxed),
            .update_retries = stats_.update_retries.load(std::memory_order_relaxed),
            .update_aborted = stats_.update_aborted.load(std::memory_order_relaxed),
            .slow_update_fn_count = stats_.slow_update_fn_count.load(std::memory_order_relaxed),
            .avg_update_latency_ms = avg_update_latency_ms,
            .p99_update_latency_ms = p99_update_latency_ms
        };
    }

    uint64_t NextVersion() noexcept {
        if (hlc_) {
            return hlc_->Next();
        }
        return version_counter_.fetch_add(1, std::memory_order_relaxed);
    }

private:
    void ApplyConflictBackoff(int attempt) const noexcept {
        // Exponential yielding with a small cap keeps fairness under contention
        // while avoiding blocking sleeps inside hot update retries.
        const int yield_rounds = std::min(1 << std::min(attempt, 6),
                                          kMaxConflictYieldRounds);
        for (int i = 0; i < yield_rounds; ++i) {
            std::this_thread::yield();
        }
    }

    void MaybePublishL1HitRatio(bool force = false) {
        if (!config_.enable_metrics) {
            return;
        }

        uint64_t requests_since_last = 0;
        if (!force) {
            requests_since_last =
                l1_metric_publish_requests_.fetch_add(1, std::memory_order_relaxed) + 1;

            // Fast path: avoid clock reads on every Get().
            // We only evaluate time-based publish on the first request in a
            // cycle or when request volume reaches the publish interval.
            const bool first_request_in_cycle = requests_since_last == 1;
            const bool due_by_request =
                requests_since_last >= kL1MetricPublishRequestInterval;
            if (!first_request_in_cycle && !due_by_request) {
                return;
            }
        }

        bool expected = false;
        if (!l1_metric_publish_in_progress_.compare_exchange_strong(
                expected, true, std::memory_order_acq_rel,
                std::memory_order_relaxed)) {
            return;
        }

        const uint64_t now_ms = detail::GetCurrentTimeMs();
        const uint64_t last_published_ms =
            l1_metric_last_publish_ms_.load(std::memory_order_relaxed);
        const bool due_by_request =
            force || requests_since_last >= kL1MetricPublishRequestInterval;
        const bool due_by_time =
            force || last_published_ms == 0 ||
            now_ms >= last_published_ms + kL1MetricPublishIntervalMs;
        if (!due_by_request && !due_by_time) {
            l1_metric_publish_in_progress_.store(false, std::memory_order_release);
            return;
        }

        l1_metric_last_publish_ms_.store(now_ms, std::memory_order_relaxed);
        l1_metric_publish_requests_.store(0, std::memory_order_relaxed);

        const uint64_t l1_hits = stats_.l1_hits.load(std::memory_order_relaxed);
        const uint64_t l1_misses = stats_.l1_misses.load(std::memory_order_relaxed);
        const uint64_t total = l1_hits + l1_misses;
        const double l1_hit_ratio =
            total > 0 ? static_cast<double>(l1_hits) / total : 0.0;
        monitor::Metrics::Instance().SetGauge(kL1HitRatioMetric, l1_hit_ratio);
        l1_metric_publish_in_progress_.store(false, std::memory_order_release);
    }

    void RecordUpdateLatency(std::chrono::steady_clock::time_point start_time) {
        const auto end_time = std::chrono::steady_clock::now();
        const double elapsed_ms =
            std::chrono::duration<double, std::milli>(end_time - start_time).count();

        std::lock_guard<std::mutex> lock(update_latency_mutex_);
        update_latency_total_ms_ += elapsed_ms;
        ++update_latency_total_count_;

        update_latency_samples_[update_latency_sample_cursor_] = elapsed_ms;
        update_latency_sample_cursor_ =
            (update_latency_sample_cursor_ + 1) % kUpdateLatencySampleWindow;
        if (update_latency_sample_count_ < kUpdateLatencySampleWindow) {
            ++update_latency_sample_count_;
        }
    }

    std::unique_ptr<IStorageBackend> backend_;
    Config config_;
    std::atomic<bool> shutdown_;
    std::atomic<uint64_t> version_counter_{1};

    std::unique_ptr<l1::MemoryCache> l1_cache_;
    std::unique_ptr<l2::RocksDBCache> l2_cache_;
    std::unique_ptr<utils::CircuitBreaker> circuit_breaker_;
    std::unique_ptr<utils::CircuitBreaker> backend_circuit_breaker_;
    std::unique_ptr<utils::GlobalHybridClock> hlc_;
    std::unique_ptr<persistence::AsyncPersistenceQueue> async_queue_;
    std::atomic<uint64_t> l1_metric_last_publish_ms_{0};
    std::atomic<uint64_t> l1_metric_publish_requests_{0};
    std::atomic<bool> l1_metric_publish_in_progress_{false};
    mutable std::mutex update_latency_mutex_;
    std::array<double, kUpdateLatencySampleWindow> update_latency_samples_{};
    size_t update_latency_sample_cursor_ = 0;
    size_t update_latency_sample_count_ = 0;
    double update_latency_total_ms_ = 0.0;
    uint64_t update_latency_total_count_ = 0;

    struct Statistics {
        std::atomic<uint64_t> l1_hits{0};
        std::atomic<uint64_t> l1_misses{0};
        std::atomic<uint64_t> l2_hits{0};
        std::atomic<uint64_t> l2_misses{0};
        std::atomic<uint64_t> total_updates{0};
        std::atomic<uint64_t> successful_updates{0};
        std::atomic<uint64_t> update_conflicts{0};
        std::atomic<uint64_t> update_retries{0};
        std::atomic<uint64_t> update_aborted{0};
        std::atomic<uint64_t> slow_update_fn_count{0};
        std::atomic<uint64_t> total_sets{0};
    };
    Statistics stats_;
};

// ===== StorageEngine静态成员 =====
std::unique_ptr<StorageEngine> StorageEngine::instance_;
std::mutex StorageEngine::instance_mutex_;

// ===== StorageEngine公开接口实现 =====
StorageEngine::StorageEngine() = default;
StorageEngine::~StorageEngine() = default;

bool StorageEngine::Initialize(std::unique_ptr<IStorageBackend> backend,
                                const Config& config) {
    std::lock_guard<std::mutex> lock(instance_mutex_);

    auto logger = spdlog::get("mir2");
    if (instance_) {
        if (logger) {
            logger->warn("StorageEngine already initialized");
        }
        return false;
    }

    instance_ = std::unique_ptr<StorageEngine>(new StorageEngine());
    instance_->pimpl_ = std::make_unique<Impl>(std::move(backend), config);
    g_instance_ptr.store(instance_.get(), std::memory_order_release);

    if (logger) {
        logger->info("StorageEngine initialized successfully");
    }
    return true;
}

StorageEngine& StorageEngine::Instance() {
    StorageEngine* ptr = g_instance_ptr.load(std::memory_order_acquire);
    if (ptr != nullptr) {
        return *ptr;
    }

    std::lock_guard<std::mutex> lock(instance_mutex_);
    ptr = instance_.get();
    if (!ptr) {
        throw std::runtime_error("StorageEngine not initialized");
    }
    g_instance_ptr.store(ptr, std::memory_order_release);

    return *ptr;
}

bool StorageEngine::IsInitialized() noexcept {
    std::lock_guard<std::mutex> lock(instance_mutex_);
    return instance_ != nullptr;
}

void StorageEngine::Shutdown() {
    std::lock_guard<std::mutex> lock(instance_mutex_);

    if (instance_) {
        g_instance_ptr.store(nullptr, std::memory_order_release);
        instance_->pimpl_->Shutdown();
        instance_.reset();

        auto logger = spdlog::get("mir2");
        if (logger) {
            logger->info("StorageEngine shutdown complete");
        }
    }
}

std::optional<VersionedData> StorageEngine::Get(const std::string& key) {
    if (!IsValidStorageKey(key)) {
        return std::nullopt;
    }
    size_t stripe = pimpl_->GetStripe(key);
    std::shared_lock<std::shared_mutex> lock(pimpl_->locks_[stripe]);
    return pimpl_->GetInternal(key);
}

mir2::logic::Task<std::optional<VersionedData>> StorageEngine::GetAsync(
    const std::string& key,
    mir2::logic::CoroutineExecutor& executor) {
    if (!IsValidStorageKey(key)) {
        co_return std::nullopt;
    }
    size_t stripe = pimpl_->GetStripe(key);
    {
        std::shared_lock<std::shared_mutex> lock(pimpl_->locks_[stripe]);
        auto cached = pimpl_->GetFromL1Internal(key, true);
        if (cached) {
            co_return cached;
        }
    }

    co_return co_await executor.Async([this, key]() -> std::optional<VersionedData> {
        size_t stripe = pimpl_->GetStripe(key);
        std::shared_lock<std::shared_mutex> lock(pimpl_->locks_[stripe]);
        auto cached = pimpl_->GetFromL1Internal(key, false);
        if (cached) {
            return cached;
        }
        return pimpl_->GetFromL2Internal(key);
    });
}

bool StorageEngine::Set(const std::string& key,
                        const std::vector<uint8_t>& data,
                        Priority priority) {
    if (!IsValidStorageKey(key) || data.size() > kMaxStorageValueSize) {
        return false;
    }
    size_t stripe = pimpl_->GetStripe(key);
    std::unique_lock<std::shared_mutex> lock(pimpl_->locks_[stripe]);

    VersionedData versioned{
        pimpl_->NextVersion(),
        data,
        detail::GetCurrentTimeMs()
    };
    return pimpl_->SetInternal(key, versioned, priority);
}

bool StorageEngine::SetSync(const std::string& key,
                            const std::vector<uint8_t>& data,
                            Priority priority) {
    if (!IsValidStorageKey(key) || data.size() > kMaxStorageValueSize) {
        return false;
    }
    size_t stripe = pimpl_->GetStripe(key);
    std::unique_lock<std::shared_mutex> lock(pimpl_->locks_[stripe]);

    VersionedData versioned{
        pimpl_->NextVersion(),
        data,
        detail::GetCurrentTimeMs()
    };
    return pimpl_->SetSyncInternal(key, versioned, priority);
}

bool StorageEngine::Update(const std::string& key,
                           UpdateFunction update_fn,
                           int max_retries,
                           Priority priority) {
    if (!IsValidStorageKey(key)) {
        return false;
    }
    return pimpl_->UpdateImpl(key, std::move(update_fn), max_retries, priority);
}

std::optional<VersionedData> StorageEngine::LoadFromDB(const std::string& key) {
    if (!IsValidStorageKey(key)) {
        return std::nullopt;
    }
    return pimpl_->LoadFromDBInternal(key);
}

mir2::logic::Task<std::optional<VersionedData>> StorageEngine::LoadFromDBAsync(
    const std::string& key,
    mir2::logic::CoroutineExecutor& executor) {
    if (!IsValidStorageKey(key)) {
        co_return std::nullopt;
    }
    co_return co_await executor.Async([this, key]() -> std::optional<VersionedData> {
        return pimpl_->LoadFromDBInternal(key);
    });
}

bool StorageEngine::Flush(uint32_t timeout_ms) {
    if (!pimpl_) {
        return false;
    }
    return pimpl_->FlushInternal(timeout_ms);
}

bool StorageEngine::CreateShutdownSnapshot() {
    // RocksDB is the durable store; just flush the async queue.
    return Flush(10000);
}

bool StorageEngine::PerformStartupRecovery() {
    if (!pimpl_) {
        return false;
    }
    return pimpl_->PerformStartupRecoveryInternal();
}

StorageEngine::HealthMetrics StorageEngine::GetHealthMetrics() const noexcept {
    return pimpl_->GetHealthMetrics();
}

}  // namespace mir2::storage_engine
