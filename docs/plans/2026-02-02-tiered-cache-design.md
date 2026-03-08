# 多层缓存架构设计文档

**设计日期**: 2026-02-02
**状态**: 已批准，待实施
**预计工期**: 3-4 周

---

## 执行摘要

本设计实施**本地化多层缓存架构**，采用 L1 内存 (tsl::lru_map) + L2 RocksDB + PostgreSQL 三层存储，配合全局无锁 Hybrid Logical Clock (HLC) 实现版本控制。核心目标：

- **性能**: L1 <1ms, L2 <50μs, 单节点 10K ops/s
- **成本**: 移除 Redis 依赖，简化运维
- **一致性**: HLC 版本控制 + 熔断降级机制
- **可靠性**: 高优先级 <100ms 持久化，崩溃恢复 <1s

---

## 1. 架构设计

### 1.1 整体架构

```
┌────────────────────────────────────┐
│        Gateway (网关层)            │
│   (静态路由 + 强一致性 Hash)        │
└─────────────────┬──────────────────┘
                  │ (Sticky Session)
                  ▼
┌────────────────────────────────────┐
│     Game Server Node (Stateful)    │
│ ┌─────────────┐    ┌─────────────┐ │
│ │ L1: Memory  │───▶│ L2: RocksDB │ │
│ │ (tsl::map)  │◀───│ (NVMe SSD)  │ │
│ └──────┬──────┘    └──────┬──────┘ │
│        │ <1ms             │ <50us  │
└────────┼──────────────────┼────────┘
         │                  │
         │ (Write-Behind)   │ (30s Flush)
         ▼                  ▼
┌────────────────────────────────────┐
│       PostgreSQL (Global DB)       │
│     (最终持久化 + 跨节点共享)       │
└────────────────────────────────────┘
```

### 1.2 核心设计决策

| 决策点 | 选择 | 理由 |
|--------|------|------|
| **L2 存储** | RocksDB (本地 SSD) | 性能 (<50μs) + 成本 (无网络 IO) + 一致性 (Sticky Session) |
| **一致性策略** | Write-Through (L1+L2) + Write-Behind (PG) | 平衡性能与可靠性 |
| **版本控制** | GlobalHybridClock (HLC) | 防时钟回退 + 无锁高性能 |
| **容错机制** | CircuitBreaker + 降级 | 故障隔离，L2 失败降级到 L1+PG |
| **持久化策略** | 分级 (高优先级 <100ms，低优先级 30s) | 满足不同数据可靠性需求 |

---

## 2. 核心组件设计

### 2.1 GlobalHybridClock (全局无锁时钟)

**职责**: 生成单调递增版本号，处理时钟回退

**关键特性**:
- 无锁 CAS 操作 (1000万+ ops/s)
- HLC 编码: `[48 bits 物理时间(ms)][16 bits 逻辑计数器]`
- 崩溃恢复: 从 RocksDB max_version 恢复
- 时钟回退处理: 保留旧物理时间 + 递增逻辑计数器

**接口**:
```cpp
class GlobalHybridClock {
public:
    explicit GlobalHybridClock(RocksDBCache* l2_cache = nullptr);
    uint64_t Next();  // 生成下一个版本号

private:
    std::atomic<uint64_t> clock_{0};
    void RecoverFromPersistence();
    uint64_t ScanMaxVersionInDB();
};
```

**崩溃恢复逻辑**:
```
1. 读取持久化的 HLC 状态
2. 扫描 RocksDB 获取 max_version
3. 获取当前系统时间
4. 选择三者最大值初始化 HLC
5. 如果时钟回退，使用 max_version 继续
```

---

### 2.2 LocalLRUCache (L1 内存缓存)

**职责**: 热数据内存缓存，LRU 淘汰

**关键特性**:
- 基于 tsl::lru_map (header-only)
- 固定容量 1000 条目 (~20MB)
- shared_mutex 读写锁 (多读单写)
- shared_ptr 零拷贝数据共享

**接口**:
```cpp
class LocalLRUCache {
public:
    using DataPtr = std::shared_ptr<const std::vector<uint8_t>>;

    struct VersionedData {
        uint64_t version;
        DataPtr data;
        uint64_t timestamp_ms;
    };

    std::optional<VersionedData> Get(const std::string& key);
    void Set(const std::string& key, const VersionedData& data);

private:
    tsl::lru_map<std::string, VersionedData> cache_;
    mutable std::shared_mutex mutex_;
};
```

---

### 2.3 RocksDBCache (L2 本地持久化)

**职责**: SSD 持久化缓存，支持 TTL 和批量写入

**关键特性**:
- DBWithTTL 自动过期 (1 小时)
- Bloom Filter (10 bits/key, 1% 假阳性)
- Block Cache (256MB)
- WriteBatch 批量写入
- PinnableSlice 零拷贝读取
- max_version 元数据维护

**配置**:
```cpp
struct Config {
    std::string db_path = "./data/rocksdb";
    size_t write_buffer_size = 64 * 1024 * 1024;
    size_t block_cache_size = 256 * 1024 * 1024;
    int32_t ttl_seconds = 3600;
    double bloom_filter_bits_per_key = 10.0;
};
```

**接口**:
```cpp
class RocksDBCache {
public:
    std::optional<VersionedData> Get(const std::string& key);
    bool Set(const std::string& key, const VersionedData& data);
    bool BatchSet(const std::vector<std::pair<std::string, VersionedData>>& batch);

private:
    rocksdb::DBWithTTL* db_ = nullptr;  // 手动释放
    std::shared_ptr<rocksdb::Cache> block_cache_;
};
```

---

### 2.4 TieredCache (多层协调器)

**职责**: 统一缓存接口，协调 L1/L2/PG 三层

**核心 API**:

#### API 1: SetState (直接覆盖)
```cpp
bool SetState(const std::string& key,
              const std::vector<uint8_t>& data,
              Priority priority = Priority::NORMAL);
```
- **用途**: 位置更新、战斗属性快照
- **特点**: HLC 自动生成版本，无并发检查
- **性能**: <1ms

#### API 2: UpdateWithRetry (依赖旧值)
```cpp
template<typename UpdateFunc>
bool UpdateWithRetry(const std::string& key,
                    UpdateFunc update_fn,
                    int max_retries = 5,
                    Priority priority = Priority::HIGH);
```
- **用途**: 金币操作、物品栏修改
- **特点**: CAS 版本检查 + 自动重试
- **性能**: <5ms (含重试)

#### API 3: Get (纯缓存读取)
```cpp
std::optional<VersionedData> Get(const std::string& key);
```
- **流程**: L1 → L2 → nullopt (不查 DB)
- **回填**: L2 命中自动回填 L1

#### API 4: LoadFromDBAsync (DB 加载)
```cpp
std::future<std::optional<VersionedData>> LoadFromDBAsync(
    const std::string& key);
```
- **用途**: 玩家登录、数据恢复
- **回填**: 自动回填 L1 + L2

---

### 2.5 CircuitBreaker (熔断器)

**职责**: L2 故障检测与降级

**触发条件**:
- 连续失败 ≥ 10 次
- 失败率 > 50% (最小 20 请求)

**状态机**:
```
CLOSED (正常)
  ↓ (失败率超标)
OPEN (熔断，拒绝请求)
  ↓ (30秒后)
HALF_OPEN (限流恢复)
  ↓ (成功 3 次)
CLOSED (恢复)
```

**降级策略**:
```cpp
// L2 熔断时降级
if (l2_circuit_breaker_.IsOpen()) {
    l1_->Set(key, data);                    // 只写 L1
    pg_queue_->Enqueue(key, data, HIGH);    // 直接写 PG (高优先级)
    return true;
}
```

---

### 2.6 AsyncPersistenceQueue (异步持久化)

**职责**: 分级持久化到 PostgreSQL

**队列设计**:
- **高优先级队列**: 立即单条提交 (<100ms)
- **普通队列**: 批量提交 (30s 或 100 条触发)

**接口**:
```cpp
class AsyncPersistenceQueue {
public:
    enum class Priority { HIGH, NORMAL };

    bool Enqueue(const std::string& key,
                 const VersionedData& data,
                 Priority priority);
    bool FlushAll(uint32_t timeout_ms = 5000);  // Graceful shutdown
};
```

---

## 3. 数据流与性能

### 3.1 写入流程

#### SetState (位置更新)
```
业务调用 → SetState
  ↓
HLC.Next() 生成版本号 (无锁 CAS)
  ↓
L2 RocksDB 写入 (带熔断保护, <50μs)
  ↓
L1 内存写入 (<1μs)
  ↓
PG 普通队列 (30s 批量)
  ↓
返回成功 (总延迟 <1ms)
```

#### UpdateWithRetry (金币操作)
```
业务调用 → UpdateWithRetry
  ↓
L1 读取当前值 (old_version)
  ↓
业务逻辑计算新值
  ↓
HLC.Next() 生成 new_version
  ↓
版本检查 (current > old_version?)
  ├─ 是 → 重试 (退避 10ms)
  └─ 否 → 继续
  ↓
L2 + L1 写入
  ↓
PG 高优先级队列 (立即提交)
  ↓
返回成功 (总延迟 <5ms)
```

### 3.2 读取流程

```
业务调用 → Get(key)
  ↓
L1 查找
  ├─ 命中 → 返回 (<1ms)
  └─ 未命中 ↓
L2 查找 (带熔断检查)
  ├─ 命中 → 回填 L1 → 返回 (<5ms)
  └─ 未命中 → 返回 nullopt

业务判断 → 需要加载? → LoadFromDBAsync
  ↓
PostgreSQL 查询 (<50ms)
  ↓
回填 L2 + L1
  ↓
返回数据
```

### 3.3 性能指标

| 操作 | 目标延迟 (P99) | 吞吐量 |
|------|---------------|--------|
| SetState | <1ms | 20K ops/s |
| UpdateWithRetry | <5ms | 10K ops/s |
| Get (L1 命中) | <1ms | 100K ops/s |
| Get (L2 命中) | <5ms | 50K ops/s |
| LoadFromDBAsync | <50ms | 1K ops/s |
| 高优先级持久化 | <100ms | 500 ops/s |

---

## 4. 故障处理

### 4.1 L2 RocksDB 故障

**检测**: CircuitBreaker 连续失败 ≥ 10 次

**降级流程**:
```
1. 熔断器进入 OPEN 状态
2. 所有写入跳过 L2
3. 直接写入 L1 + 高优先级 PG
4. 30 秒后进入 HALF_OPEN 尝试恢复
```

**影响**:
- 性能轻微下降 (无 L2 持久化)
- 依赖 L1 + PG 保证数据安全

---

### 4.2 节点崩溃恢复

**场景**: 节点宕机，玩家重新登录到新节点

**时间线**:
```
T0: Node-1 宕机
  - L1 内存数据丢失
  - L2 RocksDB 数据保留 (SSD 持久化)
  - PG 有最近 30 秒的快照

T1: Gateway 检测到 Node-1 不健康
  - 路由玩家到 Node-2

T2: 玩家在 Node-2 登录
  - LoadFromDBAsync 从 PG 加载
  - 可能丢失最近 30 秒低优先级数据 (位置)
  - 高优先级数据 (装备、金币) 已持久化

T3: 数据回填到 Node-2 的 L1 + L2
  - 玩家恢复正常游戏
```

**RPO/RTO**:
- RPO (高优先级): <100ms
- RPO (低优先级): <30s
- RTO: <50ms

---

### 4.3 时钟回退恢复

**场景**: 服务器重启 + NTP 时钟回退

**防护措施**:
```
1. 启动时读取 RocksDB max_version
2. 读取持久化的 HLC 状态
3. 获取当前系统时间
4. 选择三者最大值初始化 HLC
5. 如果 HLC > 系统时间，保持 HLC 时间不变
6. 后续写入递增逻辑计数器
```

**监控告警**:
```cpp
if (hlc_physical_ms - system_time_ms > 3600000) {
    SendAlert("HLC drift > 1 hour, check NTP service");
}
```

---

## 5. 监控与可观测性

### 5.1 Prometheus 指标

```
# 缓存性能
cache_l1_hits_total
cache_l1_misses_total
cache_l2_hits_total
cache_l2_misses_total
cache_operation_duration_seconds{layer="l1|l2"}

# 持久化
persistence_enqueued_total{priority="high|normal"}
persistence_success_total
persistence_failed_total
persistence_latency_seconds{priority="high|normal"}

# 熔断器
circuit_breaker_state{state="closed|open|half_open"}
circuit_breaker_rejected_total

# HLC 健康
hlc_time_drift_ms
hlc_logical_counter_overflow_total
```

### 5.2 健康检查接口

```cpp
struct HealthMetrics {
    bool is_healthy;
    CircuitBreaker::State l2_state;
    double l1_hit_ratio;
    double l2_hit_ratio;
    int64_t hlc_time_drift_ms;
    size_t pg_queue_depth;
};

HealthMetrics GetHealthMetrics() const;
```

---

## 6. 实施计划

### 第 1 周: 基础层实现

**任务**:
- [ ] GlobalHybridClock 实现与单元测试
- [ ] LocalLRUCache 集成 tsl::lru_map
- [ ] RocksDBCache 完整实现 (Bloom Filter + WriteBatch)
- [ ] VersionedData 数据结构定义

**验收标准**:
- HLC 单元测试通过 (含时钟回退场景)
- RocksDB 读写性能达标 (<50μs)
- L1 LRU 淘汰策略正确

---

### 第 2 周: TieredCache 协调层

**任务**:
- [ ] TieredCache::SetState 实现
- [ ] TieredCache::UpdateWithRetry 实现
- [ ] TieredCache::Get 实现 (含 L1 回填)
- [ ] TieredCache::LoadFromDBAsync 实现
- [ ] CircuitBreaker 集成

**验收标准**:
- 两个 API 功能测试通过
- 熔断器触发与恢复验证
- 并发写入测试 (金币操作无丢失)

---

### 第 3 周: 持久化与恢复

**任务**:
- [ ] AsyncPersistenceQueue 实现
- [ ] 高低优先级队列分离
- [ ] HLC 持久化与恢复逻辑
- [ ] max_version 元数据维护
- [ ] 崩溃恢复集成测试

**验收标准**:
- 高优先级 <100ms 持久化验证
- 低优先级 30s 批量验证
- 崩溃重启后版本号连续性

---

### 第 4 周: 监控、优化与压测

**任务**:
- [ ] Prometheus 指标暴露
- [ ] 健康检查接口
- [ ] 性能基准测试 (10K ops/s)
- [ ] 边缘场景测试 (时钟回退、L2 故障)
- [ ] 文档与运维手册

**验收标准**:
- 所有性能指标达标
- Grafana 面板可用
- 故障演练通过

---

## 7. 依赖项

**vcpkg.json 更新**:
```json
{
  "dependencies": [
    "rocksdb",
    "tsl-ordered-map",
    "nlohmann-json",
    "libpq",
    "spdlog"
  ]
}
```

---

## 8. 风险评估

| 风险 | 影响 | 概率 | 缓解措施 |
|------|------|------|---------|
| RocksDB 学习曲线 | 开发延期 1 周 | 中 | 提前阅读文档，参考开源项目 |
| HLC 实现 bug | 版本冲突导致数据丢失 | 低 | 充分单元测试，边缘场景覆盖 |
| L2 性能不达标 | 延迟超标 | 低 | Bloom Filter + Block Cache 调优 |
| 时钟严重漂移 | 服务异常 | 低 | 监控告警 + 运维 SOP |
| 内存泄漏 | 节点 OOM | 中 | Valgrind 检测 + Code Review |

---

## 9. 成功标准

**功能完整性**:
- ✅ 所有 API 正常工作
- ✅ 版本控制无冲突
- ✅ 熔断降级生效
- ✅ 崩溃恢复正确

**性能达标**:
- ✅ L1 读取 <1ms (P99)
- ✅ L2 读取 <5ms (P99)
- ✅ 单节点吞吐 10K ops/s
- ✅ L1 命中率 >95%

**可靠性保证**:
- ✅ 高优先级 RPO <100ms
- ✅ 低优先级 RPO <30s
- ✅ RTO <1s
- ✅ 时钟回退安全

**运维友好**:
- ✅ Prometheus 指标可用
- ✅ 健康检查正常
- ✅ 故障演练通过
- ✅ 运维文档完整

---

## 10. 后续优化方向

**Phase 2 (可选)**:
- Redis Sentinel 作为跨节点共享层 (玩家迁移)
- 多节点数据一致性协议 (Vector Clock)
- 更细粒度的 TTL 策略
- 自动化性能调优工具

---

**文档版本**: 1.0
**最后更新**: 2026-02-02
**审批状态**: ✅ 已批准
**实施负责人**: Claude Code + 开发团队
