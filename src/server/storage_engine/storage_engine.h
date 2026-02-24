/**
 * @file storage_engine.h
 * @brief 统一存储引擎 - 整合缓存与持久化
 *
 * 设计原则：
 * - 单一入口：StorageEngine::Instance()
 * - PIMPL隔离：零头文件污染
 * - 三层缓存：L1(内存) + L2(RocksDB) + L3(PostgreSQL)
 * - 分段锁：64路并发，读写分离
 */

#ifndef MIR2_STORAGE_ENGINE_STORAGE_ENGINE_H_
#define MIR2_STORAGE_ENGINE_STORAGE_ENGINE_H_

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "storage_engine/interfaces/storage_backend.h"

namespace mir2::logic {
template <typename T>
class Task;
class CoroutineExecutor;
}  // namespace mir2::logic

namespace mir2::storage_engine {

// ===== 公开值类型 =====
struct VersionedData {
    uint64_t version;
    std::vector<uint8_t> data;
    uint64_t timestamp_ms;  // 使用uint64_t避免窄化
};

enum class Priority {
    LOW = 0,
    NORMAL = 1,
    HIGH = 2,
    CRITICAL = 3
};

// ===== 核心引擎 =====
class StorageEngine {
public:
    ~StorageEngine();

    struct Config {
        // L1缓存配置
        uint32_t l1_max_entries = 10000;
        uint32_t l1_ttl_seconds = 300;

        // L2缓存配置
        uint32_t l2_max_size_mb = 512;
        std::string l2_path = "/var/lib/mir2/cache";
        uint32_t l2_ttl_seconds = 604800;  // 7 days — WAL entries must survive restarts

        // 持久化配置
        uint32_t auto_sync_interval_ms = 5000;
        uint32_t batch_size = 100;
        uint32_t sync_timeout_ms = 30000;
        size_t queue_capacity = 10000;
        size_t queue_worker_threads = 2;
        uint32_t queue_retry_count = 3;
        uint32_t queue_retry_delay_ms = 100;
        size_t dead_letter_max_items = 10000;
        bool enable_strict_write_guarantee = true;
        std::vector<std::string> critical_key_prefixes;
        std::vector<std::string> sync_write_key_prefixes = {"char:"};
        bool critical_data_no_ttl = true;
        bool enable_outbox = false;
        size_t outbox_replay_limit = 0;  // 0 means replay all.
        size_t outbox_max_items = 200000;  // 0 means unlimited.

        // 熔断器配置
        uint32_t circuit_breaker_threshold = 5;
        uint32_t circuit_breaker_timeout_ms = 60000;

        // 监控
        bool enable_metrics = true;
        bool enable_audit_log = true;
        size_t audit_log_max_entries = 5000;

        // 存储接口鉴权
        bool enable_access_control = false;
        bool require_auth_for_reads = false;
        std::string access_control_token;
    };

    struct RuntimeTunableConfig {
        std::optional<uint32_t> l1_ttl_seconds;
        std::optional<uint32_t> auto_sync_interval_ms;
        std::optional<uint32_t> batch_size;
        std::optional<uint32_t> queue_retry_count;
        std::optional<uint32_t> queue_retry_delay_ms;
        std::optional<uint32_t> circuit_breaker_threshold;
        std::optional<uint32_t> circuit_breaker_timeout_ms;
        std::optional<bool> enable_metrics;
        std::optional<bool> enable_strict_write_guarantee;
        std::optional<bool> enable_access_control;
        std::optional<bool> require_auth_for_reads;
        std::optional<std::string> access_control_token;
    };

    enum class AccessOperation : uint8_t {
        kGet = 0,
        kSet = 1,
        kSetSync = 2,
        kLoadFromDB = 3,
        kInvalidate = 4,
        kInvalidateByPrefix = 5,
        kBatchGet = 6,
        kBatchSet = 7,
    };

    struct AccessContext {
        std::string principal;
        std::string access_token;
        bool trusted = false;
    };

    struct AuditEntry {
        uint64_t timestamp_ms = 0;
        std::string principal;
        std::string operation;
        std::string key;
        bool success = false;
        std::string reason;
    };

    enum class InvalidationEventType : uint8_t {
        kKey = 0,
        kPrefix = 1,
    };

    struct InvalidationEvent {
        InvalidationEventType type = InvalidationEventType::kKey;
        std::string key_or_prefix;
        uint64_t timestamp_ms = 0;
        bool from_broadcast = false;
    };

    using InvalidationCallback = std::function<void(const InvalidationEvent&)>;
    using InvalidationInboundHandler =
        std::function<void(const InvalidationEvent&)>;

    // Cross-process invalidation adapter (e.g. Redis/MQ/DB notify).
    // Local in-process invalidation bus remains enabled by default.
    class IInvalidationAdapter {
    public:
        virtual ~IInvalidationAdapter() = default;

        // Register inbound handler and start adapter runtime.
        virtual bool Start(InvalidationInboundHandler inbound_handler) = 0;
        virtual void Stop() = 0;

        // Publish local invalidation to external channel.
        virtual bool Publish(const InvalidationEvent& event) = 0;
    };

    /**
     * @brief 初始化存储引擎（必须在启动时调用）
     */
    static bool Initialize(
        std::unique_ptr<IStorageBackend> backend,
        const Config& config);

    static bool Initialize(std::unique_ptr<IStorageBackend> backend) {
        return Initialize(std::move(backend), Config{});
    }

    /**
     * @brief 获取单例实例
     */
    static StorageEngine& Instance();

    /**
     * @brief 检查是否已初始化
     */
    static bool IsInitialized() noexcept;

    /**
     * @brief 优雅关闭
     */
    static void Shutdown();

    // ============= 核心API =============

    /**
     * @brief API 1: 纯读取 (L1→L2, 无DB)
     *
     * 性能目标：<1ms (L1命中)，<5ms (L2命中)
     * 场景：读取静态数据、坐标查询
     */
    std::optional<VersionedData> Get(const std::string& key);

    /**
     * @brief 协程友好的异步读取
     *
     * L1命中时同步返回，L1 Miss时切后台线程执行L2查询。
     */
    mir2::logic::Task<std::optional<VersionedData>> GetAsync(
        const std::string& key,
        mir2::logic::CoroutineExecutor& executor);

    /**
     * @brief API 2: 直接写入 (无版本检查)
     *
     * 性能目标：<1ms
     * 场景：坐标更新、快照保存
     */
    bool Set(const std::string& key,
             const std::vector<uint8_t>& data,
             Priority priority = Priority::NORMAL);

    /**
     * @brief API 2.0: Durable async write (bypass sync-prefix auto-upgrade)
     *
     * 与 Set() 的区别：
     * - 不会因 sync_write_key_prefixes 自动升级为 SetSync；
     * - 仍保证至少写入 L2 / backend / outbox 之一，否则返回 false。
     */
    bool SetAsyncDurable(const std::string& key,
                         const std::vector<uint8_t>& data,
                         Priority priority = Priority::HIGH);

    /**
     * @brief API 2.1: 强制同步写入 (WAL fsync)
     *
     * 关键路径使用：充值、交易确认、稀有掉落
     * 语义：成功返回时，数据已写入 L2 持久层并完成 fsync。
     */
    bool SetSync(const std::string& key,
                 const std::vector<uint8_t>& data,
                 Priority priority = Priority::CRITICAL);

    bool CompareAndSet(const std::string& key,
                       uint64_t expected_version,
                       const std::vector<uint8_t>& data,
                       Priority priority = Priority::HIGH);

    /**
     * @brief API 3: 条件更新 (基于当前值计算)
     *
     * ⚠️ 重要约束：update_fn 必须是纯计算函数
     * - ✅ 允许：内存中的数值运算、逻辑判断、数据序列化
     * - ❌ 禁止：I/O操作、访问其他key、调用外部服务、持有其他锁
     *
     * @param key 数据键
     * @param update_fn 更新函数（纯计算）：
     *   - 输入：std::optional<VersionedData>（当前值，可能为空）
     *   - 输出：std::optional<std::vector<uint8_t>>（新值，空表示放弃更新）
     *   - 执行时机：释放读锁后、获取写锁前（无锁状态）
     *   - 执行约束：必须在 <100μs 内完成（纯内存计算）
     * @param max_retries 版本冲突时的最大重试次数
     * @param priority 优先级
     * @return true 成功更新或业务逻辑放弃，false 达到最大重试次数
     *
     * 性能目标：<5ms (含重试)
     * 场景：金币消费、物品栏操作
     *
     * 使用示例：
     * @code
     * // 扣除50金币（余额不足则放弃）
     * engine.Update("player:123:gold",
     *     [](const std::optional<VersionedData>& current)
     *         -> std::optional<std::vector<uint8_t>> {
     *         if (!current) return std::nullopt;
     *
     *         int32_t gold = DecodeInt32(current->data);
     *         if (gold < 50) return std::nullopt;
     *
     *         return EncodeInt32(gold - 50);
     *     }
     * );
     * @endcode
     */
    using UpdateFunction = std::function<
        std::optional<std::vector<uint8_t>>(const std::optional<VersionedData>&)
    >;

    bool Update(const std::string& key,
                UpdateFunction update_fn,
                int max_retries = 5,
                Priority priority = Priority::HIGH);

    /**
     * @brief API 4: 同步加载 (从DB回填)
     *
     * 性能目标：<50ms (磁盘IO)
     * 场景：玩家登录、数据恢复
     * 特性：自动回填L2+L1
     */
    std::optional<VersionedData> LoadFromDB(const std::string& key);

    /**
     * @brief 协程友好的异步加载（从DB回填）
     */
    mir2::logic::Task<std::optional<VersionedData>> LoadFromDBAsync(
        const std::string& key,
        mir2::logic::CoroutineExecutor& executor);

    std::vector<std::optional<VersionedData>> BatchGet(
        const std::vector<std::string>& keys);

    bool BatchSet(
        const std::vector<std::pair<std::string, std::vector<uint8_t>>>& kvs,
        Priority priority = Priority::NORMAL);

    bool BatchLoadFromDB(
        const std::vector<std::string>& keys,
        std::vector<std::optional<VersionedData>>* out);

    std::optional<VersionedData> GetWithAccess(
        const std::string& key,
        const AccessContext& access);
    bool SetWithAccess(
        const std::string& key,
        const std::vector<uint8_t>& data,
        const AccessContext& access,
        Priority priority = Priority::NORMAL);
    bool SetSyncWithAccess(
        const std::string& key,
        const std::vector<uint8_t>& data,
        const AccessContext& access,
        Priority priority = Priority::CRITICAL);
    std::optional<VersionedData> LoadFromDBWithAccess(
        const std::string& key,
        const AccessContext& access);
    std::vector<std::optional<VersionedData>> BatchGetWithAccess(
        const std::vector<std::string>& keys,
        const AccessContext& access);
    bool BatchSetWithAccess(
        const std::vector<std::pair<std::string, std::vector<uint8_t>>>& kvs,
        const AccessContext& access,
        Priority priority = Priority::NORMAL);

    bool Invalidate(const std::string& key);
    size_t InvalidateByPrefix(const std::string& prefix);
    bool InvalidateWithAccess(
        const std::string& key,
        const AccessContext& access);
    size_t InvalidateByPrefixWithAccess(
        const std::string& prefix,
        const AccessContext& access);
    bool InvalidateFromBroadcast(const std::string& key);
    size_t InvalidateByPrefixFromBroadcast(const std::string& prefix);

    bool CheckAccess(AccessOperation op,
                     const std::string& key,
                     const AccessContext& access,
                     std::string* deny_reason = nullptr) const;
    std::vector<AuditEntry> GetRecentAuditEntries(size_t limit = 100) const;
    uint64_t SubscribeInvalidation(InvalidationCallback callback);
    bool UnsubscribeInvalidation(uint64_t subscription_id);
    bool SetInvalidationAdapter(std::shared_ptr<IInvalidationAdapter> adapter);
    void ClearInvalidationAdapter();
    bool ApplyRuntimeConfig(const RuntimeTunableConfig& cfg);

    // ============= 生命周期API =============

    /**
     * @brief 主动触发同步
     */
    bool Flush(uint32_t timeout_ms = 5000);

    /**
     * @brief 优雅关闭快照
     */
    bool CreateShutdownSnapshot();

    /**
     * @brief 启动恢复
     */
    bool PerformStartupRecovery();

    // ============= 监控API =============

    struct HealthMetrics {
        bool is_healthy;
        double l1_hit_ratio;
        double l2_hit_ratio;
        size_t l1_size;
        size_t l2_size;
        int64_t pending_syncs;
        uint32_t circuit_breaker_failures;
        uint8_t l2_circuit_breaker_state;
        uint8_t backend_circuit_breaker_state;
        size_t high_priority_queue_depth;
        size_t normal_priority_queue_depth;
        size_t outbox_depth;
        size_t dead_letter_depth;

        // Update操作统计
        uint64_t total_updates;
        uint64_t successful_updates;
        uint64_t update_conflicts;
        uint64_t update_retries;
        uint64_t update_aborted;
        uint64_t slow_update_fn_count;

        // 性能指标
        double avg_update_latency_ms;
        double p99_update_latency_ms;
    };

    HealthMetrics GetHealthMetrics() const noexcept;

private:
    StorageEngine();
    StorageEngine(const StorageEngine&) = delete;
    StorageEngine& operator=(const StorageEngine&) = delete;

    static std::unique_ptr<StorageEngine> instance_;
    static std::mutex instance_mutex_;

    // ===== PIMPL - 完全隐藏实现 =====
    class Impl;
    std::unique_ptr<Impl> pimpl_;
};

}  // namespace mir2::storage_engine

#endif  // MIR2_STORAGE_ENGINE_STORAGE_ENGINE_H_
