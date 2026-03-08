# Legacy Fallback 灰度收敛运维手册

## 1. 目标与范围

- 目标：在不引入大面积失败的前提下，逐步收敛 Logic 侧 `legacy fallback`。
- 范围：`mir2_logic` 配置项（`config/logic.yaml`）与指标告警（Prometheus）。
- 核心原则：先观测、后收敛；先灰度、后全量；任何阶段可快速回滚。

---

## 2. 配置项说明

`config/logic.yaml` 中 `server` 下相关配置：

- `legacy_fallback_enabled`
  - 总开关。`false` 时所有 legacy dispatch fallback 禁用。
- `legacy_fallback_allow_auth_whitelist`
  - 是否允许认证白名单消息走 fallback（如登录/心跳/登出类）。
- `legacy_fallback_allow_critical_msgs`
  - 是否允许关键业务消息走 fallback（如移动/战斗/装备等 critical）。
- `legacy_fallback_allow_normal_msgs`
  - 是否允许 normal 消息走 fallback。

建议初始值（当前默认）：

```yaml
server:
  legacy_fallback_enabled: true
  legacy_fallback_allow_auth_whitelist: true
  legacy_fallback_allow_critical_msgs: true
  legacy_fallback_allow_normal_msgs: false
```

---

## 3. 分阶段灰度策略

## 阶段 0：基线观察（不改变行为）

- 配置：
  - 保持默认值（如上）。
- 观察窗口：
  - 至少 24 小时，覆盖高峰时段。
- 重点指标：
  - `logic_hot_event_queue_full_fallback_total`
  - `logic_legacy_dispatch_blocked_total`
  - `logic_thread_affinity_violation_total`
  - `logic_entity_version_mismatch_total`
- 放行条件：
  - 无 P0/P1 故障。
  - `logic_thread_affinity_violation_total` 无新增。
  - `logic_entity_version_mismatch_total` 无持续上升趋势。

## 阶段 1：禁用 normal fallback（通常已默认）

- 配置：

```yaml
server:
  legacy_fallback_enabled: true
  legacy_fallback_allow_auth_whitelist: true
  legacy_fallback_allow_critical_msgs: true
  legacy_fallback_allow_normal_msgs: false
```

- 灰度比例：
  - 5% -> 20% -> 50%，每档至少观察 2 小时。
- 重点风险：
  - normal 消息在过载时可能被阻断而非 fallback。
- 回滚条件：
  - `logic_legacy_dispatch_blocked_total` 速率激增并伴随业务失败投诉。

## 阶段 2：收紧 critical fallback

- 配置：

```yaml
server:
  legacy_fallback_enabled: true
  legacy_fallback_allow_auth_whitelist: true
  legacy_fallback_allow_critical_msgs: false
  legacy_fallback_allow_normal_msgs: false
```

- 灰度比例：
  - 1% -> 5% -> 20%，每档至少观察 4 小时。
- 前置条件：
  - 阶段 1 稳定至少 24 小时。
  - 队列满事件受控（无持续过载）。
- 回滚条件：
  - 登录成功率、关键操作成功率、在线人数留存出现明显下滑。

## 阶段 3：仅保留 auth 白名单 fallback（推荐长期形态）

- 配置：

```yaml
server:
  legacy_fallback_enabled: true
  legacy_fallback_allow_auth_whitelist: true
  legacy_fallback_allow_critical_msgs: false
  legacy_fallback_allow_normal_msgs: false
```

- 说明：
  - 与阶段 2 配置相同，但扩大到全量并持续观察。
  - 如果阶段 2 已全量，可视为阶段 3 验证期。

## 阶段 4：完全关闭 legacy fallback（最终目标）

- 配置：

```yaml
server:
  legacy_fallback_enabled: false
  legacy_fallback_allow_auth_whitelist: false
  legacy_fallback_allow_critical_msgs: false
  legacy_fallback_allow_normal_msgs: false
```

- 灰度比例：
  - 1% -> 5% -> 20% -> 50% -> 100%，建议跨日推进。
- 强制要求：
  - 必须有快速回滚预案（见第 6 节）。

---

## 4. 观测与告警基线

已配置告警（`config/prometheus/alerts.yaml`）：

- `LogicThreadAffinityViolationDetected`（critical）
- `EntityVersionMismatchHigh`（warning）
- `LegacyDispatchBlockedHigh`（warning）
- `QueueFullFallbackHigh`（warning）

建议同时关注业务 SLI：

- 登录成功率
- 关键请求成功率（移动/战斗/背包）
- 网关断开率、超时率
- p99 请求延迟

---

## 5. 发布操作步骤（每次灰度）

1. 变更目标环境的 `config/logic.yaml`。
2. 重启对应 `mir2_logic` 实例（或按当前部署方式滚动发布）。
3. 发布后 10 分钟内重点看：
   - 错误日志是否出现连续 `blocked legacy dispatch by policy`
   - 告警是否触发
4. 观察窗口达到后再扩大流量档位。
5. 每次扩大前记录对比：
   - 上一档指标快照
   - 当前档指标快照

---

## 6. 回滚预案

触发任一条件立即回滚：

- `LogicThreadAffinityViolationDetected` 触发且持续。
- 业务核心 SLI 明显劣化（登录成功率、关键操作成功率）。
- 玩家投诉集中爆发且可关联本次策略变更。

回滚方式（最快）：

```yaml
server:
  legacy_fallback_enabled: true
  legacy_fallback_allow_auth_whitelist: true
  legacy_fallback_allow_critical_msgs: true
  legacy_fallback_allow_normal_msgs: false
```

然后滚动重启实例，恢复默认保守策略。

---

## 7. 变更记录模板

- 变更时间：
- 变更人：
- 环境/分组：
- 灰度比例：
- 变更前配置：
- 变更后配置：
- 观察时长：
- 指标对比结论：
- 是否进入下一阶段：
- 回滚情况（如有）：
