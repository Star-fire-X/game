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
#include <deque>
#include <mutex>
#include <shared_mutex>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <utility>
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
constexpr const char* kStrictWriteFallbackMetric =
    "storage.strict_write_fallback_total";
constexpr const char* kStrictWriteFailMetric = "storage.strict_write_fail_total";
constexpr const char* kRecoveryRecoveredMetric = "storage.recovery.recovered_total";
constexpr const char* kRecoveryErrorMetric = "storage.recovery.error_total";
constexpr const char* kAccessAllowMetric = "storage.access.allow_total";
constexpr const char* kAccessDenyMetric = "storage.access.deny_total";
constexpr const char* kAuditLogMetric = "storage.audit.log_total";
constexpr const char* kInvalidationLocalMetric =
    "storage.invalidation.local_total";
constexpr const char* kInvalidationBroadcastMetric =
    "storage.invalidation.broadcast_total";
constexpr const char* kInvalidationBroadcastPublishMetric =
    "storage.invalidation.broadcast_publish_total";
constexpr const char* kInvalidationBroadcastPublishFailMetric =
    "storage.invalidation.broadcast_publish_fail_total";
constexpr const char* kL2CircuitBreakerStateMetric =
    "storage.circuit_breaker.l2_state";
constexpr const char* kBackendCircuitBreakerStateMetric =
    "storage.circuit_breaker.backend_state";
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
    // Account payload is backend-only source-of-truth and bypasses L1/L2 read caches.
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
        const uint32_t block_cache_mb =
            config_.l2_block_cache_mb > 0
                ? config_.l2_block_cache_mb
                : config_.l2_max_size_mb;
        l2_config.block_cache_size =
            static_cast<size_t>(block_cache_mb) * 1024 * 1024;
        l2_config.data_write_buffer_size =
            static_cast<size_t>(std::max(config_.l2_data_write_buffer_mb, 1u)) *
            1024 * 1024;
        l2_config.meta_write_buffer_size =
            static_cast<size_t>(std::max(config_.l2_meta_write_buffer_mb, 1u)) *
            1024 * 1024;
        l2_config.data_max_write_buffer_number =
            static_cast<int>(std::max(config_.l2_data_max_write_buffer_number, 2u));
        l2_config.meta_max_write_buffer_number =
            static_cast<int>(std::max(config_.l2_meta_max_write_buffer_number, 2u));
        l2_config.max_background_jobs =
            static_cast<int>(std::max(config_.l2_max_background_jobs, 1u));
        l2_config.max_background_flushes =
            static_cast<int>(std::max(config_.l2_max_background_flushes, 1u));
        l2_config.block_size = std::max(config_.l2_block_size, 1024u);
        l2_config.bloom_filter_bits_per_key =
            std::max(config_.l2_bloom_filter_bits_per_key, 0.0);
        l2_config.ttl_seconds = static_cast<int32_t>(config_.l2_ttl_seconds);
        l2_config.ttl_periodic_compaction_seconds =
            config_.l2_ttl_periodic_compaction_seconds;
        l2_config.strict_ttl_reads = config_.l2_strict_ttl_reads;
        l2_config.scan_fill_cache = config_.l2_scan_fill_cache;
        l2_config.iter_pin_data = config_.l2_iter_pin_data;
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
            queue_config.queue_capacity = config_.queue_capacity;
            queue_config.batch_size = config_.batch_size;
            queue_config.batch_interval_ms = config_.auto_sync_interval_ms;
            queue_config.worker_threads = std::max<size_t>(2, config_.queue_worker_threads);
            queue_config.retry_count = config_.queue_retry_count;
            queue_config.retry_delay_ms = config_.queue_retry_delay_ms;
            queue_config.dead_letter_max_items = config_.dead_letter_max_items;
            queue_config.enable_durable_outbox = config_.enable_outbox;
            queue_config.outbox_replay_limit = config_.outbox_replay_limit;
            queue_config.outbox_max_items = config_.outbox_max_items;
            queue_config.enable_metrics = config_.enable_metrics;
            async_queue_ = std::make_unique<persistence::AsyncPersistenceQueue>(
                backend_.get(), l2_cache_.get(), queue_config);
        }
        enable_metrics_.store(config_.enable_metrics, std::memory_order_release);
        enable_strict_write_guarantee_.store(
            config_.enable_strict_write_guarantee, std::memory_order_release);
        enable_access_control_.store(
            config_.enable_access_control, std::memory_order_release);
        require_auth_for_reads_.store(
            config_.require_auth_for_reads, std::memory_order_release);
        {
            std::unique_lock<std::shared_mutex> lock(access_control_mutex_);
            access_control_token_ = config_.access_control_token;
        }
        PublishCircuitBreakerStateGauges();

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

        ClearInvalidationAdapterInternal();

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

    std::vector<std::unique_lock<std::shared_mutex>> LockAllStripes() {
        std::vector<std::unique_lock<std::shared_mutex>> locks;
        locks.reserve(kLockStripes);
        for (auto& lock : locks_) {
            locks.emplace_back(lock);
        }
        return locks;
    }

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
            auto l2_result = l2_cache_->Get(key, ResolveDataTier(key));
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

    bool IsPersistentTierCriticalKey(const std::string& key) const {
        if (IsSensitiveStorageKey(key)) {
            return true;
        }

        for (const auto& prefix : config_.critical_key_prefixes) {
            if (!prefix.empty() &&
                std::string_view(key).starts_with(prefix)) {
                return true;
            }
        }
        return false;
    }

    bool IsSyncWriteCriticalKey(const std::string& key) const {
        if (IsSensitiveStorageKey(key)) {
            return true;
        }
        for (const auto& prefix : config_.sync_write_key_prefixes) {
            if (!prefix.empty() &&
                std::string_view(key).starts_with(prefix)) {
                return true;
            }
        }
        // Keep legacy behavior: critical prefixes are sync-write keys unless
        // explicitly carved out elsewhere.
        for (const auto& prefix : config_.critical_key_prefixes) {
            if (!prefix.empty() &&
                std::string_view(key).starts_with(prefix)) {
                return true;
            }
        }
        return false;
    }

    l2::RocksDBCache::DataTier ResolveDataTier(
        const std::string& key) const {
        return (config_.critical_data_no_ttl && IsPersistentTierCriticalKey(key))
            ? l2::RocksDBCache::DataTier::kPersistent
            : l2::RocksDBCache::DataTier::kTtl;
    }

    bool PersistToBackendSync(const std::string& key,
                              const VersionedData& data) {
        auto logger = spdlog::get("mir2");
        if (!backend_) {
            if (backend_circuit_breaker_) {
                backend_circuit_breaker_->OnFailure();
                PublishCircuitBreakerStateGauges();
            }
            if (logger) {
                logger->warn("StorageEngine sync persist failed: backend unavailable, key={}", key);
            }
            return false;
        }
        if (backend_circuit_breaker_ && backend_circuit_breaker_->IsOpen()) {
            PublishCircuitBreakerStateGauges();
            if (logger) {
                logger->warn("StorageEngine sync persist blocked by circuit breaker, key={}", key);
            }
            return false;
        }
        if (!backend_->IsHealthy()) {
            if (backend_circuit_breaker_) {
                backend_circuit_breaker_->OnFailure();
                PublishCircuitBreakerStateGauges();
            }
            if (logger) {
                logger->warn("StorageEngine sync persist failed: backend unhealthy, key={}", key);
            }
            return false;
        }

        auto save_result = backend_->Save(key, data.version, data.data);
        if (!save_result.success) {
            if (backend_circuit_breaker_) {
                backend_circuit_breaker_->OnFailure();
                PublishCircuitBreakerStateGauges();
            }
            if (logger) {
                logger->warn("StorageEngine sync persist failed for key={}, error={}",
                             key, save_result.error_message);
            }
            return false;
        }
        if (backend_circuit_breaker_) {
            backend_circuit_breaker_->OnSuccess();
            PublishCircuitBreakerStateGauges();
        }
        return true;
    }

    // ===== Set实现 =====
    bool SetInternal(const std::string& key,
                     const VersionedData& data,
                     Priority priority,
                     bool enforce_sync_prefix_semantics = true) {
        if (shutdown_.load(std::memory_order_acquire)) {
            return false;
        }

        if (IsSensitiveStorageKey(key)) {
            if (!PersistToBackendSync(key, data)) {
                IncrementStorageCounter(kStrictWriteFailMetric);
                return false;
            }

            if (l1_cache_) {
                l1_cache_->Delete(key);
            }

            stats_.total_sets.fetch_add(1, std::memory_order_relaxed);
            return true;
        }

        bool l2_persisted = false;
        bool backend_persisted = false;
        bool queued_for_persistence = false;
        const auto data_tier = ResolveDataTier(key);

        if (l2_cache_ && (!circuit_breaker_ || !circuit_breaker_->IsOpen())) {
            l2_persisted = l2_cache_->Set(key, data, data_tier);
            if (circuit_breaker_) {
                if (l2_persisted) {
                    circuit_breaker_->OnSuccess();
                } else {
                    circuit_breaker_->OnFailure();
                }
                PublishCircuitBreakerStateGauges();
            }
        }

        if (!l2_persisted &&
            enable_strict_write_guarantee_.load(std::memory_order_acquire)) {
            backend_persisted = PersistToBackendSync(key, data);
            if (!backend_persisted) {
                IncrementStorageCounter(kStrictWriteFailMetric);
                auto logger = spdlog::get("mir2");
                if (logger) {
                    logger->warn(
                        "StorageEngine Set rejected: strict persistence failed, key={}",
                        key);
                }
                return false;
            }
            IncrementStorageCounter(kStrictWriteFallbackMetric);
        }

        if (!backend_persisted && async_queue_) {
            queued_for_persistence = async_queue_->Enqueue(key, data, priority);
            if (!queued_for_persistence) {
                const bool requires_sync_compensation =
                    priority == Priority::CRITICAL ||
                    (enforce_sync_prefix_semantics && IsSyncWriteCriticalKey(key));
                if (requires_sync_compensation) {
                    backend_persisted = PersistToBackendSync(key, data);
                    if (!backend_persisted) {
                        IncrementStorageCounter(kStrictWriteFailMetric);
                        auto logger = spdlog::get("mir2");
                        if (logger) {
                            logger->warn(
                                "StorageEngine Set rejected: outbox enqueue rejected and sync compensation failed, key={}",
                                key);
                        }
                        return false;
                    }
                    IncrementStorageCounter(kStrictWriteFallbackMetric);
                } else {
                    // For degraded mode without durable outbox, L2 can already be
                    // the accepted source of truth for non-critical writes.
                    if (!config_.enable_outbox && l2_persisted) {
                        auto logger = spdlog::get("mir2");
                        if (logger) {
                            logger->warn(
                                "StorageEngine Set accepted with L2 durability despite enqueue rejection, key={}",
                                key);
                        }
                    } else {
                        auto logger = spdlog::get("mir2");
                        if (logger) {
                            logger->warn(
                                "StorageEngine Set rejected: outbox enqueue rejected for non-critical key={}",
                                key);
                        }
                        return false;
                    }
                }
            }
        }

        if (!l2_persisted && !backend_persisted && !queued_for_persistence) {
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
            if (!PersistToBackendSync(key, data)) {
                IncrementStorageCounter(kStrictWriteFailMetric);
                return false;
            }

            if (l1_cache_) {
                l1_cache_->Delete(key);
            }

            stats_.total_sets.fetch_add(1, std::memory_order_relaxed);
            return true;
        }

        bool l2_success = false;
        const auto data_tier = ResolveDataTier(key);
        if (l2_cache_ && (!circuit_breaker_ || !circuit_breaker_->IsOpen())) {
            l2_success = l2_cache_->SetSync(key, data, data_tier);
            if (circuit_breaker_) {
                if (l2_success) {
                    circuit_breaker_->OnSuccess();
                } else {
                    circuit_breaker_->OnFailure();
                }
                PublishCircuitBreakerStateGauges();
            }
        }

        if (!l2_success) {
            if (!PersistToBackendSync(key, data)) {
                IncrementStorageCounter(kStrictWriteFailMetric);
                return false;
            }
            IncrementStorageCounter(kStrictWriteFallbackMetric);
        }

        if (l1_cache_) {
            l1_cache_->Set(key, data);
        }

        if (l2_success && async_queue_) {
            const bool queued = async_queue_->Enqueue(key, data, priority);
            if (!queued) {
                if (!PersistToBackendSync(key, data)) {
                    auto logger = spdlog::get("mir2");
                    if (!config_.enable_outbox && l2_success) {
                        if (logger) {
                            logger->warn(
                                "StorageEngine SetSync accepted with L2 durability in degraded non-outbox mode after enqueue rejection and sync compensation failure, key={}",
                                key);
                        }
                        stats_.total_sets.fetch_add(1, std::memory_order_relaxed);
                        return true;
                    }
                    IncrementStorageCounter(kStrictWriteFailMetric);
                    if (logger) {
                        logger->warn(
                            "StorageEngine SetSync rejected: enqueue rejected and sync compensation failed, key={}",
                            key);
                    }
                    return false;
                }
                IncrementStorageCounter(kStrictWriteFallbackMetric);
            }
        }

        stats_.total_sets.fetch_add(1, std::memory_order_relaxed);
        return true;
    }

    bool DeleteInternal(const std::string& key,
                        uint64_t version,
                        const DeleteOptions& options) {
        if (shutdown_.load(std::memory_order_acquire)) {
            return false;
        }

        const bool hard_delete = options.hard_delete || !options.write_tombstone;
        bool removed_in_cache = false;

        if (l1_cache_ && l1_cache_->Delete(key)) {
            removed_in_cache = true;
        }

        if (l2_cache_ &&
            (!circuit_breaker_ || !circuit_breaker_->IsOpen()) &&
            l2_cache_->Delete(key, ResolveDataTier(key))) {
            removed_in_cache = true;
            if (circuit_breaker_) {
                circuit_breaker_->OnSuccess();
            }
        } else if (l2_cache_ && circuit_breaker_) {
            circuit_breaker_->OnFailure();
        }
        PublishCircuitBreakerStateGauges();

        bool backend_deleted = false;
        if (backend_) {
            if (backend_circuit_breaker_ && backend_circuit_breaker_->IsOpen()) {
                PublishCircuitBreakerStateGauges();
            } else if (!backend_->IsHealthy()) {
                if (backend_circuit_breaker_) {
                    backend_circuit_breaker_->OnFailure();
                    PublishCircuitBreakerStateGauges();
                }
            } else {
                const auto result = backend_->Delete(key, version, hard_delete);
                backend_deleted = result.success;
                if (backend_circuit_breaker_) {
                    if (backend_deleted) {
                        backend_circuit_breaker_->OnSuccess();
                    } else {
                        backend_circuit_breaker_->OnFailure();
                    }
                    PublishCircuitBreakerStateGauges();
                }
            }
        }

        if (backend_ &&
            enable_strict_write_guarantee_.load(std::memory_order_acquire) &&
            !backend_deleted) {
            return false;
        }

        return removed_in_cache || backend_deleted || backend_ == nullptr;
    }

    StorageValidationReport ValidateStorageInternal() {
        StorageValidationReport report;
        report.ok = true;
        report.summary = "ok";
        report.checked_keys = l1_cache_ ? l1_cache_->GetSize() : 0;

        if (!backend_) {
            report.summary = "backend_unavailable";
            return report;
        }

        const auto validate_result = backend_->Validate();
        if (!validate_result.success) {
            report.ok = false;
            report.corrupted_keys = 1;
            report.summary = validate_result.error_message;
        }
        return report;
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

                const bool is_sync_write_key = IsSyncWriteCriticalKey(key);
                const Priority effective_priority =
                    is_sync_write_key ? Priority::CRITICAL : priority;
                const bool write_success = is_sync_write_key
                    ? SetSyncInternal(key, new_versioned_data, effective_priority)
                    : SetInternal(key, new_versioned_data, effective_priority);
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
            PublishCircuitBreakerStateGauges();
            return std::nullopt;
        }

        if (!backend_->IsHealthy()) {
            if (backend_circuit_breaker_) {
                backend_circuit_breaker_->OnFailure();
                PublishCircuitBreakerStateGauges();
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
            PublishCircuitBreakerStateGauges();
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
                l2_cache_->Set(key, loaded, ResolveDataTier(key));
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

    bool InvalidateInternal(const std::string& key) {
        bool invalidated = false;
        if (l1_cache_ && l1_cache_->Delete(key)) {
            invalidated = true;
        }
        if (l2_cache_) {
            invalidated = l2_cache_->Delete(key, ResolveDataTier(key)) || invalidated;
        }
        return invalidated;
    }

    size_t InvalidateByPrefixInternal(const std::string& prefix) {
        if (prefix.empty()) {
            return 0;
        }

        size_t invalidated = 0;
        if (l1_cache_) {
            invalidated += l1_cache_->DeleteByPrefix(prefix);
        }

        if (l2_cache_) {
            invalidated += l2_cache_->DeleteByPrefix(
                prefix, l2::RocksDBCache::DataTier::kTtl);
            invalidated += l2_cache_->DeleteByPrefix(
                prefix, l2::RocksDBCache::DataTier::kPersistent);
        }
        return invalidated;
    }

    bool ApplyRuntimeConfigInternal(const StorageEngine::RuntimeTunableConfig& cfg) {
        persistence::AsyncPersistenceQueue::RuntimeConfig queue_runtime_config;
        bool has_queue_runtime_update = false;
        if (cfg.enable_metrics.has_value()) {
            config_.enable_metrics = *cfg.enable_metrics;
            enable_metrics_.store(*cfg.enable_metrics, std::memory_order_release);
            queue_runtime_config.enable_metrics = *cfg.enable_metrics;
            has_queue_runtime_update = true;
        }
        if (cfg.enable_strict_write_guarantee.has_value()) {
            config_.enable_strict_write_guarantee = *cfg.enable_strict_write_guarantee;
            enable_strict_write_guarantee_.store(
                *cfg.enable_strict_write_guarantee, std::memory_order_release);
        }
        if (cfg.enable_access_control.has_value()) {
            config_.enable_access_control = *cfg.enable_access_control;
            enable_access_control_.store(
                *cfg.enable_access_control, std::memory_order_release);
        }
        if (cfg.require_auth_for_reads.has_value()) {
            config_.require_auth_for_reads = *cfg.require_auth_for_reads;
            require_auth_for_reads_.store(
                *cfg.require_auth_for_reads, std::memory_order_release);
        }
        if (cfg.access_control_token.has_value()) {
            config_.access_control_token = *cfg.access_control_token;
            std::unique_lock<std::shared_mutex> lock(access_control_mutex_);
            access_control_token_ = *cfg.access_control_token;
        }

        if (cfg.l1_ttl_seconds.has_value()) {
            config_.l1_ttl_seconds = *cfg.l1_ttl_seconds;
            l1::MemoryCache::Config l1_config;
            l1_config.capacity = config_.l1_max_entries;
            l1_config.ttl_seconds = config_.l1_ttl_seconds;
            l1_cache_ = std::make_unique<l1::MemoryCache>(l1_config);
        }
        if (cfg.auto_sync_interval_ms.has_value()) {
            config_.auto_sync_interval_ms = *cfg.auto_sync_interval_ms;
            queue_runtime_config.batch_interval_ms = *cfg.auto_sync_interval_ms;
            has_queue_runtime_update = true;
        }
        if (cfg.batch_size.has_value()) {
            config_.batch_size = *cfg.batch_size;
            queue_runtime_config.batch_size = *cfg.batch_size;
            has_queue_runtime_update = true;
        }
        if (cfg.queue_retry_count.has_value()) {
            config_.queue_retry_count = *cfg.queue_retry_count;
            queue_runtime_config.retry_count = *cfg.queue_retry_count;
            has_queue_runtime_update = true;
        }
        if (cfg.queue_retry_delay_ms.has_value()) {
            config_.queue_retry_delay_ms = *cfg.queue_retry_delay_ms;
            queue_runtime_config.retry_delay_ms = *cfg.queue_retry_delay_ms;
            has_queue_runtime_update = true;
        }
        if (cfg.circuit_breaker_threshold.has_value()) {
            config_.circuit_breaker_threshold = *cfg.circuit_breaker_threshold;
        }
        if (cfg.circuit_breaker_timeout_ms.has_value()) {
            config_.circuit_breaker_timeout_ms = *cfg.circuit_breaker_timeout_ms;
        }
        if (cfg.circuit_breaker_threshold.has_value() ||
            cfg.circuit_breaker_timeout_ms.has_value()) {
            utils::CircuitBreaker::Config cb_config;
            cb_config.failure_threshold = config_.circuit_breaker_threshold;
            cb_config.open_timeout_ms = config_.circuit_breaker_timeout_ms;
            circuit_breaker_ = std::make_unique<utils::CircuitBreaker>(cb_config);
            backend_circuit_breaker_ = std::make_unique<utils::CircuitBreaker>(cb_config);
            PublishCircuitBreakerStateGauges();
        }
        if (has_queue_runtime_update && async_queue_) {
            async_queue_->ApplyRuntimeConfig(queue_runtime_config);
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

        l2_cache_->ForEach(
            [&](const std::string& key, const VersionedData& l2_data) -> bool {
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
            },
            l2::RocksDBCache::DataTier::kPersistent);

        if (logger) {
            logger->info("Startup recovery: recovered={}, errors={}", recovered, errors);
        }
        if (recovered > 0) {
            IncrementStorageCounter(kRecoveryRecoveredMetric, recovered);
        }
        if (errors > 0) {
            IncrementStorageCounter(kRecoveryErrorMetric, errors);
        }
        return errors == 0;
    }

    // ===== 健康指标 =====
    StorageEngine::HealthMetrics GetHealthMetrics() const noexcept {
        PublishCircuitBreakerStateGauges();

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
        uint64_t l2_pending_compaction_bytes = 0;
        uint64_t l2_running_compactions = 0;
        uint64_t l2_running_flushes = 0;
        uint64_t l2_block_cache_usage = 0;
        uint64_t l2_immutable_memtables = 0;
        bool l2_write_stopped = false;
        if (l2_cache_) {
            (void)l2_cache_->GetUInt64Property(
                "rocksdb.estimate-pending-compaction-bytes",
                &l2_pending_compaction_bytes);
            (void)l2_cache_->GetUInt64Property("rocksdb.num-running-compactions",
                                               &l2_running_compactions);
            (void)l2_cache_->GetUInt64Property("rocksdb.num-running-flushes",
                                               &l2_running_flushes);
            (void)l2_cache_->GetUInt64Property("rocksdb.block-cache-usage",
                                               &l2_block_cache_usage);
            (void)l2_cache_->GetUInt64Property("rocksdb.num-immutable-mem-table",
                                               &l2_immutable_memtables);
            uint64_t write_stopped = 0;
            if (l2_cache_->GetUInt64Property("rocksdb.is-write-stopped",
                                             &write_stopped)) {
                l2_write_stopped = write_stopped != 0;
            }
        }

        int64_t pending_syncs = async_queue_ ? async_queue_->PendingCount() : 0;
        uint32_t breaker_failures = 0;
        uint8_t l2_circuit_breaker_state = static_cast<uint8_t>(
            utils::CircuitBreaker::State::CLOSED);
        uint8_t backend_circuit_breaker_state = static_cast<uint8_t>(
            utils::CircuitBreaker::State::CLOSED);
        if (circuit_breaker_) {
            breaker_failures += circuit_breaker_->FailureCount();
            l2_circuit_breaker_state =
                static_cast<uint8_t>(circuit_breaker_->GetState());
        }
        if (backend_circuit_breaker_) {
            breaker_failures += backend_circuit_breaker_->FailureCount();
            backend_circuit_breaker_state =
                static_cast<uint8_t>(backend_circuit_breaker_->GetState());
        }
        size_t high_priority_queue_depth = 0;
        size_t normal_priority_queue_depth = 0;
        size_t outbox_depth = 0;
        size_t dead_letter_depth = 0;
        if (async_queue_) {
            const auto queue_stats = async_queue_->GetStats();
            high_priority_queue_depth = queue_stats.high_priority_queue_depth;
            normal_priority_queue_depth = queue_stats.normal_priority_queue_depth;
            outbox_depth = queue_stats.outbox_depth;
            dead_letter_depth = queue_stats.dead_letter_depth;
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
            .l2_circuit_breaker_state = l2_circuit_breaker_state,
            .backend_circuit_breaker_state = backend_circuit_breaker_state,
            .high_priority_queue_depth = high_priority_queue_depth,
            .normal_priority_queue_depth = normal_priority_queue_depth,
            .outbox_depth = outbox_depth,
            .dead_letter_depth = dead_letter_depth,
            .l2_pending_compaction_bytes = l2_pending_compaction_bytes,
            .l2_running_compactions = l2_running_compactions,
            .l2_running_flushes = l2_running_flushes,
            .l2_block_cache_usage = l2_block_cache_usage,
            .l2_immutable_memtables = l2_immutable_memtables,
            .l2_write_stopped = l2_write_stopped,
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

    bool CheckAccessInternal(StorageEngine::AccessOperation op,
                             const std::string& key,
                             const StorageEngine::AccessContext& access,
                             std::string* deny_reason) const {
        if (!IsValidStorageKey(key)) {
            if (deny_reason) {
                *deny_reason = "invalid_key";
            }
            return false;
        }
        if (access.trusted) {
            return true;
        }
        if (!enable_access_control_.load(std::memory_order_acquire)) {
            return true;
        }

        const bool is_read_op =
            op == StorageEngine::AccessOperation::kGet ||
            op == StorageEngine::AccessOperation::kBatchGet ||
            op == StorageEngine::AccessOperation::kLoadFromDB;
        if (is_read_op &&
            !require_auth_for_reads_.load(std::memory_order_acquire)) {
            return true;
        }

        std::string expected_token;
        {
            std::shared_lock<std::shared_mutex> lock(access_control_mutex_);
            expected_token = access_control_token_;
        }
        if (expected_token.empty()) {
            if (deny_reason) {
                *deny_reason = "token_not_configured";
            }
            return false;
        }
        if (access.principal.empty()) {
            if (deny_reason) {
                *deny_reason = "missing_principal";
            }
            return false;
        }
        if (access.access_token != expected_token) {
            if (deny_reason) {
                *deny_reason = "invalid_token";
            }
            return false;
        }
        return true;
    }

    void RecordAudit(const StorageEngine::AuditEntry& entry) {
        if (!config_.enable_audit_log) {
            return;
        }
        {
            std::lock_guard<std::mutex> lock(audit_mutex_);
            if (config_.audit_log_max_entries > 0 &&
                audit_entries_.size() >= config_.audit_log_max_entries) {
                audit_entries_.pop_front();
            }
            audit_entries_.push_back(entry);
        }
        IncrementStorageCounter(kAuditLogMetric);

        auto logger = spdlog::get("mir2");
        if (logger) {
            logger->info(
                "StorageEngine audit op={} key={} principal={} success={} reason={}",
                entry.operation,
                entry.key,
                entry.principal.empty() ? "anonymous" : entry.principal,
                entry.success,
                entry.reason);
        }
    }

    std::vector<StorageEngine::AuditEntry> GetRecentAuditEntriesInternal(
        size_t limit) const {
        std::lock_guard<std::mutex> lock(audit_mutex_);
        if (audit_entries_.empty() || limit == 0) {
            return {};
        }
        const size_t count = std::min(limit, audit_entries_.size());
        std::vector<StorageEngine::AuditEntry> result;
        result.reserve(count);
        auto begin =
            audit_entries_.end() - static_cast<std::ptrdiff_t>(count);
        for (auto it = begin; it != audit_entries_.end(); ++it) {
            result.push_back(*it);
        }
        return result;
    }

    uint64_t SubscribeInvalidationInternal(
        StorageEngine::InvalidationCallback callback) {
        if (!callback) {
            return 0;
        }
        std::lock_guard<std::mutex> lock(invalidation_mutex_);
        const uint64_t id = next_invalidation_subscription_id_++;
        invalidation_subscribers_.emplace(id, std::move(callback));
        return id;
    }

    bool UnsubscribeInvalidationInternal(uint64_t subscription_id) {
        if (subscription_id == 0) {
            return false;
        }
        std::lock_guard<std::mutex> lock(invalidation_mutex_);
        return invalidation_subscribers_.erase(subscription_id) > 0;
    }

    void PublishInvalidationEventInternal(
        const StorageEngine::InvalidationEvent& event) {
        std::vector<StorageEngine::InvalidationCallback> callbacks;
        {
            std::lock_guard<std::mutex> lock(invalidation_mutex_);
            callbacks.reserve(invalidation_subscribers_.size());
            for (const auto& [id, cb] : invalidation_subscribers_) {
                (void)id;
                callbacks.push_back(cb);
            }
        }

        if (event.from_broadcast) {
            IncrementStorageCounter(kInvalidationBroadcastMetric);
        } else {
            IncrementStorageCounter(kInvalidationLocalMetric);
        }

        for (const auto& cb : callbacks) {
            if (!cb) {
                continue;
            }
            try {
                cb(event);
            } catch (const std::exception& e) {
                auto logger = spdlog::get("mir2");
                if (logger) {
                    logger->warn("StorageEngine invalidation callback failed: {}",
                                 e.what());
                }
            } catch (...) {
                auto logger = spdlog::get("mir2");
                if (logger) {
                    logger->warn(
                        "StorageEngine invalidation callback failed: unknown exception");
                }
            }
        }
    }

    bool SetInvalidationAdapterInternal(
        std::shared_ptr<StorageEngine::IInvalidationAdapter> adapter,
        StorageEngine::InvalidationInboundHandler inbound_handler) {
        auto logger = spdlog::get("mir2");
        std::shared_ptr<StorageEngine::IInvalidationAdapter> previous;
        {
            std::lock_guard<std::mutex> lock(invalidation_adapter_mutex_);
            previous = std::move(invalidation_adapter_);
        }
        if (previous) {
            previous->Stop();
        }

        if (!adapter) {
            return true;
        }
        if (!inbound_handler) {
            if (logger) {
                logger->warn(
                    "StorageEngine invalidation adapter rejected: inbound handler missing");
            }
            return false;
        }

        if (!adapter->Start(std::move(inbound_handler))) {
            if (logger) {
                logger->warn("StorageEngine invalidation adapter start failed");
            }
            return false;
        }

        {
            std::lock_guard<std::mutex> lock(invalidation_adapter_mutex_);
            invalidation_adapter_ = std::move(adapter);
        }
        return true;
    }

    void ClearInvalidationAdapterInternal() {
        std::shared_ptr<StorageEngine::IInvalidationAdapter> adapter;
        {
            std::lock_guard<std::mutex> lock(invalidation_adapter_mutex_);
            adapter = std::move(invalidation_adapter_);
        }
        if (adapter) {
            adapter->Stop();
        }
    }

    void PublishInvalidationToAdapterInternal(
        const StorageEngine::InvalidationEvent& event) {
        if (event.from_broadcast) {
            return;
        }
        std::shared_ptr<StorageEngine::IInvalidationAdapter> adapter;
        {
            std::lock_guard<std::mutex> lock(invalidation_adapter_mutex_);
            adapter = invalidation_adapter_;
        }
        if (!adapter) {
            return;
        }

        if (!adapter->Publish(event)) {
            IncrementStorageCounter(kInvalidationBroadcastPublishFailMetric);
            auto logger = spdlog::get("mir2");
            if (logger) {
                logger->warn(
                    "StorageEngine invalidation adapter publish failed, target={}",
                    event.key_or_prefix);
            }
            return;
        }
        IncrementStorageCounter(kInvalidationBroadcastPublishMetric);
    }

    void PublishCircuitBreakerStateGauges() const {
        uint8_t l2_state = static_cast<uint8_t>(
            utils::CircuitBreaker::State::CLOSED);
        uint8_t backend_state = static_cast<uint8_t>(
            utils::CircuitBreaker::State::CLOSED);
        if (circuit_breaker_) {
            l2_state = static_cast<uint8_t>(circuit_breaker_->GetState());
        }
        if (backend_circuit_breaker_) {
            backend_state =
                static_cast<uint8_t>(backend_circuit_breaker_->GetState());
        }
        SetStorageGauge(kL2CircuitBreakerStateMetric,
                        static_cast<double>(l2_state));
        SetStorageGauge(kBackendCircuitBreakerStateMetric,
                        static_cast<double>(backend_state));
    }

    void RecordAccessDecision(bool allowed) {
        IncrementStorageCounter(allowed ? kAccessAllowMetric : kAccessDenyMetric);
    }

    void RecordAuditEntry(const StorageEngine::AuditEntry& entry) {
        RecordAudit(entry);
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
        if (!enable_metrics_.load(std::memory_order_acquire)) {
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
        SetStorageGauge(kL1HitRatioMetric, l1_hit_ratio);
        l1_metric_publish_in_progress_.store(false, std::memory_order_release);
    }

    void IncrementStorageCounter(const std::string& name, uint64_t delta = 1) const {
        if (!enable_metrics_.load(std::memory_order_acquire) || delta == 0) {
            return;
        }
        monitor::Metrics::Instance().IncrementCounter(name, delta);
    }

    void SetStorageGauge(const std::string& name, double value) const {
        if (!enable_metrics_.load(std::memory_order_acquire)) {
            return;
        }
        monitor::Metrics::Instance().SetGauge(name, value);
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
    std::atomic<bool> enable_metrics_{true};
    std::atomic<bool> enable_strict_write_guarantee_{true};
    std::atomic<bool> enable_access_control_{false};
    std::atomic<bool> require_auth_for_reads_{false};
    mutable std::shared_mutex access_control_mutex_;
    std::string access_control_token_;

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
    mutable std::mutex audit_mutex_;
    std::deque<StorageEngine::AuditEntry> audit_entries_;
    mutable std::mutex invalidation_mutex_;
    uint64_t next_invalidation_subscription_id_ = 1;
    std::unordered_map<uint64_t, StorageEngine::InvalidationCallback>
        invalidation_subscribers_;
    mutable std::mutex invalidation_adapter_mutex_;
    std::shared_ptr<StorageEngine::IInvalidationAdapter> invalidation_adapter_;

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

bool StorageEngine::Put(const std::string& key,
                        const std::vector<uint8_t>& data,
                        const WriteOptions& options) {
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

    switch (options.durability) {
    case WriteDurability::kSync:
        return pimpl_->SetSyncInternal(key, versioned, options.priority);
    case WriteDurability::kDurableAsync:
        return pimpl_->SetInternal(
            key, versioned, options.priority, /*enforce_sync_prefix_semantics=*/false);
    case WriteDurability::kBestEffort:
        if (options.bypass_sync_prefix_upgrade) {
            return pimpl_->SetInternal(
                key, versioned, options.priority,
                /*enforce_sync_prefix_semantics=*/false);
        }
        if (pimpl_->IsSyncWriteCriticalKey(key)) {
            return pimpl_->SetSyncInternal(key, versioned, Priority::CRITICAL);
        }
        return pimpl_->SetInternal(
            key, versioned, options.priority,
            /*enforce_sync_prefix_semantics=*/true);
    }
    return false;
}

bool StorageEngine::Delete(const std::string& key,
                           const DeleteOptions& options) {
    if (!IsValidStorageKey(key)) {
        return false;
    }
    size_t stripe = pimpl_->GetStripe(key);
    std::unique_lock<std::shared_mutex> lock(pimpl_->locks_[stripe]);
    const uint64_t delete_version = pimpl_->NextVersion();
    return pimpl_->DeleteInternal(key, delete_version, options);
}

BatchWriteResult StorageEngine::BatchWrite(
    const std::vector<BatchWriteItem>& items) {
    BatchWriteResult result;
    result.total = items.size();

    for (const auto& item : items) {
        bool ok = false;
        switch (item.op) {
        case BatchWriteItem::Op::kPut:
            ok = Put(item.key, item.value, item.write_options);
            if (!ok) {
                result.failed_keys.push_back(item.key);
                result.failure_reasons.push_back("put_failed");
            }
            break;
        case BatchWriteItem::Op::kDelete:
            ok = Delete(item.key, item.delete_options);
            if (!ok) {
                result.failed_keys.push_back(item.key);
                result.failure_reasons.push_back("delete_failed");
            }
            break;
        }

        if (ok) {
            ++result.succeeded;
        } else {
            ++result.failed;
        }
    }

    return result;
}

bool StorageEngine::Set(const std::string& key,
                        const std::vector<uint8_t>& data,
                        Priority priority) {
    WriteOptions options;
    options.durability = WriteDurability::kBestEffort;
    options.priority = priority;
    options.bypass_sync_prefix_upgrade = false;
    return Put(key, data, options);
}

bool StorageEngine::SetAsyncDurable(const std::string& key,
                                    const std::vector<uint8_t>& data,
                                    Priority priority) {
    WriteOptions options;
    options.durability = WriteDurability::kDurableAsync;
    options.priority = priority;
    options.bypass_sync_prefix_upgrade = true;
    return Put(key, data, options);
}

bool StorageEngine::SetSync(const std::string& key,
                            const std::vector<uint8_t>& data,
                            Priority priority) {
    WriteOptions options;
    options.durability = WriteDurability::kSync;
    options.priority = priority;
    options.bypass_sync_prefix_upgrade = false;
    return Put(key, data, options);
}

bool StorageEngine::CompareAndSet(const std::string& key,
                                  uint64_t expected_version,
                                  const std::vector<uint8_t>& data,
                                  Priority priority) {
    if (!IsValidStorageKey(key) || data.size() > kMaxStorageValueSize) {
        return false;
    }

    size_t stripe = pimpl_->GetStripe(key);
    std::unique_lock<std::shared_mutex> lock(pimpl_->locks_[stripe]);
    auto current = pimpl_->GetInternal(key, false);
    const uint64_t current_version = current ? current->version : 0;
    if (current_version != expected_version) {
        return false;
    }

    VersionedData versioned{
        pimpl_->NextVersion(),
        data,
        detail::GetCurrentTimeMs()
    };
    if (pimpl_->IsSyncWriteCriticalKey(key)) {
        return pimpl_->SetSyncInternal(key, versioned, Priority::CRITICAL);
    }
    return pimpl_->SetInternal(key, versioned, priority);
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

std::vector<std::optional<VersionedData>> StorageEngine::BatchGet(
    const std::vector<std::string>& keys) {
    std::vector<std::optional<VersionedData>> result;
    result.reserve(keys.size());
    for (const auto& key : keys) {
        if (!IsValidStorageKey(key)) {
            result.push_back(std::nullopt);
            continue;
        }
        result.push_back(Get(key));
    }
    return result;
}

bool StorageEngine::BatchSet(
    const std::vector<std::pair<std::string, std::vector<uint8_t>>>& kvs,
    Priority priority) {
    std::vector<BatchWriteItem> items;
    items.reserve(kvs.size());
    for (const auto& [key, value] : kvs) {
        BatchWriteItem item;
        item.op = BatchWriteItem::Op::kPut;
        item.key = key;
        item.value = value;
        item.write_options.priority = priority;
        item.write_options.durability = WriteDurability::kBestEffort;
        items.push_back(std::move(item));
    }
    const auto result = BatchWrite(items);
    return result.failed == 0;
}

bool StorageEngine::BatchLoadFromDB(
    const std::vector<std::string>& keys,
    std::vector<std::optional<VersionedData>>* out) {
    if (out == nullptr) {
        return false;
    }
    out->clear();
    out->reserve(keys.size());
    for (const auto& key : keys) {
        if (!IsValidStorageKey(key)) {
            out->push_back(std::nullopt);
            continue;
        }
        out->push_back(LoadFromDB(key));
    }
    return true;
}

std::optional<VersionedData> StorageEngine::GetWithAccess(
    const std::string& key,
    const AccessContext& access) {
    std::string deny_reason;
    const bool allowed =
        pimpl_->CheckAccessInternal(AccessOperation::kGet, key, access,
                                    &deny_reason);
    pimpl_->RecordAccessDecision(allowed);
    pimpl_->RecordAuditEntry(AuditEntry{
        .timestamp_ms = detail::GetCurrentTimeMs(),
        .principal = access.principal,
        .operation = "get",
        .key = key,
        .success = allowed,
        .reason = allowed ? "ok" : deny_reason,
    });
    if (!allowed) {
        return std::nullopt;
    }
    return Get(key);
}

bool StorageEngine::SetWithAccess(const std::string& key,
                                  const std::vector<uint8_t>& data,
                                  const AccessContext& access,
                                  Priority priority) {
    std::string deny_reason;
    const bool allowed =
        pimpl_->CheckAccessInternal(AccessOperation::kSet, key, access,
                                    &deny_reason);
    pimpl_->RecordAccessDecision(allowed);
    if (!allowed) {
        pimpl_->RecordAuditEntry(AuditEntry{
            .timestamp_ms = detail::GetCurrentTimeMs(),
            .principal = access.principal,
            .operation = "set",
            .key = key,
            .success = false,
            .reason = deny_reason,
        });
        return false;
    }
    const bool success = Set(key, data, priority);
    pimpl_->RecordAuditEntry(AuditEntry{
        .timestamp_ms = detail::GetCurrentTimeMs(),
        .principal = access.principal,
        .operation = "set",
        .key = key,
        .success = success,
        .reason = success ? "ok" : "set_failed",
    });
    return success;
}

bool StorageEngine::SetSyncWithAccess(const std::string& key,
                                      const std::vector<uint8_t>& data,
                                      const AccessContext& access,
                                      Priority priority) {
    std::string deny_reason;
    const bool allowed = pimpl_->CheckAccessInternal(
        AccessOperation::kSetSync, key, access, &deny_reason);
    pimpl_->RecordAccessDecision(allowed);
    if (!allowed) {
        pimpl_->RecordAuditEntry(AuditEntry{
            .timestamp_ms = detail::GetCurrentTimeMs(),
            .principal = access.principal,
            .operation = "set_sync",
            .key = key,
            .success = false,
            .reason = deny_reason,
        });
        return false;
    }
    const bool success = SetSync(key, data, priority);
    pimpl_->RecordAuditEntry(AuditEntry{
        .timestamp_ms = detail::GetCurrentTimeMs(),
        .principal = access.principal,
        .operation = "set_sync",
        .key = key,
        .success = success,
        .reason = success ? "ok" : "set_sync_failed",
    });
    return success;
}

std::optional<VersionedData> StorageEngine::LoadFromDBWithAccess(
    const std::string& key,
    const AccessContext& access) {
    std::string deny_reason;
    const bool allowed =
        pimpl_->CheckAccessInternal(AccessOperation::kLoadFromDB, key, access,
                                    &deny_reason);
    pimpl_->RecordAccessDecision(allowed);
    pimpl_->RecordAuditEntry(AuditEntry{
        .timestamp_ms = detail::GetCurrentTimeMs(),
        .principal = access.principal,
        .operation = "load_from_db",
        .key = key,
        .success = allowed,
        .reason = allowed ? "ok" : deny_reason,
    });
    if (!allowed) {
        return std::nullopt;
    }
    return LoadFromDB(key);
}

std::vector<std::optional<VersionedData>> StorageEngine::BatchGetWithAccess(
    const std::vector<std::string>& keys,
    const AccessContext& access) {
    for (const auto& key : keys) {
        std::string deny_reason;
        if (!pimpl_->CheckAccessInternal(AccessOperation::kBatchGet, key, access,
                                         &deny_reason)) {
            pimpl_->RecordAccessDecision(false);
            pimpl_->RecordAuditEntry(AuditEntry{
                .timestamp_ms = detail::GetCurrentTimeMs(),
                .principal = access.principal,
                .operation = "batch_get",
                .key = key,
                .success = false,
                .reason = deny_reason,
            });
            return std::vector<std::optional<VersionedData>>(keys.size(),
                                                             std::nullopt);
        }
    }
    pimpl_->RecordAccessDecision(true);
    auto result = BatchGet(keys);
    pimpl_->RecordAuditEntry(AuditEntry{
        .timestamp_ms = detail::GetCurrentTimeMs(),
        .principal = access.principal,
        .operation = "batch_get",
        .key = "<batch>",
        .success = true,
        .reason = "ok",
    });
    return result;
}

bool StorageEngine::BatchSetWithAccess(
    const std::vector<std::pair<std::string, std::vector<uint8_t>>>& kvs,
    const AccessContext& access,
    Priority priority) {
    for (const auto& [key, value] : kvs) {
        (void)value;
        std::string deny_reason;
        if (!pimpl_->CheckAccessInternal(AccessOperation::kBatchSet, key, access,
                                         &deny_reason)) {
            pimpl_->RecordAccessDecision(false);
            pimpl_->RecordAuditEntry(AuditEntry{
                .timestamp_ms = detail::GetCurrentTimeMs(),
                .principal = access.principal,
                .operation = "batch_set",
                .key = key,
                .success = false,
                .reason = deny_reason,
            });
            return false;
        }
    }
    pimpl_->RecordAccessDecision(true);
    const bool success = BatchSet(kvs, priority);
    pimpl_->RecordAuditEntry(AuditEntry{
        .timestamp_ms = detail::GetCurrentTimeMs(),
        .principal = access.principal,
        .operation = "batch_set",
        .key = "<batch>",
        .success = success,
        .reason = success ? "ok" : "batch_set_failed",
    });
    return success;
}

bool StorageEngine::Invalidate(const std::string& key) {
    if (!IsValidStorageKey(key)) {
        return false;
    }
    bool invalidated = false;
    {
        size_t stripe = pimpl_->GetStripe(key);
        std::unique_lock<std::shared_mutex> lock(pimpl_->locks_[stripe]);
        invalidated = pimpl_->InvalidateInternal(key);
    }
    if (invalidated) {
        const InvalidationEvent event{
            .type = InvalidationEventType::kKey,
            .key_or_prefix = key,
            .timestamp_ms = detail::GetCurrentTimeMs(),
            .from_broadcast = false,
        };
        pimpl_->PublishInvalidationEventInternal(event);
        pimpl_->PublishInvalidationToAdapterInternal(event);
    }
    return invalidated;
}

size_t StorageEngine::InvalidateByPrefix(const std::string& prefix) {
    if (!pimpl_) {
        return 0;
    }
    if (prefix.empty()) {
        return 0;
    }
    size_t invalidated = 0;
    {
        [[maybe_unused]] auto stripe_locks = pimpl_->LockAllStripes();
        invalidated = pimpl_->InvalidateByPrefixInternal(prefix);
    }
    if (invalidated > 0) {
        const InvalidationEvent event{
            .type = InvalidationEventType::kPrefix,
            .key_or_prefix = prefix,
            .timestamp_ms = detail::GetCurrentTimeMs(),
            .from_broadcast = false,
        };
        pimpl_->PublishInvalidationEventInternal(event);
        pimpl_->PublishInvalidationToAdapterInternal(event);
    }
    return invalidated;
}

bool StorageEngine::InvalidateWithAccess(
    const std::string& key,
    const AccessContext& access) {
    std::string deny_reason;
    const bool allowed = pimpl_->CheckAccessInternal(
        AccessOperation::kInvalidate, key, access, &deny_reason);
    pimpl_->RecordAccessDecision(allowed);
    if (!allowed) {
        pimpl_->RecordAuditEntry(AuditEntry{
            .timestamp_ms = detail::GetCurrentTimeMs(),
            .principal = access.principal,
            .operation = "invalidate",
            .key = key,
            .success = false,
            .reason = deny_reason,
        });
        return false;
    }
    const bool success = Invalidate(key);
    pimpl_->RecordAuditEntry(AuditEntry{
        .timestamp_ms = detail::GetCurrentTimeMs(),
        .principal = access.principal,
        .operation = "invalidate",
        .key = key,
        .success = success,
        .reason = success ? "ok" : "invalidate_failed",
    });
    return success;
}

size_t StorageEngine::InvalidateByPrefixWithAccess(
    const std::string& prefix,
    const AccessContext& access) {
    std::string deny_reason;
    const bool allowed = pimpl_->CheckAccessInternal(
        AccessOperation::kInvalidateByPrefix, prefix, access, &deny_reason);
    pimpl_->RecordAccessDecision(allowed);
    if (!allowed) {
        pimpl_->RecordAuditEntry(AuditEntry{
            .timestamp_ms = detail::GetCurrentTimeMs(),
            .principal = access.principal,
            .operation = "invalidate_by_prefix",
            .key = prefix,
            .success = false,
            .reason = deny_reason,
        });
        return 0;
    }
    const size_t count = InvalidateByPrefix(prefix);
    pimpl_->RecordAuditEntry(AuditEntry{
        .timestamp_ms = detail::GetCurrentTimeMs(),
        .principal = access.principal,
        .operation = "invalidate_by_prefix",
        .key = prefix,
        .success = count > 0,
        .reason = count > 0 ? "ok" : "no_match",
    });
    return count;
}

bool StorageEngine::InvalidateFromBroadcast(const std::string& key) {
    if (!IsValidStorageKey(key)) {
        return false;
    }
    bool invalidated = false;
    {
        size_t stripe = pimpl_->GetStripe(key);
        std::unique_lock<std::shared_mutex> lock(pimpl_->locks_[stripe]);
        invalidated = pimpl_->InvalidateInternal(key);
    }
    if (invalidated) {
        pimpl_->PublishInvalidationEventInternal(InvalidationEvent{
            .type = InvalidationEventType::kKey,
            .key_or_prefix = key,
            .timestamp_ms = detail::GetCurrentTimeMs(),
            .from_broadcast = true,
        });
    }
    return invalidated;
}

size_t StorageEngine::InvalidateByPrefixFromBroadcast(const std::string& prefix) {
    if (!pimpl_ || prefix.empty()) {
        return 0;
    }
    size_t invalidated = 0;
    {
        [[maybe_unused]] auto stripe_locks = pimpl_->LockAllStripes();
        invalidated = pimpl_->InvalidateByPrefixInternal(prefix);
    }
    if (invalidated > 0) {
        pimpl_->PublishInvalidationEventInternal(InvalidationEvent{
            .type = InvalidationEventType::kPrefix,
            .key_or_prefix = prefix,
            .timestamp_ms = detail::GetCurrentTimeMs(),
            .from_broadcast = true,
        });
    }
    return invalidated;
}

bool StorageEngine::CheckAccess(AccessOperation op,
                                const std::string& key,
                                const AccessContext& access,
                                std::string* deny_reason) const {
    if (!pimpl_) {
        if (deny_reason) {
            *deny_reason = "storage_not_initialized";
        }
        return false;
    }
    return pimpl_->CheckAccessInternal(op, key, access, deny_reason);
}

std::vector<StorageEngine::AuditEntry> StorageEngine::GetRecentAuditEntries(
    size_t limit) const {
    if (!pimpl_) {
        return {};
    }
    return pimpl_->GetRecentAuditEntriesInternal(limit);
}

uint64_t StorageEngine::SubscribeInvalidation(InvalidationCallback callback) {
    if (!pimpl_) {
        return 0;
    }
    return pimpl_->SubscribeInvalidationInternal(std::move(callback));
}

bool StorageEngine::UnsubscribeInvalidation(uint64_t subscription_id) {
    if (!pimpl_) {
        return false;
    }
    return pimpl_->UnsubscribeInvalidationInternal(subscription_id);
}

bool StorageEngine::SetInvalidationAdapter(
    std::shared_ptr<IInvalidationAdapter> adapter) {
    if (!pimpl_) {
        return false;
    }
    return pimpl_->SetInvalidationAdapterInternal(
        std::move(adapter),
        [this](const InvalidationEvent& event) {
            if (event.type == InvalidationEventType::kPrefix) {
                (void)InvalidateByPrefixFromBroadcast(event.key_or_prefix);
                return;
            }
            (void)InvalidateFromBroadcast(event.key_or_prefix);
        });
}

void StorageEngine::ClearInvalidationAdapter() {
    if (!pimpl_) {
        return;
    }
    pimpl_->ClearInvalidationAdapterInternal();
}

bool StorageEngine::ApplyRuntimeConfig(const RuntimeTunableConfig& cfg) {
    if (!pimpl_) {
        return false;
    }
    [[maybe_unused]] auto stripe_locks = pimpl_->LockAllStripes();
    return pimpl_->ApplyRuntimeConfigInternal(cfg);
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

StorageValidationReport StorageEngine::ValidateStorage() {
    if (!pimpl_) {
        return StorageValidationReport{
            .ok = false,
            .checked_keys = 0,
            .corrupted_keys = 0,
            .tombstone_keys = 0,
            .summary = "storage_not_initialized",
        };
    }
    return pimpl_->ValidateStorageInternal();
}

StorageEngine::HealthMetrics StorageEngine::GetHealthMetrics() const noexcept {
    return pimpl_->GetHealthMetrics();
}

}  // namespace mir2::storage_engine
