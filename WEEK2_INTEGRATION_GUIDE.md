# Week 2 TieredCache 集成指南

## 快速开始

### 1. 最小化集成示例

```cpp
#include "cache/tiered_cache.h"
#include "cache/postgres_backend_interface.h"

using namespace mir2::cache;

// 实现 PostgresBackend 接口
class MyPostgresBackend : public PostgresBackend {
public:
    bool SaveEntity(const std::string& key, const VersionedData& data,
                    uint64_t version) override {
        // 调用数据库保存数据
        return db_->SaveEntity(key, data.data.get(), data.version);
    }

    uint32_t BatchSaveEntities(
        const std::vector<std::pair<std::string, VersionedData>>& batch) override {
        // 批量保存
        return db_->BatchSaveEntities(batch);
    }

    std::optional<VersionedData> LoadEntity(const std::string& key) override {
        // 加载数据
        return db_->LoadEntity(key);
    }

    bool IsHealthy() const override {
        return db_->IsConnected();
    }
};

// 初始化
int main() {
    // 配置
    TieredCache::Config config;
    config.l1_config.capacity = 1000;
    config.l2_config.db_path = "./data/rocksdb";
    config.pg_queue_config.batch_size = 100;
    config.circuit_breaker_config.failure_threshold = 10;

    // 创建后端
    auto pg_backend = std::make_shared<MyPostgresBackend>();

    // 创建缓存
    auto cache = std::make_unique<TieredCache>(pg_backend, config);

    // 使用
    auto data = std::vector<uint8_t>{'h', 'e', 'l', 'l', 'o'};
    cache->SetState("player:1000:pos", data, Priority::NORMAL);

    // 优雅关闭
    cache->Shutdown();

    return 0;
}
```

### 2. 游戏场景集成

#### 场景 A: 玩家登录（数据加载）

```cpp
void OnPlayerLogin(uint32_t player_id) {
    std::string player_key = "player:" + std::to_string(player_id);

    // 异步加载玩家数据
    auto future = cache_->LoadFromDBAsync(player_key);

    // 异步等待
    std::thread loader([future = std::move(future), player_id]() {
        auto player_data = future.get();
        if (player_data) {
            LOG(INFO) << "Player " << player_id << " loaded from DB";
            // 数据已回填到 L1+L2
        } else {
            LOG(WARN) << "Player " << player_id << " not found in DB";
            // 创建新玩家
        }
    });
    loader.detach();
}
```

#### 场景 B: 位置更新（SetState - 高频、低优先级）

```cpp
void OnPlayerMove(uint32_t player_id, int x, int y) {
    std::string pos_key = "player:" + std::to_string(player_id) + ":pos";

    // 编码位置数据
    std::vector<uint8_t> pos_data;
    pos_data.push_back(x);
    pos_data.push_back(y);

    // 直接覆盖，普通优先级（30s 后持久化）
    cache_->SetState(pos_key, pos_data, Priority::NORMAL);
}
```

#### 场景 C: 金币操作（UpdateWithRetry - 低频、高优先级）

```cpp
bool AddPlayerGold(uint32_t player_id, uint32_t gold_amount) {
    std::string gold_key = "player:" + std::to_string(player_id) + ":gold";

    return cache_->UpdateWithRetry(
        gold_key,
        [gold_amount](const std::optional<VersionedData>& current)
            -> std::optional<std::shared_ptr<const std::vector<uint8_t>>> {

            // 读取当前金币数
            uint32_t current_gold = 0;
            if (current && current->GetSize() >= 4) {
                // 从字节流反序列化（省略细节）
                current_gold = DeserializeUint32(current->data);
            }

            // 计算新金币数
            uint32_t new_gold = current_gold + gold_amount;

            // 序列化返回
            auto data = std::make_shared<std::vector<uint8_t>>();
            SerializeUint32(*data, new_gold);
            return data;
        },
        5,              // 最多重试 5 次
        Priority::HIGH  // 高优先级，快速持久化
    );
}
```

#### 场景 D: 背包操作（UpdateWithRetry - 需要冲突检测）

```cpp
bool AddItemToInventory(uint32_t player_id, uint32_t item_id, uint32_t count) {
    std::string inv_key = "player:" + std::to_string(player_id) + ":inventory";

    return cache_->UpdateWithRetry(
        inv_key,
        [item_id, count](const std::optional<VersionedData>& current) {
            // 反序列化当前背包
            Inventory inv = current ? DeserializeInventory(*current)
                                    : Inventory();

            // 检查空间
            if (!inv.HasSpace(count)) {
                return std::optional<std::shared_ptr<const std::vector<uint8_t>>>();
            }

            // 添加物品
            inv.AddItem(item_id, count);

            // 序列化并返回
            auto serialized = SerializeInventory(inv);
            return std::optional<std::shared_ptr<const std::vector<uint8_t>>>(
                std::make_shared<const std::vector<uint8_t>>(
                    serialized.begin(), serialized.end()));
        },
        5, Priority::HIGH);
}
```

### 3. 故障处理和监控

#### 健康检查集成

```cpp
void HealthCheckLoop() {
    while (!shutdown_) {
        auto metrics = cache_->GetHealthMetrics();

        // 记录健康指标
        LOG(INFO) << "TieredCache Health:"
                  << " is_healthy=" << metrics.is_healthy
                  << " l2_state=" << static_cast<int>(metrics.l2_state)
                  << " l1_hit_ratio=" << metrics.l1_hit_ratio
                  << " l2_hit_ratio=" << metrics.l2_hit_ratio
                  << " pg_queue_depth=" << metrics.pg_queue_depth;

        // 告警逻辑
        if (!metrics.is_healthy) {
            SendAlert("TieredCache health check failed!");
        }

        std::this_thread::sleep_for(std::chrono::seconds(10));
    }
}
```

#### 故障降级处理

```cpp
void HandleL2Failure() {
    // CircuitBreaker 自动进入 OPEN 状态
    // TieredCache 自动降级写入逻辑：
    // 1. 写 L1 内存缓存
    // 2. 写高优先级异步队列（立即提交到 PG）
    // 3. 等待 30 秒后 CircuitBreaker 转入 HALF_OPEN
    // 4. 尝试恢复 L2，成功 3 次后完全恢复

    auto metrics = cache_->GetHealthMetrics();
    if (metrics.l2_state == CircuitBreaker::State::OPEN) {
        LOG(WARN) << "L2 CircuitBreaker is OPEN, degraded mode active";
        // 可选：触发报警、人工介入等
    }
}
```

### 4. 性能调参

#### 场景 1: 高频位置更新（MMO 游戏）

```cpp
TieredCache::Config config;
config.l1_config.capacity = 10000;      // 更大的 L1 缓存
config.l2_config.block_cache_size = 512 * 1024 * 1024;  // 512MB
config.pg_queue_config.batch_size = 1000;      // 较大的批量
config.pg_queue_config.batch_interval_ms = 60000;  // 60s 批处理
config.circuit_breaker_config.failure_threshold = 20;
```

#### 场景 2: 关键数据快速持久化（交易、充值）

```cpp
TieredCache::Config config;
config.l1_config.capacity = 5000;       // 适中 L1
config.pg_queue_config.worker_threads = 4;  // 更多工作线程
config.circuit_breaker_config.failure_rate_threshold = 0.3;  // 更敏感
config.circuit_breaker_config.open_timeout_ms = 10000;  // 更快恢复
```

#### 场景 3: 内存受限环境（低端服务器）

```cpp
TieredCache::Config config;
config.l1_config.capacity = 500;        // 较小 L1
config.l2_config.write_buffer_size = 32 * 1024 * 1024;  // 32MB
config.l2_config.block_cache_size = 128 * 1024 * 1024;  // 128MB
config.pg_queue_config.batch_size = 50;
```

## 测试命令

### 编译新组件

```bash
cd /e/mir2-cpp
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target mir2_cache
```

### 运行单元测试

```bash
cd build
ctest --output-on-failure \
    -R "CircuitBreakerTest|AsyncPersistenceQueueTest|TieredCacheTest"
```

### 性能基准测试

```bash
ctest -R "Performance|Benchmark" -V
```

### 并发压力测试

```bash
ctest -R "Concurrent" -V
```

## 集成检查清单

- [ ] PostgresBackend 接口实现完成
- [ ] TieredCache::Config 根据业务调参
- [ ] SetState API 用于高频更新
- [ ] UpdateWithRetry API 用于关键数据
- [ ] Get API 用于数据查询
- [ ] LoadFromDBAsync API 用于数据恢复
- [ ] 健康检查集成到监控系统
- [ ] 故障降级逻辑测试通过
- [ ] 性能基准测试通过
- [ ] 并发测试通过
- [ ] 内存泄漏检查通过 (Valgrind)
- [ ] 生产环境灰度测试

## 常见问题

### Q1: UpdateWithRetry 什么时候重试？

A: 当 L1 中的版本号已经被更新（并发冲突）时重试，最多 5 次，每次退避 10ms。

### Q2: 数据在哪一层丢失风险最高？

A: L1 内存。建议关键数据使用 HIGH 优先级，在 100ms 内快速持久化。

### Q3: L2 RocksDB 故障会持续多久？

A: CircuitBreaker OPEN 状态持续 30 秒，之后自动尝试 HALF_OPEN 恢复。

### Q4: 如何监控 CircuitBreaker 状态？

A: 通过 GetHealthMetrics() 获取 l2_state 和 l2_consecutive_failures。

### Q5: 异步持久化失败会丢失数据吗？

A: 失败会重试最多 3 次，仍失败则记录日志。建议监控 persisted_failed 指标。

## 附录：关键字段说明

| 字段 | 含义 | 范围 |
|------|------|------|
| version | HLC 版本号 | uint64_t |
| timestamp_ms | 写入时间戳 | 毫秒 |
| l1_hit_ratio | L1 命中率 | 0.0 ~ 1.0 |
| l2_hit_ratio | L2 命中率 | 0.0 ~ 1.0 |
| pg_queue_depth | 待处理队列深度 | 0 ~ capacity |
| failure_rate | 故障率 | 0.0 ~ 1.0 |

---

**版本**: 1.0
**最后更新**: 2026-02-02
**维护者**: Claude Code Development Team
