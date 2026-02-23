# Legend2 逻辑层协程可靠性与过载防护实施方案（导出文档版）

## 摘要
本方案覆盖 5 项任务：`1 协程超时与自动取消`、`2 协程沙盒与错误隔离`、`5 ValidateCacheVersion 辅助`、`6 全局流量控制`、`7 监控与告警完善`。  
采用“两阶段落地”，策略偏“保护系统优先”：先保障逻辑线程和资源可控，再补自动化验证与治理闭环。

建议文档文件名：`docs/logic_coroutine_hardening_plan.md`

---

## 范围与目标

1. 防止单玩家异常协程影响整个逻辑服务可用性。
2. 为协程执行建立统一超时、取消、隔离和观测机制。
3. 在逻辑层增加全局消息吞吐上限，防止瞬时洪峰拖垮系统。
4. 完成可视化与告警，支持自动降级与运维处置。
5. 降低 `co_await` 后遗漏 `ValidateCacheVersion` 的人为风险。

非目标：
1. 不做协议字段变更（客户端协议保持兼容）。
2. 不引入多线程 ECS（维持逻辑线程单线程模型）。
3. 不在本阶段做大规模 handler 语义改写。

---

## 阶段划分

## Phase 1（稳定性优先，建议 1-2 周）
1. 任务2：协程沙盒与错误隔离（核心）。
2. 任务6：全局流量控制（核心）。
3. 任务7（部分）：先补关键指标、告警、基础面板。
4. 任务1（部分）：先给 `Spawn` 加任务级超时/取消。

## Phase 2（完善与收敛，建议 1 周）
1. 任务1：`EntityLane::Enter` 超时与自动取消补齐。
2. 任务5：`ValidatedContext` 自动重验机制落地。
3. 任务7：自动降级策略与运维流程闭环。
4. 任务5（可选增强）：clang-tidy 规则化检查。

---

## 设计总览

1. 在 `CoroutineExecutor` 引入统一 `SpawnOptions`，支持任务级超时、取消策略、隔离策略。
2. 在 `EntityLaneScheduler` 引入 `EnterOptions`，支持 lane 等待超时与看门狗计数。
3. 在 `LogicServer::RunPlayerMailbox` 引入“单事件沙盒 + runner 自愈清理”，确保异常不会卡死 mailbox 执行状态。
4. 在 `HotEventPipeline/Dispatch` 路径引入全局令牌桶，按优先级分级丢弃。
5. 在 `HandlerContext` 之上新增 `ValidatedContext`，对 `co_await` 后访问提供显式“再验证+刷新缓存”入口。
6. 新增 Logic 仪表盘与告警规则，联动自动降级动作（暂停非核心系统、提升丢弃等级、最终踢线）。

---

## 公开接口与类型变更（决策完成）

## 1) `CoroutineExecutor` 新接口

新增类型：
```cpp
struct SpawnOptions {
  std::chrono::milliseconds timeout{0};      // 0=禁用
  bool auto_cancel_on_timeout{true};         // 超时后请求停止
  bool detach_on_timeout{false};             // true=超时后让底层继续后台执行并计数
  bool isolate_exceptions{true};             // true=异常吞吐并上报，不向上游扩散
  std::string timeout_reason{"spawn_timeout"};
};
```

新增重载：
```cpp
template <typename T>
SpawnResult TrySpawn(Task<T> task, CoroutineMetadata metadata, SpawnOptions options);

template <typename T>
SpawnResult SpawnOrDrop(Task<T> task, ErrorCallback&& on_error,
                        CoroutineMetadata metadata, SpawnOptions options);
```

默认策略：
1. `timeout=0`（不改变旧行为）。
2. 对 mailbox/prewarm/login 等入口显式传入 `timeout`。
3. `isolate_exceptions=true`，避免入口协程把逻辑线程拖下线。

兼容性：
1. 旧重载保留，内部转调新实现（`SpawnOptions{}`）。

## 2) `EntityLaneScheduler` 新接口

新增类型：
```cpp
struct EnterOptions {
  std::chrono::milliseconds wait_timeout{0};   // 0=无限等待
  uint64_t client_id{0};                       // 用于看门狗统计
  bool watchdog_track{false};
};
```

新增 awaiter：
```cpp
EnterAwaiter Enter(uint64_t lane_key, EnterOptions options);
```

行为定义：
1. 超时时 `await_resume()` 抛 `TimeoutError`（由调用方沙盒捕获）。
2. 触发超时时增加 `logic.entity_lane.enter_timeout_total`。
3. `watchdog_track=true` 时记录该玩家 lane 超时/连续卡顿计数。

## 3) `HandlerContext` 辅助类型

新增：
```cpp
class ValidatedContext {
 public:
  explicit ValidatedContext(HandlerContext* ctx, RegistryManager* registry_manager);
  bool Revalidate();                   // co_await 后调用，校验+刷新 registry/world/map_id
  HandlerContext& Get();               // 安全访问
};
```

规则：
1. handler 内任意 `co_await` 后必须调用 `Revalidate()` 后再写 ECS。
2. 若失败：返回业务错误并终止当前消息处理。
3. Phase 2 可选接入 clang-tidy 检查（不阻塞当前主线）。

## 4) `LogicServer` 新配置项（`config_manager`）

在 `ServerConfig` 增加：
1. `coroutine_spawn_timeout_ms`（默认 `3000`）。
2. `entity_lane_enter_timeout_ms`（默认 `1000`）。
3. `entity_lane_watchdog_stuck_threshold`（默认 `3`）。
4. `entity_lane_watchdog_window_ms`（默认 `10000`）。
5. `global_msg_rate_limit_per_sec`（默认 `8000`）。
6. `global_msg_rate_burst`（默认 `16000`）。
7. `degrade_level1_queue_depth`（默认 `0.7 * hard_limit`）。
8. `degrade_level2_queue_depth`（默认 `0.9 * hard_limit`）。
9. `degrade_recover_hysteresis_ms`（默认 `5000`）。

---

## 详细任务方案

## 任务2：协程沙盒与错误隔离（最高优先级）

实现点：
1. `RunPlayerMailbox` 内对每个 `ExecuteQueuedEvent` 包裹 `try/catch`，异常只影响当前消息，不终止 runner。
2. 增加 `MailboxRunnerGuard`（RAII）保证任何路径都会清理 `executing`、`mailbox_active_runners_`。
3. `ExecuteQueuedEvent` 分支内部统一捕获 `TimeoutError/CancelledError/std::exception/...`，分别计数。
4. 玩家级错误计数器：滑窗内连续超时/异常超过阈值，触发踢线释放资源。
5. 看门狗动作顺序：告警 -> 限流 -> 踢线。

新增指标：
1. `logic.mailbox.event_exception_total`
2. `logic.mailbox.event_timeout_total`
3. `logic.mailbox.runner_recovered_total`
4. `logic.player.watchdog_trigger_total`
5. `logic.player.watchdog_kick_total`

---

## 任务6：全局流量控制（高优先级）

算法：
1. 单进程全局令牌桶（逻辑线程内无锁实现）。
2. 令牌按 tick 补充，支持 `rate + burst`。
3. 按优先级消耗：
   - `critical`：尽量通过，仅在极限过载时丢弃。
   - `normal`：超限时优先丢弃。
   - `best-effort`：超限立即丢弃。
4. 丢弃后给网关回压信号（已有 backpressure 通道复用）。

接入点：
1. `HotEventPipeline::TryEnqueue` 前置粗粒度限流（防入队爆炸）。
2. `DispatchHotEventsBatch` 二次限流（防处理阶段过载）。
3. 与 mailbox soft/hard limit 协同，形成“两层防护”。

新增指标：
1. `logic.rate_limiter.tokens`
2. `logic.rate_limiter.drop_total`
3. `logic.rate_limiter.drop_critical_total`
4. `logic.rate_limiter.drop_normal_total`
5. `logic.rate_limiter.drop_best_effort_total`
6. `logic.rate_limiter.pass_total`

---

## 任务1：协程超时与自动取消

实现点：
1. `SpawnOptions.timeout` 统一控制任务生命周期超时。
2. 超时后执行 `RequestStop()`，并将结果映射为 `TimeoutError`。
3. 对不可强杀后台任务保留现有语义：记录 `timeout_background_inflight`。
4. `EntityLane::Enter` 支持等待超时，避免无限排队。
5. 超时日志统一字段：`client_id/msg_id/trace_id/coroutine_id/handler_key/lane_key/elapsed_ms`。

新增指标：
1. `logic.coroutine.spawn_timeout_total`
2. `logic.coroutine.auto_cancel_total`
3. `logic.entity_lane.enter_timeout_total`
4. `logic.entity_lane.watchdog_consecutive_stuck`

---

## 任务5：ValidateCacheVersion 辅助

实现点：
1. 引入 `ValidatedContext`，将“校验+刷新缓存”封装为一个动作。
2. 在高风险 handler 优先替换：`movement/attack/skill/item/chat/guild/npc`。
3. 标准模式：
   - `co_await ...`
   - `if (!vctx.Revalidate()) { return error; }`
   - 再访问 ECS。
4. 可选增强：新增 clang-tidy 规则 `legend2-await-revalidate`（Phase 2 后半）。

新增指标：
1. `logic.context.revalidate_total`
2. `logic.context.revalidate_failed_total`
3. `logic.context.revalidate_refresh_total`

---

## 任务7：监控、告警与自动降级

交付物：
1. 新 Grafana：`logic-server-dashboard.json`。
2. 新 Prometheus 规则：`logic_alerts.yml`。
3. 运维手册：故障分级、降级开关、恢复条件。

面板最小集合：
1. 协程：active/running/suspended/hung/starving/timeout。
2. EntityLane：active/pending/enter_timeout/watchdog。
3. Mailbox：pending/active_runner/overflow/spawn_rejected。
4. HotEvent：queue_depth/enqueue_drop/drain_budget_hit。
5. RateLimiter：token/drop/pass。
6. Tick：duration/overrun。
7. 降级状态：当前等级、触发原因、持续时长。

告警（初始阈值）：
1. `tick_overrun` 连续 2 分钟。
2. `mailbox_pending_utilization > 0.85` 持续 1 分钟。
3. `entity_lane_pending` 持续升高 1 分钟。
4. `spawn_timeout_total` 5 分钟内突增。
5. `watchdog_kick_total` 5 分钟超过阈值。

自动降级策略：
1. Level 1：暂停非核心系统（如部分 AI/掉落更新频率降低）。
2. Level 2：提高限流强度，强制丢弃 normal+best-effort。
3. Level 3：对异常玩家快速踢线，保护整体可用性。
4. 恢复：满足回落阈值并持续 `degrade_recover_hysteresis_ms`。

---

## 测试方案与验收标准

## 单元测试
1. `CoroutineExecutor`：
   - spawn 超时触发取消。
   - timeout 与 worker 完成竞态一致性。
   - isolate_exceptions 不向上层扩散。
2. `EntityLaneScheduler`：
   - enter 超时。
   - 正常排队公平性。
   - watch dog 计数窗口逻辑。
3. `GlobalRateLimiter`：
   - refill/burst 精度。
   - 分优先级丢弃策略。
4. `ValidatedContext`：
   - await 后 map 切换重验失败。
   - 刷新缓存成功后可继续执行。

## 集成测试
1. 单玩家协程故障不影响其他玩家消息处理。
2. 人工注入慢任务，系统进入降级并自动恢复。
3. 洪峰压测下 critical 消息通过率满足目标。
4. mailbox runner 异常后可自愈重启，不出现永久 `executing=true` 卡死。

## 验收标准（量化）
1. 逻辑线程因协程异常退出：`0`。
2. 过载 10 分钟压测期间服务存活率：`100%`。
3. critical 消息成功率：`>= 99.9%`。
4. 过载恢复后 2 分钟内关键指标回归正常区间。
5. 新增指标全部可在 Prometheus 采集并在 Grafana 展示。

---

## 里程碑与工期（建议）

1. M1（2-3 天）：任务2 核心隔离 + mailbox 自愈 + 基础指标。
2. M2（2-3 天）：任务6 全局令牌桶 + 分级丢弃 + 回压联动。
3. M3（2 天）：任务1 spawn/lane 超时 + 自动取消 + 统一日志。
4. M4（2 天）：任务5 `ValidatedContext` 与关键 handler 接入。
5. M5（2 天）：任务7 仪表盘/告警/自动降级策略与文档收尾。

---

## 风险与缓解

1. 风险：超时取消导致业务中间态不一致。  
   缓解：仅在 await 边界生效；关键写操作前强制 `Revalidate()`。
2. 风险：限流过严影响玩家体验。  
   缓解：优先丢 best-effort；critical 白名单兜底；阈值可热调。
3. 风险：看门狗误踢。  
   缓解：滑窗+连续阈值+冷却期，先告警后动作。
4. 风险：监控噪声过高。  
   缓解：告警加持续时间和抖动抑制。

---

## 默认假设与已选定策略

1. 交付格式：Markdown 文档。
2. 推进方式：两阶段落地。
3. 过载策略：保护系统优先。
4. 不修改客户端协议。
5. 保持逻辑线程单线程架构。
6. clang-tidy 规则作为 Phase 2 可选增强，不阻塞主交付。
