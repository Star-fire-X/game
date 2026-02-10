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

        // 熔断器配置
        uint32_t circuit_breaker_threshold = 5;
        uint32_t circuit_breaker_timeout_ms = 60000;

        // 监控
        bool enable_metrics = true;
        bool enable_audit_log = true;
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
     * @brief API 2.1: 强制同步写入 (WAL fsync)
     *
     * 关键路径使用：充值、交易确认、稀有掉落
     * 语义：成功返回时，数据已写入 L2 持久层并完成 fsync。
     */
    bool SetSync(const std::string& key,
                 const std::vector<uint8_t>& data,
                 Priority priority = Priority::CRITICAL);

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
