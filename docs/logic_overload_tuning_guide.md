# 逻辑服过载调参说明（Priority + Backpressure + Tick Budget）

最后更新：2026-02-10

## 1. 适用范围

本文针对 `config/logic.yaml` 中 `server:` 下已经支持的过载治理参数。

覆盖三个调参面：

1. Tick 频率与 HotEvent 单帧 drain 预算。
2. 玩家邮箱优先级调度与单玩家队列上限。
3. 全局积压阈值与背压参数。

当前架构约束：

1. `LogicServer` 仍会强制 `io_threads=1`（ECS 单线程安全约束），因此所有调参都应按“单逻辑主线程”来评估。

## 2. 参数与线上建议区间

| 配置项 | 默认值 | 线上建议区间 | 说明 |
| --- | --- | --- | --- |
| `tick_interval_ms` | `50` | `30-100` | 主 Tick 周期。越小延迟越低，但调度压力越大。 |
| `service_link_write_queue_size` | `8192` | `2048-16384` | Gateway->Logic 服务链路写队列上限，过小会导致服务链路瞬断。 |
| `network_session_idle_check_interval_ms` | `30000` | `5000-60000` | 网络层空闲检测扫描周期（毫秒）。 |
| `network_session_idle_timeout_ms` | `90000` | `30000-300000` | 网络层会话空闲超时（毫秒）。 |
| `hot_event_max_drain_per_tick` | `2048` | `1024-8192` | 每个 Tick 最多 drain 的 hot event 数。 |
| `hot_event_max_drain_ms_per_tick` | `5` | `2-12` | 建议约为 `tick_interval_ms` 的 `10%-25%`。 |
| `mailbox_player_max_high_pending` | `100` | `64-256` | 单玩家高优先级待处理上限。 |
| `mailbox_player_max_low_pending` | `64` | `16-128` | 单玩家低优先级待处理上限。 |
| `mailbox_high_priority_burst` | `4` | `2-8` | 高优先级连续处理预算（之后穿插低优先级）。 |
| `mailbox_overflow_kick_threshold` | `3` | `3-8` | 连续硬溢出达到阈值才 kick。 |
| `mailbox_global_pending_hard_limit` | `20000` | `max_connections * (2-6)` | 全局待处理 hard 上限。 |
| `mailbox_global_pending_soft_limit` | `-1` | `hard 的 70%-85%` 或 `-1` | `-1` 表示自动取 hard 的 75%。 |
| `backpressure_pause_ms` | `100` | `50-300` | hot queue full 丢弃低优先级时的 pause 时长。 |
| `backpressure_signal_cooldown_ms` | `100` | `20-200` | queue-full pause 下发冷却（按 client）。 |
| `mailbox_soft_backpressure_pause_ms` | `100` | `50-200` | 触发 soft 背压时的 pause 时长。 |
| `mailbox_hard_backpressure_pause_ms` | `300` | `150-1000` | 触发 hard 背压时的 pause 时长。 |
| `mailbox_soft_backpressure_cooldown_ms` | `100` | `50-250` | soft 背压下发冷却（按 client）。 |
| `mailbox_hard_backpressure_cooldown_ms` | `25` | `10-100` | hard 背压下发冷却（按 client）。 |

线上首发建议基线（可直接起步）：

```yaml
server:
  tick_interval_ms: 50
  hot_event_max_drain_per_tick: 2048
  hot_event_max_drain_ms_per_tick: 5

  mailbox_player_max_high_pending: 128
  mailbox_player_max_low_pending: 64
  mailbox_high_priority_burst: 4
  mailbox_overflow_kick_threshold: 5

  mailbox_global_pending_hard_limit: 25000
  mailbox_global_pending_soft_limit: -1

  backpressure_pause_ms: 100
  backpressure_signal_cooldown_ms: 80
  mailbox_soft_backpressure_pause_ms: 100
  mailbox_hard_backpressure_pause_ms: 350
  mailbox_soft_backpressure_cooldown_ms: 120
  mailbox_hard_backpressure_cooldown_ms: 30
```

## 3. 调参时必看指标

核心指标：

1. `logic.tick.duration_ms`
2. `logic.tick.overrun_total`
3. `logic.hot_event.drain_budget_hit_total`
4. `logic.mailbox.pending_events`
5. `logic.mailbox.active_runners`
6. `logic.mailbox.global_pending_utilization`
7. `logic.mailbox.overflow_total`
8. `logic.mailbox.global_overflow_total`
9. `logic.mailbox.soft_backpressure_total`
10. `logic.mailbox.hard_backpressure_total`
11. `logic.backpressure.queue_full_signal_total`
12. `logic.backpressure.pause_signal_total`
13. `logic.mailbox.spawn_rejected_total`
14. `logic.handler.latency_ms`

建议守门线：

1. `p95(logic.tick.duration_ms)` <= `0.6 * tick_interval_ms`
2. `p99(logic.tick.duration_ms)` <= `0.8 * tick_interval_ms`
3. 稳态阶段 `logic.tick.overrun_total` 不应持续线性上涨。
4. 稳态阶段 `logic.mailbox.global_pending_utilization` 建议在 `0.40-0.80`。
5. 正常峰值窗口内 `logic.mailbox.global_overflow_total` 与 kick 事件应接近 0。

## 4. 压测调参顺序（必须按顺序）

一次只调一组参数，按以下顺序执行。

### Step 0：固定压测场景

1. 固定地图/NPC/脚本版本与机器规格。
2. 固定流量配比（登录/移动/战斗/聊天）与目标并发。
3. 先跑 10-15 分钟 baseline，记录全部核心指标。

### Step 1：先调 Tick 预算

参数组：

1. `tick_interval_ms`
2. `hot_event_max_drain_per_tick`
3. `hot_event_max_drain_ms_per_tick`

顺序：

1. 先固定 `tick_interval_ms`。
2. 逐步上调 drain 预算，观察 `logic.hot_event.drain_budget_hit_total` 是否下降。
3. 一旦 `logic.tick.duration_ms` 的 p99 接近守门线，停止继续上调预算。
4. 最后再判断是否需要调整 `tick_interval_ms`。

### Step 2：再调单玩家邮箱公平性

参数组：

1. `mailbox_player_max_high_pending`
2. `mailbox_player_max_low_pending`
3. `mailbox_high_priority_burst`

顺序：

1. 先提升高优先级上限，保护关键链路。
2. 低优先级上限保持更紧，限制聊天等突发堆积。
3. 关键消息延迟仍抖动时，再提高 `burst`。
4. 出现低优先级明显饥饿时，适度回调 `burst`。

### Step 3：再调全局积压与背压

参数组：

1. `mailbox_global_pending_hard_limit`
2. `mailbox_global_pending_soft_limit`
3. `mailbox_soft_backpressure_pause_ms`
4. `mailbox_hard_backpressure_pause_ms`
5. `mailbox_soft_backpressure_cooldown_ms`
6. `mailbox_hard_backpressure_cooldown_ms`
7. `backpressure_pause_ms`
8. `backpressure_signal_cooldown_ms`

顺序：

1. 先按目标并发设置 `hard_limit`。
2. `soft_limit` 固定在 `hard` 的 `70%-85%`。
3. 优先通过 pause/cooldown 降低 overflow，再考虑 kick 阈值。
4. 下发过于频繁时，优先提高 cooldown。

### Step 4：最后再调 kick 策略

参数组：

1. `mailbox_overflow_kick_threshold`

顺序：

1. 仅在 Step 1-3 稳定后调整。
2. 短时尖峰导致误踢时，上调该阈值。
3. 仅在需要快速隔离异常客户端时才下调。

### Step 5：长稳态验证

1. 持续压测 30-60 分钟。
2. 至少做一次突发压测（2 倍消息突发，持续 30-60 秒）。
3. 确认无指标漂移，且 overrun 无持续上升趋势。

## 5. 快速定位对照

现象 -> 首选调整参数组：

1. `tick.overrun_total` 快速上涨 -> Step 1（降 drain 预算或增大 tick 间隔）。
2. `drain_budget_hit_total` 高但 tick 仍健康 -> Step 1（提高 drain 预算）。
3. `global_pending_utilization` 接近 `1.0` -> Step 3（提高 hard limit 或增强背压）。
4. utilization 正常但 overflow 频繁 -> Step 2（调单玩家上限与 burst）。
5. 尖峰期间踢人过多 -> Step 3 后再 Step 4。

## 6. 线上安全规则

1. 单个发布窗口内不要跨 Step 组同时改参数。
2. 每次调参前保存上一版 `logic.yaml`。
3. 除非有明确理由，优先使用 `mailbox_global_pending_soft_limit: -1`。
4. 任何降低 `tick_interval_ms` 的改动，必须重跑完整突发压测。
5. 若 `logic.mailbox.spawn_rejected_total` 异常上升，应先排查执行器压力，再继续调参。

## 7. 硬瓶颈判定（Stage 4）

该章节用于回答一个明确问题：当前吞吐上限是否由 Logic 主线程硬性限制。

### 7.1 工作负载与爬坡

1. 控制组 `W0-Control`：`100% Heartbeat`。
2. 业务组 `W1-MixedGameplay`：`50% MoveReq + 20% AttackReq + 10% SkillReq + 10% ChatReq + 10% Heartbeat`。
3. 业务组 `W2-WriteHeavy`：`60% MoveReq + 30% AttackReq + 10% SkillReq`。
4. 单步窗口默认：`Warmup 60s + Sample 180s + Cooldown 30s`，每步重复 `3` 轮取中位数。
5. 控制组粗爬坡默认步点：`[2000, 4000, 6000, 8000, 10000, 12000, 14000, 16000] msg/s`。
6. 控制组可持续上限 `C_ctrl` 为最高健康步；业务组步点为 `[0.50, 0.65, 0.80, 0.95, 1.10, 1.25] * C_ctrl`。

### 7.2 判定指标与计算

每秒采样并在 Sample 窗口计算：

1. `offered_qps`：压测器目标发送速率（实发计数/秒）。
2. `effective_qps`：优先取 `Δlogic_routed_processed_total / sample_seconds`；若缺失则依次回退到 `logic_hot_event_drain_total`、`logic_hot_event_enqueue_total`。
3. `elasticity_i = (effective_qps_i - effective_qps_{i-1}) / (offered_qps_i - offered_qps_{i-1})`。
4. `overrun_rate = Δlogic_tick_overrun_total / tick_count_window`。
5. `drain_budget_hit_rate = Δlogic_hot_event_drain_budget_hit_total / tick_count_window`。
6. `queue_slope = (queue_depth_end - queue_depth_start) / sample_seconds`。

健康线（默认 `tick_interval_ms=50`，改 Tick 间隔时按比例换算）：

1. `p99(logic_tick_duration_ms) < 0.8 * tick_interval_ms`
2. `overrun_rate < 0.05`
3. `mailbox_util_avg < 0.70`
4. `queue_slope <= 0`

### 7.3 三态结论

1. `PROVED`（全部满足）：
   `2` 个连续负载步 `elasticity < 0.10`；并在同窗口满足：
   `p99(logic_tick_duration_ms) >= tick_interval_ms` 或 `overrun_rate >= 0.20`；
   `drain_budget_hit_rate >= 0.30`；
   `mailbox_util_avg >= 0.90` 且 `queue_slope > 0`；
   `global_overflow_rate >= 1/s` 或 `hard_backpressure_rate >= 5/s`；
   且同负载点控制组仍健康并满足 `C_ctrl / C_game >= 1.8`。
2. `FALSIFIED`（任一满足）：
   最高步仍 `elasticity >= 0.30` 且健康线全通过；
   或业务组与控制组上限差异 `< 15%` 且退化模式一致；
   或业务组退化时逻辑指标仍健康（`p99 < 0.8*tick`、`overrun_rate < 0.05`、`mailbox_util < 0.70`）。
3. `INCONCLUSIVE`：
   不满足 `PROVED`，也不满足 `FALSIFIED`。
   另外，若 Prometheus 不可用/抓取失败，或样本窗口内出现 Gateway-Logic 服务链路断开，必须降级为 `INCONCLUSIVE`，禁止给出强结论。

### 7.4 报告与发布门禁

1. 压测实现位于 `GatewayLogicPressureTest.*`，输出：
   `docs/STAGE4-LOGIC-BOTTLENECK-REPORT.md` 与
   `docs/STAGE4-LOGIC-BOTTLENECK-REPORT.csv`。
2. 报告必须包含每步 `offered/effective/tick/backlog/backpressure` 和最终唯一结论。
3. 该流程列为发布前必跑压测之一；任何涉及主线程调度、邮箱背压、HotEvent drain 预算的改动，发布前必须重跑。
