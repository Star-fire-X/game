# StorageEngine Phase2 `enable_v2_encode` 生产灰度 Runbook

## 1. 目标与范围

1. 在不破坏现网读写稳定性的前提下，将写路径从 V1 编码灰度切换到 V2 编码。
2. 本手册仅覆盖配置开关 `enable_v2_encode` 的灰度发布（5% -> 25% -> 100%）。
3. 灰度期间固定保持 `enable_v2_read_fallback=true`，不在本手册内执行 fallback 关闭动作。

## 2. 开关与生效路径

配置项位于 `config/logic.yaml` 的 `storage_engine` 段：

1. `enable_v2_encode`
2. `enable_v2_read_fallback`

运行时生效路径：

1. 修改目标实例配置文件。
2. 发送 `SIGUSR1` 触发 `LogicServer::ReloadStorageRuntimeConfig()`。
3. 观察日志中 storage runtime reload 成功信息。

## 3. 前置条件（必须全部满足）

1. `016_kv_store_soft_delete` 迁移已在生产执行并验收通过。
2. 当前生产基线配置为：
   - `enable_v2_encode=false`
   - `enable_v2_read_fallback=true`
3. 已有可用回滚通道（配置回退 + `SIGUSR1` 热重载）。
4. 已建立观测面（至少包含下列指标）：
   - 业务 SLI：登录成功率、关键写成功率、错误率、写路径 P99
   - 存储健康：`enable_v2_encode`、`enable_v2_read_fallback`
   - Codec 计数：`l2_v2_decode_reads`、`l2_v1_fallback_reads`、`l2_v1_reject_reads`、`l2_decode_errors`

## 4. 三阶段灰度计划

### 阶段 A：5%

操作：

1. 选择 5% Logic 实例（建议单机房单分组）。
2. 设置：
   - `enable_v2_encode=true`
   - `enable_v2_read_fallback=true`
3. 发送 `SIGUSR1` 热重载。

观察窗口：

1. 至少 2 小时，覆盖一个业务小高峰。

放行门禁（全部满足才可进入 25%）：

1. 配置生效：`enable_v2_encode=true` 且 `enable_v2_read_fallback=true`。
2. `l2_v1_reject_reads` 不增长（应保持 0）。
3. `l2_decode_errors` 无持续增长趋势。
4. 写路径 P99 相对基线劣化不超过 10%。
5. 业务错误率无显著上升（建议不超过基线 +20%）。

### 阶段 B：25%

操作：

1. 扩大到 25% Logic 实例。
2. 配置保持：
   - `enable_v2_encode=true`
   - `enable_v2_read_fallback=true`
3. 批次滚动 + 每批热重载后验收。

观察窗口：

1. 至少 6 小时，建议覆盖高峰。

放行门禁（全部满足才可进入 100%）：

1. 阶段 A 门禁持续满足。
2. `l2_v2_decode_reads` 持续增长（表明 V2 数据读路径正常）。
3. `l2_v1_fallback_reads` 可增长但无异常突刺；总趋势可解释（历史 V1 数据被读取）。
4. 无 P1/P0 事故、无持续性回放/死信异常增长。

### 阶段 C：100%

操作：

1. 全量实例切换 `enable_v2_encode=true`，继续保留 `enable_v2_read_fallback=true`。
2. 按机房/分组分批发布，避免同时抖动。

观察窗口：

1. 至少 24 小时（跨峰值时段）。

收敛门禁（阶段完成条件）：

1. 阶段 B 门禁持续满足。
2. 全量业务 SLI 稳定，无新增系统性故障。
3. 回滚演练路径可用（抽样实例执行回退并恢复成功）。

## 5. 发布与回退操作模板

### 发布（单批次）

1. 修改实例 `config/logic.yaml`：

```yaml
storage_engine:
  enable_v2_encode: true
  enable_v2_read_fallback: true
```

2. 热重载：

```bash
kill -USR1 <logic_pid>
```

### 回退（秒级止损）

触发条件（任一满足）：

1. 业务 SLI 快速恶化并可归因于本次灰度。
2. `l2_decode_errors` 持续增长且超过既定告警阈值。
3. 出现无法接受的写路径延迟退化或错误突增。

回退动作：

1. 将目标批次实例配置回退为：

```yaml
storage_engine:
  enable_v2_encode: false
  enable_v2_read_fallback: true
```

2. 对目标批次立即发送 `SIGUSR1`。
3. 5 分钟内确认错误率与 P99 回落。
4. 若未回落，停止扩大并执行批量回滚到上一稳定批次。

## 6. 记录模板（每个灰度档必须填写）

1. 灰度阶段：5% / 25% / 100%
2. 批次实例范围：
3. 开始时间 / 结束时间：
4. 配置与重载操作人：
5. 门禁结果：
6. 指标截图与结论：
7. 是否进入下一档：
8. 是否触发回退：
