# StorageEngine 一致性与恢复加固方案

**日期**: 2026-02-10  
**状态**: 已确认，待实施  
**范围**: `src/server/storage_engine/`、`src/server/logic/`、相关测试

---

## 1. 背景与目标

当前三层链路为 `L1 Memory -> L2 RocksDB -> L3 PostgreSQL`。现有实现存在以下风险：

1. 数据丢失窗口：普通 `Set()` 不是 `fsync` 提交。
2. 异步队列丢失：内存队列在进程崩溃时会丢未落库项。
3. TTL 过期风险：恢复依赖 L2，TTL 到期后可能丢失补偿来源。

本方案目标：

1. 关键路径写入做到“确认即持久化”。
2. 非关键路径具备“崩溃后可重放”能力。
3. 关键数据不受 TTL 回收影响。
4. 保持版本单调，避免旧版本覆盖新版本。

---

## 2. 已锁定决策

1. P0：禁止 queue-only 成功语义。  
L2 不可用时，必须同步写 PG 成功，否则直接失败。

2. P0：关键 key 默认走 `SetSync`。  
采用“前缀白名单”配置策略。

3. P1：异步队列增加本地持久化 outbox。  
介质选型：RocksDB 独立 Column Family（CF）。

4. P1：TTL 隔离。  
关键 key 写入无 TTL 空间（永不过期）；非关键 key 可保持 TTL。

5. 失败语义（已确认）：  
当 L2 不可用且 PG 同步写失败/超时时，写请求直接失败，不允许降级内存成功。

---

## 3. 高层架构调整

### 3.1 写入语义调整

- **关键 key**：
  - 默认走 `SetSync`（L2 WAL fsync）。
  - 若 L2 不可用，尝试 PG 同步写；成功才返回成功。

- **非关键 key**：
  - 先写 L2（原有路径）。
  - 同时写 Durable Outbox（本地持久化）。
  - 后台 worker 异步刷 PG，成功后 ack outbox。

### 3.2 存储分层调整

RocksDB 内部拆分 CF：

1. `cf_data_persistent`：关键数据，无 TTL。
2. `cf_data_ttl`：非关键数据，可 TTL。
3. `cf_outbox`：待落 PG 的持久化队列项。
4. `cf_meta`：元数据（如 `max_version`）。

### 3.3 启动恢复流程

1. 先初始化 RocksDB 多 CF。
2. 回放 `cf_outbox` 到内存工作队列（或直接后台重试）。
3. 执行 L2 -> PG 版本追平恢复。
4. 恢复 HLC 最大版本，确保版本不回退。

---

## 4. 接口与配置变更

## 4.1 `StorageEngine::Config` 新增字段

```cpp
std::vector<std::string> critical_key_prefixes;
bool enable_strict_write_guarantee = true;
uint32_t pg_sync_timeout_ms = 3000;
bool enable_outbox = true;
size_t outbox_max_items = 200000;
bool critical_data_no_ttl = true;
```

### 4.2 内部接口新增（建议）

```cpp
bool IsCriticalKey(const std::string& key) const;
bool PersistToBackendSync(const std::string& key,
                          const VersionedData& data,
                          uint32_t timeout_ms);
```

### 4.3 `AsyncPersistenceQueue` 扩展（建议）

```cpp
bool EnqueueDurable(const std::string& key,
                    const VersionedData& data,
                    Priority priority);

bool AckDurable(uint64_t outbox_id);
size_t ReplayFromOutbox(size_t limit);
```

`Stats` 增加：

- `outbox_depth`
- `outbox_replayed`
- `outbox_acked`
- `outbox_failed`

---

## 5. 分阶段实施明细

## P0-1 禁止 queue-only 成功语义

目标文件：

- `src/server/storage_engine/storage_engine.cc`

改造点：

1. 修改 `SetInternal` 判定逻辑：
   - 现状：`l2_persisted || queued_for_persistence` 即成功。
   - 目标：
     - 若 L2 成功：继续流程。
     - 若 L2 失败：强制 `PersistToBackendSync`；成功才返回成功。
     - PG 同步失败/超时：直接失败。

2. `SetSyncInternal` 增强：
   - L2 不可用时，允许 PG 同步兜底。
   - 兜底失败仍返回失败。

## P0-2 关键 key 默认 `SetSync`

目标文件：

- `src/server/storage_engine/storage_engine.cc`
- `src/server/storage_engine/storage_engine.h`
- `src/server/logic/logic_server.cc`（配置注入）

改造点：

1. 在 `Set`/`Update` 路径统一调用 `IsCriticalKey`。
2. 命中关键前缀时自动升级为 `SetSync` 路径。
3. 关键前缀由配置集中管理，避免调用方散落判断。

## P1-1 Durable Outbox（RocksDB CF）

目标文件：

- `src/server/storage_engine/l2/rocksdb_cache.h`
- `src/server/storage_engine/l2/rocksdb_cache.cc`
- `src/server/storage_engine/persistence/async_persistence_queue.h`
- `src/server/storage_engine/persistence/async_persistence_queue.cc`
- `src/server/storage_engine/storage_engine.cc`

改造点：

1. 在 `RocksDBCache` 增加 outbox 读写与删除 API。
2. `EnqueueDurable` 流程：先落 outbox 再入内存队列。
3. worker 写 PG 成功后 ack 删除 outbox 项。
4. 启动时回放 outbox，恢复未完成任务。
5. 增加上限控制与告警，避免无限堆积。

## P1-2 TTL 隔离

目标文件：

- `src/server/storage_engine/l2/rocksdb_cache.h`
- `src/server/storage_engine/l2/rocksdb_cache.cc`
- `src/server/storage_engine/storage_engine.cc`

改造点：

1. 拆分关键/非关键数据 CF。
2. 关键 key 读写固定走 `cf_data_persistent`（无 TTL）。
3. 非关键 key 走 `cf_data_ttl`。
4. 启动恢复扫描以持久 CF 与 outbox 为主，不依赖 TTL CF。

---

## 6. 测试计划与验收标准

## 6.1 单元测试

目标目录：

- `tests/server/storage_engine/`
- `tests/server/storage_engine_test.cc`

新增/调整用例：

1. `L2 down + PG down`：`Set` 必须失败，不可读到新值。
2. `L2 down + PG up`：`Set` 成功且 PG 可读。
3. 关键前缀命中：`Set` 自动走同步路径。
4. outbox 崩溃恢复：重启后可重放并最终落 PG。
5. outbox ack 幂等：重复 ack 不破坏状态。
6. TTL 隔离：关键 key 长时间后仍可恢复；非关键 key 可过期。

## 6.2 集成测试

目标目录：

- `tests/integration/`

新增/调整场景：

1. 正常停机：`Flush + Shutdown` 后 PG 与 L2 版本一致。
2. 崩溃重启：未刷 PG 项通过 outbox + recovery 追平。
3. PG 长时不可用恢复后：版本不回退，最终追平。

## 6.3 验收门槛

1. 不再存在 L2 失败但 queue-only 返回成功的路径。
2. 关键 key 满足“确认即持久化”语义。
3. 非关键 key 崩溃后可重放恢复。
4. 关键 key 不受 TTL 清理影响。

---

## 7. 观测与告警

建议新增指标：

1. `storage.strict_write_fallback_total`（L2->PG 同步兜底次数）
2. `storage.strict_write_fail_total`
3. `storage.outbox.depth`
4. `storage.outbox.replay_total`
5. `storage.outbox.ack_total`
6. `storage.outbox.fail_total`
7. `storage.recovery.recovered_total`
8. `storage.recovery.error_total`

建议告警：

1. outbox 深度持续增长
2. strict write 失败率升高
3. recovery 错误持续出现

---

## 8. 发布与回滚建议

1. **阶段一（灰度）**：仅加配置与指标，不切换语义。
2. **阶段二（启用 P0）**：打开严格写保证与关键前缀策略。
3. **阶段三（启用 P1）**：启用 durable outbox 与 TTL 隔离。
4. **阶段四（收敛）**：移除旧 queue-only 成功分支。

回滚原则：

1. 保留开关，支持快速关闭新路径。
2. 回滚时不删除 outbox 数据，避免恢复中断。

---

## 9. 风险与注意事项

1. 开启严格语义后，短期写失败率可能上升（换取不丢数保证）。
2. 关键 key `SetSync` 会增加写延迟，需容量评估。
3. outbox 与持久 CF 增加磁盘占用，需配合容量阈值。
4. 多 CF 改造涉及启动兼容与历史数据迁移，需提供一次性迁移逻辑或读时迁移策略。

---

## 10. 结论

该方案在不引入外部新中间件的前提下，利用现有 RocksDB + PG 架构实现：

1. 关键数据“确认不丢”。
2. 非关键数据“崩溃可补偿”。
3. TTL 与恢复职责解耦。
4. 版本语义与恢复路径统一。

可作为 StorageEngine 下一阶段的一致性与可靠性基线。
