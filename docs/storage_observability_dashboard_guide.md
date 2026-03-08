# Storage 告警/看板阈值策略（P2）

## 1. 适用范围

覆盖 `StorageEngine` 与 DB 后端关键可靠性指标，重点包含：

- 写入可靠性：`storage_strict_write_fail_total`、`storage_strict_write_fallback_total`
- 持久化队列：`storage_queue_pending`、`storage_queue_high_depth`、`storage_queue_normal_depth`
- DLQ：`storage_dead_letter_depth`、`storage_dead_letter_enqueued_total`、`storage_dead_letter_dropped_total`
- 熔断状态：`storage_circuit_breaker_l2_state`、`storage_circuit_breaker_backend_state`
- DB 连接池：`storage_db_pool_usage_ratio`、`storage_db_pool_in_use`、`storage_db_pool_available`
- 失效广播：`storage_invalidation_broadcast_publish_total`、`storage_invalidation_broadcast_publish_fail_total`
- 访问控制：`storage_access_allow_total`、`storage_access_deny_total`

说明：代码通过 `Metrics::NormalizeMetricName` 将 `.` 归一化为 `_`，PromQL 一律使用下划线名。

## 2. 阈值分级策略

- `warning`：持续异常，10 分钟内需要人工确认。
- `critical`：持续高风险，5 分钟内需要处理或升级值班。
- 瞬时尖峰默认不报警，统一加 `for` 窗口避免噪声。

## 3. 关键阈值

- DB 连接池使用率：
  - `warning`: `avg_over_time(storage_db_pool_usage_ratio[10m]) > 0.85`
  - `critical`: `avg_over_time(storage_db_pool_usage_ratio[5m]) > 0.95`
- 熔断器状态（0=CLOSED, 1=OPEN, 2=HALF_OPEN）：
  - `critical`: `max_over_time(storage_circuit_breaker_l2_state[2m]) == 1`
  - `critical`: `max_over_time(storage_circuit_breaker_backend_state[2m]) == 1`
- DLQ：
  - `warning`: `max_over_time(storage_dead_letter_depth[10m]) > 100`
  - `critical`: `max_over_time(storage_dead_letter_depth[5m]) > 1000`
  - `critical`: `increase(storage_dead_letter_dropped_total[10m]) > 0`
- 队列积压：
  - `warning`: `max_over_time(storage_queue_pending[10m]) > 2000`
- 其余保留：
  - 严格写失败 `> 0`（critical）
  - outbox 拒绝率 `> 1/s`（warning）
  - 恢复错误增量 `> 0`（warning）

## 4. 看板落地

文件：`config/prometheus/grafana/storage_observability_dashboard.json`

新增面板：

- `DB Pool Usage Ratio`
- `DB Pool InUse / Available`
- `Circuit Breaker State`
- `DLQ Depth`
- `DLQ Enqueued / Dropped Rate`
- `Queue Pending / Priority Depth`
- `Invalidation Broadcast Publish / Fail Rate`
- `Access Deny Ratio`

## 5. 告警规则落地

文件：`config/prometheus/alerts.yaml`

新增规则：

- `StorageDbPoolUsageHigh`
- `StorageDbPoolUsageCritical`
- `StorageL2CircuitBreakerOpen`
- `StorageBackendCircuitBreakerOpen`
- `StorageQueuePendingHigh`
- `StorageDeadLetterDepthHigh`
- `StorageDeadLetterDepthCritical`
- `StorageDeadLetterDroppedDetected`

## 6. 值班处置顺序

1. 先看 `StorageBackendCircuitBreakerOpen` 与 `StorageStrictWriteFailures`。
2. 再看 `StorageDbPoolUsageCritical` 是否导致后端阻塞。
3. 若 `StorageDeadLetterDroppedDetected` 触发，优先扩容或止损，避免持续丢弃。
4. 最后处理 `StorageQueuePendingHigh` 与性能退化项。
