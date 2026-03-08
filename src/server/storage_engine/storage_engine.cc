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
#include <condition_variable>
#include <deque>
#include <filesystem>
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
constexpr const char* kL2CapacitySoftLimitWriteMetric =
    "storage.l2_capacity.soft_limit_write_total";
constexpr const char* kL2CapacityHardRejectMetric =
    "storage.l2_capacity.hard_limit_reject_total";
constexpr const char* kL2CapacityHardBypassMetric =
    "storage.l2_capacity.hard_limit_bypass_total";
constexpr const char* kTombstoneGcReclaimedMetric =
    "storage.tombstone_gc.reclaimed_total";
constexpr const char* kTombstoneGcFailedMetric =
    "storage.tombstone_gc.failed_total";
constexpr const char* kRecoveryRecoveredMetric = "storage.recovery.recovered_total";
constexpr const char* kRecoveryErrorMetric = "storage.recovery.error_total";
constexpr const char* kCheckpointCreatedMetric =
    "storage.checkpoint.created_total";
constexpr const char* kCheckpointCreateErrorMetric =
    "storage.checkpoint.create_error_total";
constexpr const char* kCheckpointPrunedMetric =
    "storage.checkpoint.pruned_total";
constexpr const char* kCheckpointPruneErrorMetric =
    "storage.checkpoint.prune_error_total";
constexpr const char* kAccessAllowMetric = "storage.access.allow_total";
constexpr const char* kAccessDenyMetric = "storage.access.deny_total";
constexpr const char* kAuditLogMetric = "storage.audit.log_total";
constexpr const char* kRuntimeConfigAuditMetric =
    "storage.audit.runtime_config.total";
constexpr const char* kRuntimeConfigAuditFailureMetric =
    "storage.audit.runtime_config.failure_total";
constexpr const char* kRuntimeConfigAuditReasonUpdatedMetric =
    "storage.audit.runtime_config.reason.updated_total";
constexpr const char* kRuntimeConfigAuditReasonL2CodecApplyFailedMetric =
    "storage.audit.runtime_config.reason.l2_codec_apply_failed_total";
constexpr const char* kRuntimeConfigAuditKeyEnableAccessControlMetric =
    "storage.audit.runtime_config.key.enable_access_control_total";
constexpr const char* kRuntimeConfigAuditKeyRequireAuthForReadsMetric =
    "storage.audit.runtime_config.key.require_auth_for_reads_total";
constexpr const char* kRuntimeConfigAuditKeyAccessControlTokenMetric =
    "storage.audit.runtime_config.key.access_control_token_total";
constexpr const char* kRuntimeConfigAuditKeyEncryptionActiveKeyIdMetric =
    "storage.audit.runtime_config.key.encryption_active_key_id_total";
constexpr const char* kRuntimeConfigAuditKeyEnableDataEncryptionMetric =
    "storage.audit.runtime_config.key.enable_data_encryption_total";
constexpr const char* kRuntimeConfigAuditKeyEncryptionKeyEnvMetric =
    "storage.audit.runtime_config.key.encryption_key_env_total";
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
constexpr std::string_view kRedactedAuditKey = "<redacted>";
constexpr std::string_view kRuntimeConfigAuditOperation = "runtime_config_update";
constexpr std::string_view kRuntimeConfigAuditUpdatedReason = "updated";
constexpr std::string_view kRuntimeConfigAuditL2CodecApplyFailedReason =
    "l2_codec_apply_failed";
constexpr std::string_view kRuntimeConfigKeyEnableAccessControl =
    "config.enable_access_control";
constexpr std::string_view kRuntimeConfigKeyRequireAuthForReads =
    "config.require_auth_for_reads";
constexpr std::string_view kRuntimeConfigKeyAccessControlToken =
    "config.access_control_token";
constexpr std::string_view kRuntimeConfigKeyEncryptionActiveKeyId =
    "config.encryption_active_key_id";
constexpr std::string_view kRuntimeConfigKeyEnableDataEncryption =
    "config.enable_data_encryption";
constexpr std::string_view kRuntimeConfigKeyEncryptionKeyEnv =
    "config.encryption_key_env";

bool IsValidStorageKey(const std::string& key) {
    if (key.empty() || key.size() > kMaxStorageKeyLength) {
        return false;
    }
    return key.find('\0') == std::string::npos;
}

double NormalizeUsageRatioLimit(double ratio) {
    if (std::isnan(ratio) || std::isinf(ratio)) {
        return 1.0;
    }
    return std::clamp(ratio, 0.0, 1.0);
}

bool IsSensitiveStorageKey(const std::string& key) {
    // Account payload is backend-only source-of-truth and bypasses L1/L2 read caches.
    return std::string_view(key).starts_with(kSensitiveAccountKeyPrefix);
}

const char* WriteRejectReasonToString(WriteRejectReason reason) {
    switch (reason) {
    case WriteRejectReason::kNone:
        return "none";
    case WriteRejectReason::kInvalidKey:
        return "invalid_key";
    case WriteRejectReason::kValueTooLarge:
        return "value_too_large";
    case WriteRejectReason::kPutFailed:
        return "put_failed";
    case WriteRejectReason::kDeleteFailed:
        return "delete_failed";
    case WriteRejectReason::kAccessDenied:
        return "access_denied";
    case WriteRejectReason::kNotInitialized:
        return "not_initialized";
    case WriteRejectReason::kL2HardCapacityLimit:
        return "l2_hard_capacity_limit";
    }
    return "unknown";
}

WriteRejectReason DenyReasonToWriteRejectReason(const std::string& deny_reason) {
    if (deny_reason == "invalid_key") {
        return WriteRejectReason::kInvalidKey;
    }
    return WriteRejectReason::kAccessDenied;
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
        config_.l2_usage_soft_limit_ratio =
            NormalizeUsageRatioLimit(config_.l2_usage_soft_limit_ratio);
        config_.l2_usage_hard_limit_ratio =
            std::max(config_.l2_usage_soft_limit_ratio,
                     NormalizeUsageRatioLimit(config_.l2_usage_hard_limit_ratio));
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
        l2_config.enable_v2_encode = config_.enable_v2_encode;
        l2_config.enable_v2_read_fallback = config_.enable_v2_read_fallback;
        l2_config.enable_data_encryption = config_.enable_data_encryption;
        l2_config.encryption_active_key_id = config_.encryption_active_key_id;
        l2_config.encryption_key_env = config_.encryption_key_env;
        l2_cache_ = std::make_unique<l2::RocksDBCache>(l2_config);
        if (!l2_cache_->Initialize()) {
            if (logger) {
                logger->error("Failed to initialize L2 cache at {}", config_.l2_path);
            }
            l2_cache_.reset();
        } else {
            const auto persisted_runtime_audit_stats =
                l2_cache_->GetPersistedRuntimeConfigAuditStats();
            const auto persisted_capacity_stats =
                l2_cache_->GetPersistedCapacityGovernanceStats();
            const auto persisted_tombstone_gc_stats =
                l2_cache_->GetPersistedTombstoneGcStats();
            stats_.runtime_config_audit_total.store(
                persisted_runtime_audit_stats.runtime_config_audit_total,
                std::memory_order_relaxed);
            stats_.runtime_config_audit_failures.store(
                persisted_runtime_audit_stats.runtime_config_audit_failures,
                std::memory_order_relaxed);
            stats_.runtime_config_audit_reason_updated_total.store(
                persisted_runtime_audit_stats
                    .runtime_config_audit_reason_updated_total,
                std::memory_order_relaxed);
            stats_.runtime_config_audit_reason_l2_codec_apply_failed_total.store(
                persisted_runtime_audit_stats
                    .runtime_config_audit_reason_l2_codec_apply_failed_total,
                std::memory_order_relaxed);
            stats_.runtime_config_audit_key_enable_access_control_total.store(
                persisted_runtime_audit_stats
                    .runtime_config_audit_key_enable_access_control_total,
                std::memory_order_relaxed);
            stats_.runtime_config_audit_key_require_auth_for_reads_total.store(
                persisted_runtime_audit_stats
                    .runtime_config_audit_key_require_auth_for_reads_total,
                std::memory_order_relaxed);
            stats_.runtime_config_audit_key_access_control_token_total.store(
                persisted_runtime_audit_stats
                    .runtime_config_audit_key_access_control_token_total,
                std::memory_order_relaxed);
            stats_.runtime_config_audit_key_encryption_active_key_id_total.store(
                persisted_runtime_audit_stats
                    .runtime_config_audit_key_encryption_active_key_id_total,
                std::memory_order_relaxed);
            stats_.runtime_config_audit_key_enable_data_encryption_total.store(
                persisted_runtime_audit_stats
                    .runtime_config_audit_key_enable_data_encryption_total,
                std::memory_order_relaxed);
            stats_.runtime_config_audit_key_encryption_key_env_total.store(
                persisted_runtime_audit_stats
                    .runtime_config_audit_key_encryption_key_env_total,
                std::memory_order_relaxed);
            stats_.l2_soft_limit_write_total.store(
                persisted_capacity_stats.l2_soft_limit_write_total,
                std::memory_order_relaxed);
            stats_.l2_hard_limit_reject_total.store(
                persisted_capacity_stats.l2_hard_limit_reject_total,
                std::memory_order_relaxed);
            stats_.l2_hard_limit_bypass_total.store(
                persisted_capacity_stats.l2_hard_limit_bypass_total,
                std::memory_order_relaxed);
            stats_.tombstone_gc_reclaimed_total.store(
                persisted_tombstone_gc_stats.tombstone_gc_reclaimed_total,
                std::memory_order_relaxed);
            stats_.tombstone_gc_failed_total.store(
                persisted_tombstone_gc_stats.tombstone_gc_failed_total,
                std::memory_order_relaxed);
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
        enable_new_write_path_.store(
            config_.enable_new_write_path, std::memory_order_release);
        l2_usage_soft_limit_ratio_.store(
            config_.l2_usage_soft_limit_ratio, std::memory_order_release);
        l2_usage_hard_limit_ratio_.store(
            config_.l2_usage_hard_limit_ratio, std::memory_order_release);
        enable_access_control_.store(
            config_.enable_access_control, std::memory_order_release);
        require_auth_for_reads_.store(
            config_.require_auth_for_reads, std::memory_order_release);
        {
            std::unique_lock<std::shared_mutex> lock(access_control_mutex_);
            access_control_token_ = config_.access_control_token;
        }
        checkpoint_enabled_.store(config_.checkpoint_enabled,
                                  std::memory_order_release);
        checkpoint_interval_seconds_.store(
            std::max(config_.checkpoint_interval_seconds, 1u),
            std::memory_order_release);
        checkpoint_retention_.store(config_.checkpoint_retention,
                                    std::memory_order_release);
        tombstone_retention_seconds_.store(
            config_.tombstone_retention_seconds, std::memory_order_release);
        tombstone_gc_interval_seconds_.store(
            config_.tombstone_gc_interval_seconds, std::memory_order_release);
        checkpoint_root_dir_ = config_.checkpoint_dir.empty()
                                   ? (std::filesystem::path(config_.l2_path) /
                                      "checkpoints")
                                   : std::filesystem::path(config_.checkpoint_dir);
        StartCheckpointSchedulerIfNeeded();
        StartTombstoneGcSchedulerIfNeeded();
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

        StopCheckpointScheduler();
        StopTombstoneGcScheduler();

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

    double GetL2UsageSoftLimitRatio() const {
        return l2_usage_soft_limit_ratio_.load(std::memory_order_acquire);
    }

    double GetL2UsageHardLimitRatio() const {
        return l2_usage_hard_limit_ratio_.load(std::memory_order_acquire);
    }

    double GetCurrentL2UsageRatio(size_t l2_size_bytes = 0) const {
        if (config_.l2_max_size_mb == 0) {
            return 0.0;
        }
        const size_t size_bytes = l2_size_bytes > 0
            ? l2_size_bytes
            : (l2_cache_ ? l2_cache_->GetApproximateSizeBytes() : 0);
        if (size_bytes == 0) {
            return 0.0;
        }
        const double max_bytes =
            static_cast<double>(config_.l2_max_size_mb) * 1024.0 * 1024.0;
        if (max_bytes <= 0.0) {
            return 0.0;
        }
        return static_cast<double>(size_bytes) / max_bytes;
    }

    void ApplyL2UsageLimitRatios(double soft_limit_ratio,
                                 double hard_limit_ratio) {
        config_.l2_usage_soft_limit_ratio =
            NormalizeUsageRatioLimit(soft_limit_ratio);
        config_.l2_usage_hard_limit_ratio = std::max(
            config_.l2_usage_soft_limit_ratio,
            NormalizeUsageRatioLimit(hard_limit_ratio));
        l2_usage_soft_limit_ratio_.store(config_.l2_usage_soft_limit_ratio,
                                         std::memory_order_release);
        l2_usage_hard_limit_ratio_.store(config_.l2_usage_hard_limit_ratio,
                                         std::memory_order_release);
    }

    bool EvaluateL2CapacityPressure(bool* soft_limit_active,
                                    bool* hard_limit_active,
                                    double* usage_ratio = nullptr,
                                    size_t l2_size_bytes = 0) const {
        const double ratio = GetCurrentL2UsageRatio(l2_size_bytes);
        if (usage_ratio != nullptr) {
            *usage_ratio = ratio;
        }
        const bool soft_active = ratio >= GetL2UsageSoftLimitRatio();
        const bool hard_active = ratio >= GetL2UsageHardLimitRatio();
        if (soft_limit_active != nullptr) {
            *soft_limit_active = soft_active;
        }
        if (hard_limit_active != nullptr) {
            *hard_limit_active = hard_active;
        }
        return hard_active;
    }

    bool CheckAndRecordL2CapacityAdmission(const std::string& key,
                                           Priority priority,
                                           bool enforce_sync_prefix_semantics,
                                           WriteRejectReason* reject_reason = nullptr) {
        bool soft_limit_active = false;
        bool hard_limit_active = false;
        EvaluateL2CapacityPressure(
            &soft_limit_active, &hard_limit_active, nullptr);
        if (soft_limit_active) {
            stats_.l2_soft_limit_write_total.fetch_add(
                1, std::memory_order_relaxed);
            IncrementStorageCounter(kL2CapacitySoftLimitWriteMetric);
            PersistCapacityGovernanceStatsToL2();
        }
        const bool bypass_hard_limit =
            priority == Priority::CRITICAL ||
            (enforce_sync_prefix_semantics && IsSyncWriteCriticalKey(key));
        if (hard_limit_active && bypass_hard_limit) {
            stats_.l2_hard_limit_bypass_total.fetch_add(
                1, std::memory_order_relaxed);
            IncrementStorageCounter(kL2CapacityHardBypassMetric);
            PersistCapacityGovernanceStatsToL2();
        }
        if (hard_limit_active && !bypass_hard_limit) {
            stats_.l2_hard_limit_reject_total.fetch_add(
                1, std::memory_order_relaxed);
            IncrementStorageCounter(kL2CapacityHardRejectMetric);
            PersistCapacityGovernanceStatsToL2();
            if (reject_reason != nullptr) {
                *reject_reason = WriteRejectReason::kL2HardCapacityLimit;
            } else {
                RecordWriteRejectReason(WriteRejectReason::kL2HardCapacityLimit);
            }
            return false;
        }
        return true;
    }

    bool PutWithReasonLocked(const std::string& key,
                             const VersionedData& data,
                             const WriteOptions& options,
                             WriteRejectReason* reject_reason) {
        switch (options.durability) {
        case WriteDurability::kSync:
            return SetSyncInternal(key, data, options.priority);
        case WriteDurability::kDurableAsync:
            return SetInternal(
                key,
                data,
                options.priority,
                /*enforce_sync_prefix_semantics=*/false,
                reject_reason);
        case WriteDurability::kBestEffort:
            if (options.bypass_sync_prefix_upgrade) {
                return SetInternal(
                    key,
                    data,
                    options.priority,
                    /*enforce_sync_prefix_semantics=*/false,
                    reject_reason);
            }
            if (IsSyncWriteCriticalKey(key)) {
                return SetSyncInternal(key, data, Priority::CRITICAL);
            }
            return SetInternal(
                key,
                data,
                options.priority,
                /*enforce_sync_prefix_semantics=*/true,
                reject_reason);
        }
        return false;
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
                     bool enforce_sync_prefix_semantics = true,
                     WriteRejectReason* reject_reason = nullptr) {
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

        if (!CheckAndRecordL2CapacityAdmission(
                key, priority, enforce_sync_prefix_semantics, reject_reason)) {
            auto logger = spdlog::get("mir2");
            if (logger) {
                logger->warn(
                    "StorageEngine Set rejected: L2 hard capacity limit reached, key={}",
                    key);
            }
            return false;
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

        bool soft_limit_active = false;
        bool hard_limit_active = false;
        EvaluateL2CapacityPressure(&soft_limit_active, &hard_limit_active, nullptr);
        if (soft_limit_active) {
            stats_.l2_soft_limit_write_total.fetch_add(
                1, std::memory_order_relaxed);
            IncrementStorageCounter(kL2CapacitySoftLimitWriteMetric);
            PersistCapacityGovernanceStatsToL2();
        }
        if (hard_limit_active) {
            stats_.l2_hard_limit_bypass_total.fetch_add(
                1, std::memory_order_relaxed);
            IncrementStorageCounter(kL2CapacityHardBypassMetric);
            PersistCapacityGovernanceStatsToL2();
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

        const bool tombstone_retention_enabled =
            tombstone_retention_seconds_.load(std::memory_order_acquire) > 0;
        const bool hard_delete =
            options.hard_delete || !options.write_tombstone ||
            !tombstone_retention_enabled;
        if (async_queue_) {
            async_queue_->CancelPendingForKey(key, version);
        }

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

        if (backend_deleted && !hard_delete && l2_cache_) {
            const uint64_t retention_seconds =
                tombstone_retention_seconds_.load(std::memory_order_acquire);
            const uint64_t due_at_ms =
                detail::GetCurrentTimeMs() + retention_seconds * 1000ULL;
            if (!l2_cache_->AppendTombstoneGcEntry(key, version, due_at_ms)) {
                auto logger = spdlog::get("mir2");
                if (logger) {
                    logger->warn(
                        "StorageEngine failed to append tombstone GC entry key={} version={}",
                        key, version);
                }
            } else {
                NotifyTombstoneGcScheduler();
            }
        }

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

        return removed_in_cache || backend_deleted || backend_ == nullptr;
    }

    StorageValidationReport ValidateStorageInternal() {
        StorageValidationReport report;
        report.ok = true;
        report.summary = "ok";
        report.checked_keys = l1_cache_ ? l1_cache_->GetSize() : 0;

        auto append_summary = [&report](const std::string& msg) {
            if (msg.empty()) {
                return;
            }
            if (report.summary.empty() || report.summary == "ok") {
                report.summary = msg;
                return;
            }
            report.summary += "; " + msg;
        };

        if (l2_cache_) {
            const size_t ttl_corrupted = l2_cache_->CountCorruptedEntries(
                l2::RocksDBCache::DataTier::kTtl);
            const size_t persistent_corrupted = l2_cache_->CountCorruptedEntries(
                l2::RocksDBCache::DataTier::kPersistent);
            const size_t total_l2_corrupted =
                ttl_corrupted + persistent_corrupted;
            report.corrupted_keys += total_l2_corrupted;
            if (total_l2_corrupted > 0) {
                report.ok = false;
                append_summary(
                    "l2_corrupted_keys=" +
                    std::to_string(total_l2_corrupted));
            }
        }

        if (!backend_) {
            if (report.summary == "ok") {
                report.summary = "backend_unavailable";
            }
            return report;
        }

        const auto validate_result = backend_->Validate();
        if (!validate_result.success) {
            report.ok = false;
            report.corrupted_keys += 1;
            append_summary(validate_result.error_message);
        } else if (report.ok) {
            report.summary = "ok";
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
        bool has_l2_codec_runtime_update = false;
        bool has_checkpoint_runtime_update = false;
        bool has_tombstone_gc_runtime_update = false;
        const bool has_runtime_v2_encode_update = cfg.enable_v2_encode.has_value();
        const bool has_runtime_v2_fallback_update =
            cfg.enable_v2_read_fallback.has_value();
        const bool has_runtime_data_encryption_update =
            cfg.enable_data_encryption.has_value();
        const bool has_runtime_active_key_update =
            cfg.encryption_active_key_id.has_value();
        const bool has_runtime_key_env_update = cfg.encryption_key_env.has_value();
        const bool prev_enable_v2_encode = config_.enable_v2_encode;
        const bool prev_enable_v2_read_fallback = config_.enable_v2_read_fallback;
        const bool prev_enable_data_encryption = config_.enable_data_encryption;
        const std::string prev_encryption_active_key_id =
            config_.encryption_active_key_id;
        const std::string prev_encryption_key_env =
            config_.encryption_key_env;
        auto emit_runtime_config_audit =
            [this](std::string_view key, bool success, std::string_view reason) {
                RecordAudit(StorageEngine::AuditEntry{
                    .timestamp_ms = detail::GetCurrentTimeMs(),
                    .principal = "system",
                    .operation = std::string(kRuntimeConfigAuditOperation),
                    .key = std::string(key),
                    .success = success,
                    .reason = std::string(reason),
                });
                stats_.runtime_config_audit_total.fetch_add(
                    1, std::memory_order_relaxed);
                IncrementStorageCounter(kRuntimeConfigAuditMetric);
                if (!success) {
                    stats_.runtime_config_audit_failures.fetch_add(
                        1, std::memory_order_relaxed);
                    IncrementStorageCounter(kRuntimeConfigAuditFailureMetric);
                }
                if (reason == kRuntimeConfigAuditUpdatedReason) {
                    stats_.runtime_config_audit_reason_updated_total.fetch_add(
                        1, std::memory_order_relaxed);
                    IncrementStorageCounter(kRuntimeConfigAuditReasonUpdatedMetric);
                } else if (reason ==
                           kRuntimeConfigAuditL2CodecApplyFailedReason) {
                    stats_.runtime_config_audit_reason_l2_codec_apply_failed_total
                        .fetch_add(1, std::memory_order_relaxed);
                    IncrementStorageCounter(
                        kRuntimeConfigAuditReasonL2CodecApplyFailedMetric);
                }
                if (key == kRuntimeConfigKeyEnableAccessControl) {
                    stats_.runtime_config_audit_key_enable_access_control_total
                        .fetch_add(1, std::memory_order_relaxed);
                    IncrementStorageCounter(
                        kRuntimeConfigAuditKeyEnableAccessControlMetric);
                } else if (key == kRuntimeConfigKeyRequireAuthForReads) {
                    stats_.runtime_config_audit_key_require_auth_for_reads_total
                        .fetch_add(1, std::memory_order_relaxed);
                    IncrementStorageCounter(
                        kRuntimeConfigAuditKeyRequireAuthForReadsMetric);
                } else if (key == kRuntimeConfigKeyAccessControlToken) {
                    stats_.runtime_config_audit_key_access_control_token_total
                        .fetch_add(1, std::memory_order_relaxed);
                    IncrementStorageCounter(
                        kRuntimeConfigAuditKeyAccessControlTokenMetric);
                } else if (key == kRuntimeConfigKeyEncryptionActiveKeyId) {
                    stats_.runtime_config_audit_key_encryption_active_key_id_total
                        .fetch_add(1, std::memory_order_relaxed);
                    IncrementStorageCounter(
                        kRuntimeConfigAuditKeyEncryptionActiveKeyIdMetric);
                } else if (key == kRuntimeConfigKeyEnableDataEncryption) {
                    stats_.runtime_config_audit_key_enable_data_encryption_total
                        .fetch_add(1, std::memory_order_relaxed);
                    IncrementStorageCounter(
                        kRuntimeConfigAuditKeyEnableDataEncryptionMetric);
                } else if (key == kRuntimeConfigKeyEncryptionKeyEnv) {
                    stats_.runtime_config_audit_key_encryption_key_env_total
                        .fetch_add(1, std::memory_order_relaxed);
                    IncrementStorageCounter(
                        kRuntimeConfigAuditKeyEncryptionKeyEnvMetric);
                }
                if (l2_cache_) {
                    const l2::RocksDBCache::RuntimeConfigAuditStats persisted_stats{
                        .runtime_config_audit_total =
                            stats_.runtime_config_audit_total.load(
                                std::memory_order_relaxed),
                        .runtime_config_audit_failures =
                            stats_.runtime_config_audit_failures.load(
                                std::memory_order_relaxed),
                        .runtime_config_audit_reason_updated_total =
                            stats_.runtime_config_audit_reason_updated_total.load(
                                std::memory_order_relaxed),
                        .runtime_config_audit_reason_l2_codec_apply_failed_total =
                            stats_.runtime_config_audit_reason_l2_codec_apply_failed_total
                                .load(std::memory_order_relaxed),
                        .runtime_config_audit_key_enable_access_control_total =
                            stats_.runtime_config_audit_key_enable_access_control_total
                                .load(std::memory_order_relaxed),
                        .runtime_config_audit_key_require_auth_for_reads_total =
                            stats_.runtime_config_audit_key_require_auth_for_reads_total
                                .load(std::memory_order_relaxed),
                        .runtime_config_audit_key_access_control_token_total =
                            stats_.runtime_config_audit_key_access_control_token_total
                                .load(std::memory_order_relaxed),
                        .runtime_config_audit_key_encryption_active_key_id_total =
                            stats_.runtime_config_audit_key_encryption_active_key_id_total
                                .load(std::memory_order_relaxed),
                        .runtime_config_audit_key_enable_data_encryption_total =
                            stats_.runtime_config_audit_key_enable_data_encryption_total
                                .load(std::memory_order_relaxed),
                        .runtime_config_audit_key_encryption_key_env_total =
                            stats_.runtime_config_audit_key_encryption_key_env_total
                                .load(std::memory_order_relaxed),
                    };
                    if (!l2_cache_->PersistRuntimeConfigAuditStats(
                            persisted_stats)) {
                        auto logger = spdlog::get("mir2");
                        if (logger) {
                            logger->warn(
                                "StorageEngine failed to persist runtime audit stats to L2 meta");
                        }
                    }
                }
            };
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
        if (cfg.enable_new_write_path.has_value()) {
            config_.enable_new_write_path = *cfg.enable_new_write_path;
            enable_new_write_path_.store(
                *cfg.enable_new_write_path, std::memory_order_release);
        }
        if (cfg.enable_v2_encode.has_value()) {
            config_.enable_v2_encode = *cfg.enable_v2_encode;
            has_l2_codec_runtime_update = true;
        }
        if (cfg.enable_v2_read_fallback.has_value()) {
            config_.enable_v2_read_fallback = *cfg.enable_v2_read_fallback;
            has_l2_codec_runtime_update = true;
        }
        if (cfg.enable_data_encryption.has_value()) {
            config_.enable_data_encryption = *cfg.enable_data_encryption;
            has_l2_codec_runtime_update = true;
        }
        if (cfg.encryption_active_key_id.has_value()) {
            config_.encryption_active_key_id = *cfg.encryption_active_key_id;
            has_l2_codec_runtime_update = true;
        }
        if (cfg.encryption_key_env.has_value()) {
            config_.encryption_key_env = *cfg.encryption_key_env;
            has_l2_codec_runtime_update = true;
        }
        if (cfg.enable_access_control.has_value()) {
            config_.enable_access_control = *cfg.enable_access_control;
            enable_access_control_.store(
                *cfg.enable_access_control, std::memory_order_release);
            emit_runtime_config_audit(kRuntimeConfigKeyEnableAccessControl,
                                      true,
                                      kRuntimeConfigAuditUpdatedReason);
        }
        if (cfg.require_auth_for_reads.has_value()) {
            config_.require_auth_for_reads = *cfg.require_auth_for_reads;
            require_auth_for_reads_.store(
                *cfg.require_auth_for_reads, std::memory_order_release);
            emit_runtime_config_audit(kRuntimeConfigKeyRequireAuthForReads,
                                      true,
                                      kRuntimeConfigAuditUpdatedReason);
        }
        if (cfg.access_control_token.has_value()) {
            config_.access_control_token = *cfg.access_control_token;
            std::unique_lock<std::shared_mutex> lock(access_control_mutex_);
            access_control_token_ = *cfg.access_control_token;
            emit_runtime_config_audit(kRuntimeConfigKeyAccessControlToken,
                                      true,
                                      kRuntimeConfigAuditUpdatedReason);
        }

        if (cfg.l1_ttl_seconds.has_value()) {
            config_.l1_ttl_seconds = *cfg.l1_ttl_seconds;
            l1::MemoryCache::Config l1_config;
            l1_config.capacity = config_.l1_max_entries;
            l1_config.ttl_seconds = config_.l1_ttl_seconds;
            l1_cache_ = std::make_unique<l1::MemoryCache>(l1_config);
        }
        if (cfg.l2_usage_soft_limit_ratio.has_value() ||
            cfg.l2_usage_hard_limit_ratio.has_value()) {
            ApplyL2UsageLimitRatios(
                cfg.l2_usage_soft_limit_ratio.value_or(
                    config_.l2_usage_soft_limit_ratio),
                cfg.l2_usage_hard_limit_ratio.value_or(
                    config_.l2_usage_hard_limit_ratio));
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
        if (cfg.tombstone_retention_seconds.has_value()) {
            config_.tombstone_retention_seconds =
                *cfg.tombstone_retention_seconds;
            tombstone_retention_seconds_.store(
                config_.tombstone_retention_seconds,
                std::memory_order_release);
            has_tombstone_gc_runtime_update = true;
        }
        if (cfg.tombstone_gc_interval_seconds.has_value()) {
            config_.tombstone_gc_interval_seconds =
                *cfg.tombstone_gc_interval_seconds;
            tombstone_gc_interval_seconds_.store(
                config_.tombstone_gc_interval_seconds,
                std::memory_order_release);
            has_tombstone_gc_runtime_update = true;
        }
        if (cfg.checkpoint_enabled.has_value()) {
            config_.checkpoint_enabled = *cfg.checkpoint_enabled;
            checkpoint_enabled_.store(*cfg.checkpoint_enabled,
                                      std::memory_order_release);
            has_checkpoint_runtime_update = true;
        }
        if (cfg.checkpoint_interval_seconds.has_value()) {
            config_.checkpoint_interval_seconds =
                std::max(*cfg.checkpoint_interval_seconds, 1u);
            checkpoint_interval_seconds_.store(
                config_.checkpoint_interval_seconds,
                std::memory_order_release);
            has_checkpoint_runtime_update = true;
        }
        if (cfg.checkpoint_retention.has_value()) {
            config_.checkpoint_retention = *cfg.checkpoint_retention;
            checkpoint_retention_.store(*cfg.checkpoint_retention,
                                        std::memory_order_release);
            has_checkpoint_runtime_update = true;
        }
        if (has_l2_codec_runtime_update) {
            if (l2_cache_) {
                if (!l2_cache_->ApplyRuntimeCodecConfig(
                    config_.enable_v2_encode,
                    config_.enable_v2_read_fallback,
                    cfg.enable_data_encryption,
                    cfg.encryption_active_key_id,
                    cfg.encryption_key_env)) {
                    if (has_runtime_v2_encode_update) {
                        emit_runtime_config_audit(
                            "config.enable_v2_encode",
                            false,
                            kRuntimeConfigAuditL2CodecApplyFailedReason);
                    }
                    if (has_runtime_v2_fallback_update) {
                        emit_runtime_config_audit(
                            "config.enable_v2_read_fallback",
                            false,
                            kRuntimeConfigAuditL2CodecApplyFailedReason);
                    }
                    if (has_runtime_data_encryption_update) {
                        emit_runtime_config_audit(
                            kRuntimeConfigKeyEnableDataEncryption,
                            false,
                            kRuntimeConfigAuditL2CodecApplyFailedReason);
                    }
                    if (has_runtime_active_key_update) {
                        emit_runtime_config_audit(
                            kRuntimeConfigKeyEncryptionActiveKeyId,
                            false,
                            kRuntimeConfigAuditL2CodecApplyFailedReason);
                    }
                    if (has_runtime_key_env_update) {
                        emit_runtime_config_audit(
                            kRuntimeConfigKeyEncryptionKeyEnv,
                            false,
                            kRuntimeConfigAuditL2CodecApplyFailedReason);
                    }
                    config_.enable_v2_encode = prev_enable_v2_encode;
                    config_.enable_v2_read_fallback =
                        prev_enable_v2_read_fallback;
                    config_.enable_data_encryption =
                        prev_enable_data_encryption;
                    config_.encryption_active_key_id =
                        prev_encryption_active_key_id;
                    config_.encryption_key_env = prev_encryption_key_env;
                    return false;
                }
            }
            if (l1_cache_) {
                // Drop stale decoded entries so codec policy changes take effect
                // immediately on subsequent reads.
                l1::MemoryCache::Config l1_config;
                l1_config.capacity = config_.l1_max_entries;
                l1_config.ttl_seconds = config_.l1_ttl_seconds;
                l1_cache_ = std::make_unique<l1::MemoryCache>(l1_config);
            }
            if (has_runtime_v2_encode_update) {
                emit_runtime_config_audit(
                    "config.enable_v2_encode",
                    true,
                    kRuntimeConfigAuditUpdatedReason);
            }
            if (has_runtime_v2_fallback_update) {
                emit_runtime_config_audit(
                    "config.enable_v2_read_fallback",
                    true,
                    kRuntimeConfigAuditUpdatedReason);
            }
            if (has_runtime_data_encryption_update) {
                emit_runtime_config_audit(
                    kRuntimeConfigKeyEnableDataEncryption,
                    true,
                    kRuntimeConfigAuditUpdatedReason);
            }
            if (has_runtime_active_key_update) {
                emit_runtime_config_audit(
                    kRuntimeConfigKeyEncryptionActiveKeyId,
                    true,
                    kRuntimeConfigAuditUpdatedReason);
            }
            if (has_runtime_key_env_update) {
                emit_runtime_config_audit(
                    kRuntimeConfigKeyEncryptionKeyEnv,
                    true,
                    kRuntimeConfigAuditUpdatedReason);
            }
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
        if (has_checkpoint_runtime_update) {
            if (checkpoint_enabled_.load(std::memory_order_acquire) &&
                l2_cache_ != nullptr) {
                StartCheckpointSchedulerIfNeeded();
                NotifyCheckpointScheduler();
            } else {
                StopCheckpointScheduler();
            }
        }
        if (has_tombstone_gc_runtime_update) {
            if (TombstoneGcSchedulerEnabled()) {
                StartTombstoneGcSchedulerIfNeeded();
                NotifyTombstoneGcScheduler();
            } else {
                StopTombstoneGcScheduler();
            }
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
        double l2_usage_ratio = 0.0;
        const double l2_usage_soft_limit_ratio = GetL2UsageSoftLimitRatio();
        const double l2_usage_hard_limit_ratio = GetL2UsageHardLimitRatio();
        bool l2_soft_limit_active = false;
        bool l2_hard_limit_active = false;
        bool l2_write_stopped = false;
        bool enable_v2_encode = config_.enable_v2_encode;
        bool enable_v2_read_fallback = config_.enable_v2_read_fallback;
        bool enable_data_encryption = config_.enable_data_encryption;
        uint64_t l2_v2_decode_reads = 0;
        uint64_t l2_v1_fallback_reads = 0;
        uint64_t l2_v1_reject_reads = 0;
        uint64_t l2_decode_errors = 0;
        uint64_t l2_encrypted_decode_reads = 0;
        uint64_t l2_decrypt_failures = 0;
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
            EvaluateL2CapacityPressure(&l2_soft_limit_active,
                                       &l2_hard_limit_active,
                                       &l2_usage_ratio,
                                       l2_size);
            const auto codec_stats = l2_cache_->GetCodecRuntimeStats();
            enable_v2_encode = codec_stats.enable_v2_encode;
            enable_v2_read_fallback = codec_stats.enable_v2_read_fallback;
            enable_data_encryption = codec_stats.enable_data_encryption;
            l2_v2_decode_reads = codec_stats.v2_decode_reads;
            l2_v1_fallback_reads = codec_stats.v1_fallback_reads;
            l2_v1_reject_reads = codec_stats.v1_reject_reads;
            l2_decode_errors = codec_stats.decode_errors;
            l2_encrypted_decode_reads = codec_stats.encrypted_decode_reads;
            l2_decrypt_failures = codec_stats.decrypt_failures;
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
        size_t tombstone_gc_pending = 0;
        if (async_queue_) {
            const auto queue_stats = async_queue_->GetStats();
            high_priority_queue_depth = queue_stats.high_priority_queue_depth;
            normal_priority_queue_depth = queue_stats.normal_priority_queue_depth;
            outbox_depth = queue_stats.outbox_depth;
            dead_letter_depth = queue_stats.dead_letter_depth;
        }
        if (l2_cache_) {
            tombstone_gc_pending = l2_cache_->TombstoneGcDepth();
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
            .tombstone_gc_pending = tombstone_gc_pending,
            .l2_pending_compaction_bytes = l2_pending_compaction_bytes,
            .l2_running_compactions = l2_running_compactions,
            .l2_running_flushes = l2_running_flushes,
            .l2_block_cache_usage = l2_block_cache_usage,
            .l2_immutable_memtables = l2_immutable_memtables,
            .l2_usage_ratio = l2_usage_ratio,
            .l2_usage_soft_limit_ratio = l2_usage_soft_limit_ratio,
            .l2_usage_hard_limit_ratio = l2_usage_hard_limit_ratio,
            .l2_soft_limit_active = l2_soft_limit_active,
            .l2_hard_limit_active = l2_hard_limit_active,
            .l2_soft_limit_write_total =
                stats_.l2_soft_limit_write_total.load(std::memory_order_relaxed),
            .l2_hard_limit_reject_total =
                stats_.l2_hard_limit_reject_total.load(std::memory_order_relaxed),
            .l2_hard_limit_bypass_total =
                stats_.l2_hard_limit_bypass_total.load(std::memory_order_relaxed),
            .l2_write_stopped = l2_write_stopped,
            .enable_v2_encode = enable_v2_encode,
            .enable_v2_read_fallback = enable_v2_read_fallback,
            .enable_data_encryption = enable_data_encryption,
            .l2_v2_decode_reads = l2_v2_decode_reads,
            .l2_v1_fallback_reads = l2_v1_fallback_reads,
            .l2_v1_reject_reads = l2_v1_reject_reads,
            .l2_decode_errors = l2_decode_errors,
            .l2_encrypted_decode_reads = l2_encrypted_decode_reads,
            .l2_decrypt_failures = l2_decrypt_failures,
            .tombstone_gc_reclaimed_total =
                stats_.tombstone_gc_reclaimed_total.load(
                    std::memory_order_relaxed),
            .tombstone_gc_failed_total =
                stats_.tombstone_gc_failed_total.load(
                    std::memory_order_relaxed),
            .runtime_config_audit_total =
                stats_.runtime_config_audit_total.load(std::memory_order_relaxed),
            .runtime_config_audit_failures =
                stats_.runtime_config_audit_failures.load(std::memory_order_relaxed),
            .runtime_config_audit_reason_updated_total =
                stats_.runtime_config_audit_reason_updated_total.load(
                    std::memory_order_relaxed),
            .runtime_config_audit_reason_l2_codec_apply_failed_total =
                stats_.runtime_config_audit_reason_l2_codec_apply_failed_total.load(
                    std::memory_order_relaxed),
            .runtime_config_audit_key_enable_access_control_total =
                stats_.runtime_config_audit_key_enable_access_control_total.load(
                    std::memory_order_relaxed),
            .runtime_config_audit_key_require_auth_for_reads_total =
                stats_.runtime_config_audit_key_require_auth_for_reads_total.load(
                    std::memory_order_relaxed),
            .runtime_config_audit_key_access_control_token_total =
                stats_.runtime_config_audit_key_access_control_token_total.load(
                    std::memory_order_relaxed),
            .runtime_config_audit_key_encryption_active_key_id_total =
                stats_.runtime_config_audit_key_encryption_active_key_id_total.load(
                    std::memory_order_relaxed),
            .runtime_config_audit_key_enable_data_encryption_total =
                stats_.runtime_config_audit_key_enable_data_encryption_total.load(
                    std::memory_order_relaxed),
            .runtime_config_audit_key_encryption_key_env_total =
                stats_.runtime_config_audit_key_encryption_key_env_total.load(
                    std::memory_order_relaxed),
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
        const bool sensitive_key = IsSensitiveStorageKey(entry.key);
        const bool security_denied =
            entry.reason == "invalid_token" ||
            entry.reason == "access_denied" ||
            entry.reason == "token_not_configured";
        const bool runtime_config_change =
            entry.operation == kRuntimeConfigAuditOperation;
        const bool force_audit =
            sensitive_key || security_denied || runtime_config_change;
        if (!config_.enable_audit_log && !force_audit) {
            return;
        }

        StorageEngine::AuditEntry sanitized = entry;
        if (sensitive_key) {
            sanitized.key = std::string(kRedactedAuditKey);
        }
        {
            std::lock_guard<std::mutex> lock(audit_mutex_);
            if (config_.audit_log_max_entries > 0 &&
                audit_entries_.size() >= config_.audit_log_max_entries) {
                audit_entries_.pop_front();
            }
            audit_entries_.push_back(sanitized);
        }
        IncrementStorageCounter(kAuditLogMetric);

        auto logger = spdlog::get("mir2");
        if (logger) {
            logger->info(
                "StorageEngine audit op={} key={} principal={} success={} reason={}",
                sanitized.operation,
                sanitized.key,
                sanitized.principal.empty() ? "anonymous" : sanitized.principal,
                sanitized.success,
                sanitized.reason);
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

    void PersistCapacityGovernanceStatsToL2() const {
        if (l2_cache_ == nullptr) {
            return;
        }
        const l2::RocksDBCache::CapacityGovernanceStats persisted_stats{
            .l2_soft_limit_write_total =
                stats_.l2_soft_limit_write_total.load(std::memory_order_relaxed),
            .l2_hard_limit_reject_total =
                stats_.l2_hard_limit_reject_total.load(std::memory_order_relaxed),
            .l2_hard_limit_bypass_total =
                stats_.l2_hard_limit_bypass_total.load(std::memory_order_relaxed),
        };
        if (!l2_cache_->PersistCapacityGovernanceStats(persisted_stats)) {
            auto logger = spdlog::get("mir2");
            if (logger) {
                logger->warn(
                    "StorageEngine failed to persist capacity governance stats to L2 meta");
                }
        }
    }

    void PersistTombstoneGcStatsToL2() const {
        if (l2_cache_ == nullptr) {
            return;
        }
        const l2::RocksDBCache::TombstoneGcStats persisted_stats{
            .tombstone_gc_reclaimed_total =
                stats_.tombstone_gc_reclaimed_total.load(
                    std::memory_order_relaxed),
            .tombstone_gc_failed_total =
                stats_.tombstone_gc_failed_total.load(
                    std::memory_order_relaxed),
        };
        if (!l2_cache_->PersistTombstoneGcStats(persisted_stats)) {
            auto logger = spdlog::get("mir2");
            if (logger) {
                logger->warn(
                    "StorageEngine failed to persist tombstone GC stats to L2 meta");
            }
        }
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

    bool IsNewWritePathEnabled() const {
        return enable_new_write_path_.load(std::memory_order_acquire);
    }

    void RecordWriteRejectReason(WriteRejectReason reason) const {
        IncrementStorageCounter(
            std::string("storage.write.reject.") +
            WriteRejectReasonToString(reason));
    }

    void StartCheckpointSchedulerIfNeeded() {
        if (checkpoint_scheduler_thread_.joinable()) {
            return;
        }
        if (!checkpoint_enabled_.load(std::memory_order_acquire) ||
            l2_cache_ == nullptr) {
            return;
        }
        checkpoint_scheduler_stop_.store(false, std::memory_order_release);
        checkpoint_scheduler_thread_ =
            std::thread([this]() { RunCheckpointSchedulerLoop(); });
    }

    void StopCheckpointScheduler() {
        checkpoint_scheduler_stop_.store(true, std::memory_order_release);
        checkpoint_scheduler_cv_.notify_all();
        if (checkpoint_scheduler_thread_.joinable()) {
            checkpoint_scheduler_thread_.join();
        }
        checkpoint_scheduler_stop_.store(false, std::memory_order_release);
    }

    void NotifyCheckpointScheduler() {
        checkpoint_scheduler_cv_.notify_all();
    }

    bool TombstoneGcSchedulerEnabled() const {
        return backend_ != nullptr &&
               l2_cache_ != nullptr &&
               tombstone_retention_seconds_.load(std::memory_order_acquire) > 0 &&
               tombstone_gc_interval_seconds_.load(std::memory_order_acquire) > 0;
    }

    bool ProcessDueTombstoneGcEntries() {
        if (!TombstoneGcSchedulerEnabled()) {
            return false;
        }

        constexpr size_t kReplayLimit = 256;
        const uint64_t now_ms = detail::GetCurrentTimeMs();
        size_t reclaimed = 0;
        size_t failed = 0;

        l2_cache_->ReplayDueTombstoneGc(
            kReplayLimit, now_ms,
            [this, &reclaimed, &failed](const l2::RocksDBCache::TombstoneGcEntry& entry) {
                if (backend_circuit_breaker_ &&
                    backend_circuit_breaker_->IsOpen()) {
                    PublishCircuitBreakerStateGauges();
                    ++failed;
                    stats_.tombstone_gc_failed_total.fetch_add(
                        1, std::memory_order_relaxed);
                    IncrementStorageCounter(kTombstoneGcFailedMetric);
                    PersistTombstoneGcStatsToL2();
                    return true;
                }
                if (!backend_->IsHealthy()) {
                    if (backend_circuit_breaker_) {
                        backend_circuit_breaker_->OnFailure();
                        PublishCircuitBreakerStateGauges();
                    }
                    ++failed;
                    stats_.tombstone_gc_failed_total.fetch_add(
                        1, std::memory_order_relaxed);
                    IncrementStorageCounter(kTombstoneGcFailedMetric);
                    PersistTombstoneGcStatsToL2();
                    return true;
                }

                const auto result =
                    backend_->Delete(entry.key, entry.delete_version, true);
                if (!result.success) {
                    if (backend_circuit_breaker_) {
                        backend_circuit_breaker_->OnFailure();
                        PublishCircuitBreakerStateGauges();
                    }
                    ++failed;
                    stats_.tombstone_gc_failed_total.fetch_add(
                        1, std::memory_order_relaxed);
                    IncrementStorageCounter(kTombstoneGcFailedMetric);
                    PersistTombstoneGcStatsToL2();
                    return true;
                }
                if (backend_circuit_breaker_) {
                    backend_circuit_breaker_->OnSuccess();
                    PublishCircuitBreakerStateGauges();
                }
                if (!l2_cache_->AckTombstoneGcEntry(entry.tombstone_gc_id)) {
                    ++failed;
                    stats_.tombstone_gc_failed_total.fetch_add(
                        1, std::memory_order_relaxed);
                    IncrementStorageCounter(kTombstoneGcFailedMetric);
                    PersistTombstoneGcStatsToL2();
                    return true;
                }

                ++reclaimed;
                stats_.tombstone_gc_reclaimed_total.fetch_add(
                    1, std::memory_order_relaxed);
                IncrementStorageCounter(kTombstoneGcReclaimedMetric);
                PersistTombstoneGcStatsToL2();
                return true;
            });
        return reclaimed > 0 || failed > 0;
    }

    void StartTombstoneGcSchedulerIfNeeded() {
        if (tombstone_gc_scheduler_thread_.joinable()) {
            return;
        }
        if (!TombstoneGcSchedulerEnabled()) {
            return;
        }
        tombstone_gc_scheduler_stop_.store(false, std::memory_order_release);
        tombstone_gc_scheduler_thread_ =
            std::thread([this]() { RunTombstoneGcSchedulerLoop(); });
    }

    void StopTombstoneGcScheduler() {
        tombstone_gc_scheduler_stop_.store(true, std::memory_order_release);
        tombstone_gc_scheduler_cv_.notify_all();
        if (tombstone_gc_scheduler_thread_.joinable()) {
            tombstone_gc_scheduler_thread_.join();
        }
        tombstone_gc_scheduler_stop_.store(false, std::memory_order_release);
    }

    void NotifyTombstoneGcScheduler() {
        tombstone_gc_scheduler_cv_.notify_all();
    }

private:
    bool PruneCheckpointRetentionInternal() {
        const uint32_t retention =
            checkpoint_retention_.load(std::memory_order_acquire);
        if (retention == 0) {
            return true;  // Unlimited retention.
        }
        std::error_code ec;
        if (!std::filesystem::exists(checkpoint_root_dir_, ec)) {
            return true;
        }

        std::vector<std::filesystem::path> checkpoints;
        for (const auto& entry :
             std::filesystem::directory_iterator(checkpoint_root_dir_, ec)) {
            if (ec) {
                IncrementStorageCounter(kCheckpointPruneErrorMetric);
                return false;
            }
            if (!entry.is_directory()) {
                continue;
            }
            const std::string name = entry.path().filename().string();
            if (name.rfind("cp-", 0) == 0) {
                checkpoints.push_back(entry.path());
            }
        }

        if (checkpoints.size() <= retention) {
            return true;
        }

        std::sort(checkpoints.begin(), checkpoints.end(),
                  [](const std::filesystem::path& lhs,
                     const std::filesystem::path& rhs) {
                      return lhs.filename().string() < rhs.filename().string();
                  });

        const size_t to_prune = checkpoints.size() - retention;
        bool ok = true;
        for (size_t i = 0; i < to_prune; ++i) {
            std::error_code remove_error;
            std::filesystem::remove_all(checkpoints[i], remove_error);
            if (remove_error) {
                ok = false;
                IncrementStorageCounter(kCheckpointPruneErrorMetric);
                auto logger = spdlog::get("mir2");
                if (logger) {
                    logger->warn("StorageEngine checkpoint prune failed path={} err={}",
                                 checkpoints[i].string(),
                                 remove_error.message());
                }
            } else {
                IncrementStorageCounter(kCheckpointPrunedMetric);
            }
        }
        return ok;
    }

    bool CreateScheduledCheckpointInternal() {
        if (!checkpoint_enabled_.load(std::memory_order_acquire) ||
            l2_cache_ == nullptr) {
            return false;
        }

        std::error_code ec;
        std::filesystem::create_directories(checkpoint_root_dir_, ec);
        if (ec) {
            IncrementStorageCounter(kCheckpointCreateErrorMetric);
            auto logger = spdlog::get("mir2");
            if (logger) {
                logger->warn(
                    "StorageEngine checkpoint create dir failed path={} err={}",
                    checkpoint_root_dir_.string(), ec.message());
            }
            return false;
        }

        const uint64_t now_ms = detail::GetCurrentTimeMs();
        std::filesystem::path checkpoint_path =
            checkpoint_root_dir_ / ("cp-" + std::to_string(now_ms));
        uint32_t suffix = 0;
        while (std::filesystem::exists(checkpoint_path, ec) && !ec) {
            ++suffix;
            checkpoint_path =
                checkpoint_root_dir_ /
                ("cp-" + std::to_string(now_ms) + "-" + std::to_string(suffix));
        }

        if (!l2_cache_->CreateCheckpoint(checkpoint_path.string())) {
            IncrementStorageCounter(kCheckpointCreateErrorMetric);
            return false;
        }
        IncrementStorageCounter(kCheckpointCreatedMetric);
        (void)PruneCheckpointRetentionInternal();
        return true;
    }

    void RunCheckpointSchedulerLoop() {
        auto logger = spdlog::get("mir2");
        if (logger) {
            logger->info(
                "StorageEngine checkpoint scheduler started enabled={} interval_s={} retention={}",
                checkpoint_enabled_.load(std::memory_order_acquire),
                checkpoint_interval_seconds_.load(std::memory_order_acquire),
                checkpoint_retention_.load(std::memory_order_acquire));
        }

        while (!shutdown_.load(std::memory_order_acquire) &&
               !checkpoint_scheduler_stop_.load(std::memory_order_acquire)) {
            if (checkpoint_enabled_.load(std::memory_order_acquire)) {
                (void)CreateScheduledCheckpointInternal();
            }

            const uint32_t interval_seconds = std::max(
                checkpoint_interval_seconds_.load(std::memory_order_acquire), 1u);
            std::unique_lock<std::mutex> lock(checkpoint_scheduler_mutex_);
            checkpoint_scheduler_cv_.wait_for(
                lock, std::chrono::seconds(interval_seconds),
                [this]() {
                    return shutdown_.load(std::memory_order_acquire) ||
                           checkpoint_scheduler_stop_.load(std::memory_order_acquire);
                });
        }

        if (logger) {
            logger->info("StorageEngine checkpoint scheduler stopped");
        }
    }

    void RunTombstoneGcSchedulerLoop() {
        auto logger = spdlog::get("mir2");
        if (logger) {
            logger->info(
                "StorageEngine tombstone GC scheduler started retention_s={} interval_s={}",
                tombstone_retention_seconds_.load(std::memory_order_acquire),
                tombstone_gc_interval_seconds_.load(std::memory_order_acquire));
        }

        while (!shutdown_.load(std::memory_order_acquire) &&
               !tombstone_gc_scheduler_stop_.load(std::memory_order_acquire)) {
            (void)ProcessDueTombstoneGcEntries();

            const uint32_t interval_seconds = std::max(
                tombstone_gc_interval_seconds_.load(std::memory_order_acquire), 1u);
            std::unique_lock<std::mutex> lock(tombstone_gc_scheduler_mutex_);
            tombstone_gc_scheduler_cv_.wait_for(
                lock, std::chrono::seconds(interval_seconds),
                [this]() {
                    return shutdown_.load(std::memory_order_acquire) ||
                           tombstone_gc_scheduler_stop_.load(std::memory_order_acquire);
                });
        }

        if (logger) {
            logger->info("StorageEngine tombstone GC scheduler stopped");
        }
    }

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
    std::atomic<bool> enable_new_write_path_{true};
    std::atomic<double> l2_usage_soft_limit_ratio_{0.85};
    std::atomic<double> l2_usage_hard_limit_ratio_{0.95};
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
    std::filesystem::path checkpoint_root_dir_;
    std::atomic<bool> checkpoint_enabled_{false};
    std::atomic<uint32_t> checkpoint_interval_seconds_{1800};
    std::atomic<uint32_t> checkpoint_retention_{48};
    std::atomic<bool> checkpoint_scheduler_stop_{false};
    mutable std::mutex checkpoint_scheduler_mutex_;
    std::condition_variable checkpoint_scheduler_cv_;
    std::thread checkpoint_scheduler_thread_;
    std::atomic<uint32_t> tombstone_retention_seconds_{604800};
    std::atomic<uint32_t> tombstone_gc_interval_seconds_{3600};
    std::atomic<bool> tombstone_gc_scheduler_stop_{false};
    mutable std::mutex tombstone_gc_scheduler_mutex_;
    std::condition_variable tombstone_gc_scheduler_cv_;
    std::thread tombstone_gc_scheduler_thread_;

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
        std::atomic<uint64_t> l2_soft_limit_write_total{0};
        std::atomic<uint64_t> l2_hard_limit_reject_total{0};
        std::atomic<uint64_t> l2_hard_limit_bypass_total{0};
        std::atomic<uint64_t> tombstone_gc_reclaimed_total{0};
        std::atomic<uint64_t> tombstone_gc_failed_total{0};
        std::atomic<uint64_t> runtime_config_audit_total{0};
        std::atomic<uint64_t> runtime_config_audit_failures{0};
        std::atomic<uint64_t> runtime_config_audit_reason_updated_total{0};
        std::atomic<uint64_t> runtime_config_audit_reason_l2_codec_apply_failed_total{0};
        std::atomic<uint64_t> runtime_config_audit_key_enable_access_control_total{0};
        std::atomic<uint64_t> runtime_config_audit_key_require_auth_for_reads_total{0};
        std::atomic<uint64_t> runtime_config_audit_key_access_control_token_total{0};
        std::atomic<uint64_t> runtime_config_audit_key_encryption_active_key_id_total{0};
        std::atomic<uint64_t> runtime_config_audit_key_enable_data_encryption_total{0};
        std::atomic<uint64_t> runtime_config_audit_key_encryption_key_env_total{0};
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

    const auto validation_report = instance_->pimpl_->ValidateStorageInternal();
    if (!validation_report.ok && config.startup_fail_on_validation_error) {
        if (logger) {
            logger->error(
                "StorageEngine init validation gate failed: {}",
                validation_report.summary);
        }
        g_instance_ptr.store(nullptr, std::memory_order_release);
        instance_->pimpl_->Shutdown();
        instance_.reset();
        return false;
    }
    if (!validation_report.ok && logger) {
        logger->warn(
            "StorageEngine init validation warning (startup gate disabled): {}",
            validation_report.summary);
    }

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
    return pimpl_->PutWithReasonLocked(key, versioned, options, nullptr);
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
        WriteRejectReason reason = WriteRejectReason::kNone;
        switch (item.op) {
        case BatchWriteItem::Op::kPut:
            if (!IsValidStorageKey(item.key)) {
                reason = WriteRejectReason::kInvalidKey;
                ok = false;
                break;
            }
            if (item.value.size() > kMaxStorageValueSize) {
                reason = WriteRejectReason::kValueTooLarge;
                ok = false;
                break;
            }
            {
                size_t stripe = pimpl_->GetStripe(item.key);
                std::unique_lock<std::shared_mutex> lock(pimpl_->locks_[stripe]);
                VersionedData versioned{
                    pimpl_->NextVersion(),
                    item.value,
                    detail::GetCurrentTimeMs()
                };
                ok = pimpl_->PutWithReasonLocked(
                    item.key, versioned, item.write_options, &reason);
            }
            if (!ok) {
                reason = reason == WriteRejectReason::kNone
                    ? WriteRejectReason::kPutFailed
                    : reason;
            }
            break;
        case BatchWriteItem::Op::kDelete:
            if (!IsValidStorageKey(item.key)) {
                reason = WriteRejectReason::kInvalidKey;
                ok = false;
                break;
            }
            ok = Delete(item.key, item.delete_options);
            if (!ok) {
                reason = WriteRejectReason::kDeleteFailed;
            }
            break;
        }

        if (ok) {
            ++result.succeeded;
        } else {
            ++result.failed;
            result.failed_keys.push_back(item.key);
            if (reason == WriteRejectReason::kNone) {
                reason = WriteRejectReason::kPutFailed;
            }
            result.failure_reasons.push_back(WriteRejectReasonToString(reason));
            result.failure_reason_codes.push_back(reason);
            pimpl_->RecordWriteRejectReason(reason);
        }
    }

    return result;
}

bool StorageEngine::Set(const std::string& key,
                        const std::vector<uint8_t>& data,
                        Priority priority) {
    if (!pimpl_->IsNewWritePathEnabled()) {
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
        if (pimpl_->IsSyncWriteCriticalKey(key)) {
            return pimpl_->SetSyncInternal(key, versioned, Priority::CRITICAL);
        }
        return pimpl_->SetInternal(
            key, versioned, priority, /*enforce_sync_prefix_semantics=*/true);
    }

    WriteOptions options;
    options.durability = WriteDurability::kBestEffort;
    options.priority = priority;
    options.bypass_sync_prefix_upgrade = false;
    return Put(key, data, options);
}

bool StorageEngine::SetAsyncDurable(const std::string& key,
                                    const std::vector<uint8_t>& data,
                                    Priority priority) {
    if (!pimpl_->IsNewWritePathEnabled()) {
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
        return pimpl_->SetInternal(
            key, versioned, priority, /*enforce_sync_prefix_semantics=*/false);
    }

    WriteOptions options;
    options.durability = WriteDurability::kDurableAsync;
    options.priority = priority;
    options.bypass_sync_prefix_upgrade = true;
    return Put(key, data, options);
}

bool StorageEngine::SetSync(const std::string& key,
                            const std::vector<uint8_t>& data,
                            Priority priority) {
    if (!pimpl_->IsNewWritePathEnabled()) {
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
    if (!pimpl_->IsNewWritePathEnabled()) {
        for (const auto& [key, value] : kvs) {
            if (!IsValidStorageKey(key) || value.size() > kMaxStorageValueSize) {
                return false;
            }
        }
        for (const auto& [key, value] : kvs) {
            if (!Set(key, value, priority)) {
                return false;
            }
        }
        return true;
    }

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

bool StorageEngine::PutWithAccess(const std::string& key,
                                  const std::vector<uint8_t>& data,
                                  const AccessContext& access,
                                  const WriteOptions& options) {
    std::string deny_reason;
    const bool allowed =
        pimpl_->CheckAccessInternal(AccessOperation::kSet, key, access,
                                    &deny_reason);
    pimpl_->RecordAccessDecision(allowed);
    if (!allowed) {
        pimpl_->RecordAuditEntry(AuditEntry{
            .timestamp_ms = detail::GetCurrentTimeMs(),
            .principal = access.principal,
            .operation = "put",
            .key = key,
            .success = false,
            .reason = deny_reason,
        });
        pimpl_->RecordWriteRejectReason(WriteRejectReason::kAccessDenied);
        return false;
    }
    const bool success = Put(key, data, options);
    pimpl_->RecordAuditEntry(AuditEntry{
        .timestamp_ms = detail::GetCurrentTimeMs(),
        .principal = access.principal,
        .operation = "put",
        .key = key,
        .success = success,
        .reason = success ? "ok" : "put_failed",
    });
    return success;
}

bool StorageEngine::DeleteWithAccess(const std::string& key,
                                     const AccessContext& access,
                                     const DeleteOptions& options) {
    std::string deny_reason;
    const bool allowed = pimpl_->CheckAccessInternal(
        AccessOperation::kInvalidate, key, access, &deny_reason);
    pimpl_->RecordAccessDecision(allowed);
    if (!allowed) {
        pimpl_->RecordAuditEntry(AuditEntry{
            .timestamp_ms = detail::GetCurrentTimeMs(),
            .principal = access.principal,
            .operation = "delete",
            .key = key,
            .success = false,
            .reason = deny_reason,
        });
        pimpl_->RecordWriteRejectReason(WriteRejectReason::kAccessDenied);
        return false;
    }
    const bool success = Delete(key, options);
    pimpl_->RecordAuditEntry(AuditEntry{
        .timestamp_ms = detail::GetCurrentTimeMs(),
        .principal = access.principal,
        .operation = "delete",
        .key = key,
        .success = success,
        .reason = success ? "ok" : "delete_failed",
    });
    return success;
}

BatchWriteResult StorageEngine::BatchWriteWithAccess(
    const std::vector<BatchWriteItem>& items,
    const AccessContext& access) {
    if (!pimpl_->IsNewWritePathEnabled()) {
        for (const auto& item : items) {
            std::string deny_reason;
            const bool allowed = pimpl_->CheckAccessInternal(
                AccessOperation::kBatchSet, item.key, access, &deny_reason);
            pimpl_->RecordAccessDecision(allowed);
            if (!allowed) {
                const WriteRejectReason reason =
                    DenyReasonToWriteRejectReason(deny_reason);
                BatchWriteResult denied;
                denied.total = items.size();
                denied.failed = items.size();
                denied.failed_keys.reserve(items.size());
                denied.failure_reasons.reserve(items.size());
                denied.failure_reason_codes.reserve(items.size());
                for (const auto& failed_item : items) {
                    const WriteRejectReason item_reason =
                        IsValidStorageKey(failed_item.key)
                            ? reason
                            : WriteRejectReason::kInvalidKey;
                    denied.failed_keys.push_back(failed_item.key);
                    denied.failure_reasons.push_back(
                        WriteRejectReasonToString(item_reason));
                    denied.failure_reason_codes.push_back(item_reason);
                }
                pimpl_->RecordWriteRejectReason(reason);
                pimpl_->RecordAuditEntry(AuditEntry{
                    .timestamp_ms = detail::GetCurrentTimeMs(),
                    .principal = access.principal,
                    .operation = "batch_write",
                    .key = item.key,
                    .success = false,
                    .reason = deny_reason,
                });
                pimpl_->RecordAuditEntry(AuditEntry{
                    .timestamp_ms = detail::GetCurrentTimeMs(),
                    .principal = access.principal,
                    .operation = "batch_write",
                    .key = "<batch>",
                    .success = false,
                    .reason = "batch_write_failed",
                });
                return denied;
            }
            if (item.op == BatchWriteItem::Op::kPut &&
                item.value.size() > kMaxStorageValueSize) {
                BatchWriteResult denied;
                denied.total = items.size();
                denied.failed = items.size();
                denied.failed_keys.reserve(items.size());
                denied.failure_reasons.reserve(items.size());
                denied.failure_reason_codes.reserve(items.size());
                for (const auto& failed_item : items) {
                    denied.failed_keys.push_back(failed_item.key);
                    denied.failure_reasons.push_back(
                        WriteRejectReasonToString(
                            WriteRejectReason::kValueTooLarge));
                    denied.failure_reason_codes.push_back(
                        WriteRejectReason::kValueTooLarge);
                }
                pimpl_->RecordWriteRejectReason(
                    WriteRejectReason::kValueTooLarge);
                pimpl_->RecordAuditEntry(AuditEntry{
                    .timestamp_ms = detail::GetCurrentTimeMs(),
                    .principal = access.principal,
                    .operation = "batch_write",
                    .key = item.key,
                    .success = false,
                    .reason = "value_too_large",
                });
                pimpl_->RecordAuditEntry(AuditEntry{
                    .timestamp_ms = detail::GetCurrentTimeMs(),
                    .principal = access.principal,
                    .operation = "batch_write",
                    .key = "<batch>",
                    .success = false,
                    .reason = "batch_write_failed",
                });
                return denied;
            }
        }

        auto result = BatchWrite(items);
        for (size_t i = 0; i < result.failed_keys.size(); ++i) {
            const std::string& failed_key = result.failed_keys[i];
            const std::string reason =
                i < result.failure_reasons.size() ? result.failure_reasons[i]
                                                  : "batch_write_failed";
            pimpl_->RecordAuditEntry(AuditEntry{
                .timestamp_ms = detail::GetCurrentTimeMs(),
                .principal = access.principal,
                .operation = "batch_write",
                .key = failed_key,
                .success = false,
                .reason = reason,
            });
        }
        pimpl_->RecordAuditEntry(AuditEntry{
            .timestamp_ms = detail::GetCurrentTimeMs(),
            .principal = access.principal,
            .operation = "batch_write",
            .key = "<batch>",
            .success = result.failed == 0,
            .reason = result.failed == 0 ? "ok" : "batch_write_failed",
        });
        return result;
    }

    BatchWriteResult result;
    result.total = items.size();

    std::vector<BatchWriteItem> authorized_items;
    authorized_items.reserve(items.size());

    for (const auto& item : items) {
        std::string deny_reason;
        const bool allowed = pimpl_->CheckAccessInternal(
            AccessOperation::kBatchSet, item.key, access, &deny_reason);
        pimpl_->RecordAccessDecision(allowed);
        if (!allowed) {
            const WriteRejectReason reason =
                DenyReasonToWriteRejectReason(deny_reason);
            ++result.failed;
            result.failed_keys.push_back(item.key);
            result.failure_reasons.push_back(WriteRejectReasonToString(reason));
            result.failure_reason_codes.push_back(reason);
            pimpl_->RecordWriteRejectReason(reason);
            pimpl_->RecordAuditEntry(AuditEntry{
                .timestamp_ms = detail::GetCurrentTimeMs(),
                .principal = access.principal,
                .operation = "batch_write",
                .key = item.key,
                .success = false,
                .reason = deny_reason,
            });
            continue;
        }

        authorized_items.push_back(item);
    }

    if (!authorized_items.empty()) {
        auto authorized_result = BatchWrite(authorized_items);
        for (size_t i = 0; i < authorized_result.failed_keys.size(); ++i) {
            const std::string& failed_key = authorized_result.failed_keys[i];
            const std::string reason =
                i < authorized_result.failure_reasons.size()
                    ? authorized_result.failure_reasons[i]
                    : "batch_write_failed";
            pimpl_->RecordAuditEntry(AuditEntry{
                .timestamp_ms = detail::GetCurrentTimeMs(),
                .principal = access.principal,
                .operation = "batch_write",
                .key = failed_key,
                .success = false,
                .reason = reason,
            });
        }
        result.succeeded += authorized_result.succeeded;
        result.failed += authorized_result.failed;
        result.failed_keys.insert(result.failed_keys.end(),
                                  authorized_result.failed_keys.begin(),
                                  authorized_result.failed_keys.end());
        result.failure_reasons.insert(result.failure_reasons.end(),
                                      authorized_result.failure_reasons.begin(),
                                      authorized_result.failure_reasons.end());
        result.failure_reason_codes.insert(result.failure_reason_codes.end(),
                                           authorized_result.failure_reason_codes.begin(),
                                           authorized_result.failure_reason_codes.end());
    }

    pimpl_->RecordAuditEntry(AuditEntry{
        .timestamp_ms = detail::GetCurrentTimeMs(),
        .principal = access.principal,
        .operation = "batch_write",
        .key = "<batch>",
        .success = result.failed == 0,
        .reason = result.failed == 0 ? "ok" : "batch_write_failed",
    });
    return result;
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
    if (!pimpl_->IsNewWritePathEnabled()) {
        for (const auto& key : keys) {
            std::string deny_reason;
            const bool allowed = pimpl_->CheckAccessInternal(
                AccessOperation::kBatchGet, key, access, &deny_reason);
            pimpl_->RecordAccessDecision(allowed);
            if (!allowed) {
                pimpl_->RecordAuditEntry(AuditEntry{
                    .timestamp_ms = detail::GetCurrentTimeMs(),
                    .principal = access.principal,
                    .operation = "batch_get",
                    .key = key,
                    .success = false,
                    .reason = deny_reason,
                });
                pimpl_->RecordAuditEntry(AuditEntry{
                    .timestamp_ms = detail::GetCurrentTimeMs(),
                    .principal = access.principal,
                    .operation = "batch_get",
                    .key = "<batch>",
                    .success = false,
                    .reason = "batch_get_failed",
                });
                return std::vector<std::optional<VersionedData>>(
                    keys.size(), std::nullopt);
            }
        }

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

    std::vector<std::optional<VersionedData>> result(keys.size(), std::nullopt);
    std::vector<std::string> allowed_keys;
    std::vector<size_t> allowed_indices;
    allowed_keys.reserve(keys.size());
    allowed_indices.reserve(keys.size());
    size_t denied_count = 0;

    for (size_t i = 0; i < keys.size(); ++i) {
        const auto& key = keys[i];
        std::string deny_reason;
        const bool allowed = pimpl_->CheckAccessInternal(
            AccessOperation::kBatchGet, key, access, &deny_reason);
        pimpl_->RecordAccessDecision(allowed);
        if (!allowed) {
            ++denied_count;
            pimpl_->RecordAuditEntry(AuditEntry{
                .timestamp_ms = detail::GetCurrentTimeMs(),
                .principal = access.principal,
                .operation = "batch_get",
                .key = key,
                .success = false,
                .reason = deny_reason,
            });
            continue;
        }

        allowed_keys.push_back(key);
        allowed_indices.push_back(i);
    }

    if (!allowed_keys.empty()) {
        auto allowed_results = BatchGet(allowed_keys);
        for (size_t i = 0; i < allowed_results.size() && i < allowed_indices.size();
             ++i) {
            result[allowed_indices[i]] = std::move(allowed_results[i]);
        }
    }

    std::string batch_reason = "ok";
    if (denied_count == keys.size() && !keys.empty()) {
        batch_reason = "batch_get_failed";
    } else if (denied_count > 0) {
        batch_reason = "batch_get_partial_denied";
    }

    pimpl_->RecordAuditEntry(AuditEntry{
        .timestamp_ms = detail::GetCurrentTimeMs(),
        .principal = access.principal,
        .operation = "batch_get",
        .key = "<batch>",
        .success = denied_count == 0,
        .reason = batch_reason,
    });
    return result;
}

bool StorageEngine::BatchSetWithAccess(
    const std::vector<std::pair<std::string, std::vector<uint8_t>>>& kvs,
    const AccessContext& access,
    Priority priority) {
    if (!pimpl_->IsNewWritePathEnabled()) {
        for (const auto& [key, value] : kvs) {
            (void)value;
            std::string deny_reason;
            const bool allowed = pimpl_->CheckAccessInternal(
                AccessOperation::kBatchSet, key, access, &deny_reason);
            pimpl_->RecordAccessDecision(allowed);
            if (!allowed) {
                pimpl_->RecordAuditEntry(AuditEntry{
                    .timestamp_ms = detail::GetCurrentTimeMs(),
                    .principal = access.principal,
                    .operation = "batch_set",
                    .key = key,
                    .success = false,
                    .reason = deny_reason,
                });
                pimpl_->RecordAuditEntry(AuditEntry{
                    .timestamp_ms = detail::GetCurrentTimeMs(),
                    .principal = access.principal,
                    .operation = "batch_set",
                    .key = "<batch>",
                    .success = false,
                    .reason = "batch_set_failed",
                });
                return false;
            }
        }
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

    const auto result = BatchWriteWithAccess(items, access);
    const bool success = result.failed == 0;
    if (!success) {
        for (size_t i = 0; i < result.failed_keys.size(); ++i) {
            const std::string& failed_key = result.failed_keys[i];
            const std::string reason =
                i < result.failure_reasons.size() ? result.failure_reasons[i]
                                                  : "batch_set_failed";
            pimpl_->RecordAuditEntry(AuditEntry{
                .timestamp_ms = detail::GetCurrentTimeMs(),
                .principal = access.principal,
                .operation = "batch_set",
                .key = failed_key,
                .success = false,
                .reason = reason,
            });
        }
    }
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
