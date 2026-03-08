# 逻辑服过载治理改造方案（协程调度 / Tick / 邮箱容量 / 消息优先级）

## 1. 摘要

目标是把当前的「单线程逻辑循环 + 固定阈值邮箱 + 队列满时粗粒度回退」升级为「可配置 Tick + 分级优先队列 + 分层背压 + 可观测调度限流」。

改造后优先保证登录、战斗、移动等关键链路，降低聊天等低优先级消息对主路径的干扰，并避免邮箱在短时突发下轻易踢人。

## 2. 成功标准

1. 关键消息（登录/移动/攻击/技能）在压力下仍可处理，`queue_full` 时不出现大面积关键丢包。
2. `logic.mailbox.overflow_total` 明显下降，`KickMailboxOverflow` 触发率降到极低（仅持续异常客户端）。
3. 同等压测下 P99 handler latency 下降或持平，且抖动更小。
4. Tick 延迟可观测且可配置，不再硬编码 50ms。
5. 调度拒绝（`spawn_rejected_over_limit`）可按任务来源归因。

## 3. 现状问题

1. 逻辑服 Tick 频率硬编码 50ms，配置 `tick_interval_ms` 在逻辑主循环未生效。
2. Tick 内对 hot queue 全量 drain，存在单帧长尾风险。
3. 玩家邮箱为单队列 + 固定上限（每玩家 100），溢出策略偏激进（直接 kick）。
4. 消息优先级仅在 `queue_full` 时做粗粒度区分，缺少队列级优先级调度。
5. 协程调度虽有全局上限，但缺少按来源的限流与观测，拥塞定位粒度不足。

## 4. 改造范围与非目标

### 4.1 范围

1. 逻辑服 Tick 调度链路（`LogicServer`）。
2. HotEvent 入队与邮箱调度链路（`HotEventPipeline`、`DispatchHotEventsBatch`、`RunPlayerMailbox`）。
3. 协程执行器外围限流与观测（`CoroutineExecutor` 调用点）。
4. 配置项、指标、测试补齐。

### 4.2 非目标

1. 不改 ECS 线程模型（仍保持逻辑主线程串行）。
2. 不引入跨服级流量治理。
3. 不重写网络协议，仅在现有消息语义上做优先级与背压治理。

## 5. 详细方案

### 5.1 Tick 改造（配置化 + 预算化）

#### 5.1.1 配置生效修正

1. `LogicServer` 新增成员 `tick_interval_`（`std::chrono::milliseconds`）。
2. 初始化阶段读取 `ConfigManager::GetServerConfig().tick_interval_ms`，并做 clamp（建议区间 20~200ms）。
3. 删除逻辑服硬编码 `kTickInterval`，统一使用 `tick_interval_` 调度下一帧。

#### 5.1.2 Tick 内预算 drain

1. 新增配置：
   - `hot_event_max_drain_per_tick`（默认 2048）
   - `hot_event_max_drain_ms_per_tick`（默认 5）
2. `Tick()` 中 hot queue drain 改为双阈值停止：
   - 达到最大条数，停止 drain；
   - 达到最大耗时，停止 drain；
3. 未处理事件留待下一帧，防止 ECS/world update 被饿死。

#### 5.1.3 Tick 指标

新增指标：

1. `logic.tick.interval_ms.configured`
2. `logic.tick.duration_ms`
3. `logic.tick.overrun_total`
4. `logic.hot_event.drain_budget_hit_total`

### 5.2 消息优先级体系（HotEvent + Mailbox）

#### 5.2.1 增加优先级类型

在 `hot_event.h` 增加：

1. `enum class HotPriority : uint8_t { kCritical, kNormal, kBestEffort };`
2. `HotEvent` 增加优先级字段（或复用 `flags` 位编码）。
3. 保持 `HotEvent` 64 字节布局约束不变。

默认映射：

1. `kCritical`：登录、角色选择、移动、攻击、技能、关键背包/交易确认。
2. `kNormal`：常规 gameplay generic。
3. `kBestEffort`：聊天、心跳、非关键广播。

#### 5.2.2 HotEventPipeline 入队策略升级

1. `TryEnqueue` 内部根据 `msg_id` 计算优先级并写入 `HotEvent`。
2. `queue_full` 时策略：
   - `kCritical`：优先 legacy fallback。
   - `kNormal`：可 fallback（受限速保护）。
   - `kBestEffort`：直接丢弃并发送背压。

#### 5.2.3 玩家邮箱改为双队列

`PlayerMailbox` 从单 `deque` 改为：

1. `high_q`（Critical + Normal）
2. `low_q`（BestEffort）

执行策略（`RunPlayerMailbox`）：

1. 加权轮询，默认 `high:low = 4:1`。
2. 高优先优先处理，低优先防饿死。

#### 5.2.4 容量策略升级

新增配置（默认值）：

1. `mailbox_player_high_hard_limit = 256`
2. `mailbox_player_low_hard_limit = 128`
3. `mailbox_player_total_soft_limit = 256`
4. `mailbox_global_soft_limit = 15000`
5. `mailbox_global_hard_limit = 20000`
6. `mailbox_overflow_kick_threshold = 3`

触发顺序：

1. 优先丢当前玩家低优先队列。
2. 发送 `BackpressurePause`。
3. 仅在连续硬溢出达到阈值后执行 `KickMailboxOverflow`。

### 5.3 协程调度治理（来源限流 + 指标归因）

#### 5.3.1 来源分组限流

在 `CoroutineExecutor` 调用点引入任务来源标签：

1. `mailbox_runner`
2. `legacy_dispatch`
3. `prewarm`
4. `internal_async`

初版软配额建议：

1. `mailbox_runner <= 4096`
2. `legacy_dispatch <= 4096`
3. `prewarm <= 512`
4. 其余来源共享剩余配额。

#### 5.3.2 恢复回调压力保护

1. 当 `pending_resume_callbacks` 高于阈值（如 5000）时，优先抑制非关键来源 spawn。
2. 当 `timeout_background_inflight` 高于阈值时，限制新发起的非关键超时包装任务。

#### 5.3.3 新增指标

1. `logic.coroutine.spawn_rejected_total{source=*}`
2. `logic.coroutine.running_count_by_source{source=*}`
3. `logic.coroutine.pending_resume_high_watermark`

## 6. 接口与类型变更清单

### 6.1 `ServerConfig` 新增字段

文件：`src/server/config/config_manager.h`、`src/server/config/config_manager.cc`

1. `hot_event_max_drain_per_tick`
2. `hot_event_max_drain_ms_per_tick`
3. `mailbox_player_high_hard_limit`
4. `mailbox_player_low_hard_limit`
5. `mailbox_player_total_soft_limit`
6. `mailbox_overflow_kick_threshold`

### 6.2 `HotEvent` 类型扩展

文件：`src/server/logic/events/hot_event.h`

1. 新增优先级表达（独立字段或 `flags` 位段）。
2. 保持 `sizeof(HotEvent) == 64` 的静态断言成立。

### 6.3 `PlayerMailbox` 数据结构更新

文件：`src/server/logic/logic_server.h`

1. 单队列改双队列（`high_q` / `low_q`）。
2. 新增连续溢出计数器、可选降级状态。

### 6.4 调度入口行为更新

文件：`src/server/logic/logic_server.cc`、`src/server/logic/handler_registry.cc`

1. `DispatchHotEvent` / `DispatchHotEventsBatch` 按优先级入不同队列。
2. `RunPlayerMailbox` 按权重调度双队列。
3. `HandleMailboxSpawnRejected` 记录来源标签与丢弃归因。

## 7. 测试计划

### 7.1 逻辑单测（扩展现有 mailbox 测试）

文件：`tests/server/logic/player_mailbox_causal_test.cc`

新增场景：

1. 高低优先混入时，高优先延迟不被低优先拖垮。
2. 低优先溢出优先丢弃，不立即 kick。
3. 连续硬溢出达到阈值才 kick。
4. 全局软上限触发 pause，恢复后继续处理。

### 7.2 Pipeline 单测

文件：`tests/server/logic/hot_event_pipeline_test.cc`

新增场景：

1. `msg_id -> priority` 映射正确。
2. `queue_full` 时 `critical` fallback，`best-effort` drop。

### 7.3 Tick 预算回归

新增场景：

1. 单帧 drain 不超过条数预算。
2. 单帧 drain 不超过耗时预算。
3. drain 命中预算时指标计数正确。

### 7.4 压测验收（非单测）

1. 聊天洪峰 + 少量战斗，验证关键链路延迟。
2. 大量 reconnect + prewarm，验证 spawn 拒绝不扩散至核心链路。
3. 持续高压 10~30 分钟，观察 overflow、kick、queue depth、tick overrun。

## 8. 发布与回滚

### 8.1 分阶段开关

保留：`LEGEND2_HOT_EVENT_PIPELINE`

新增：

1. `LEGEND2_MAILBOX_PRIORITY=0/1`
2. `LEGEND2_TICK_BUDGET=0/1`
3. `LEGEND2_MAILBOX_GRACEFUL_OVERFLOW=0/1`

发布顺序：

1. 仅开启观测和 Tick 配置化。
2. 开启 priority + dual mailbox，保持旧 kick 逻辑。
3. 开启 graceful overflow。
4. 开启来源限流。

### 8.2 回滚策略

任一阶段若关键指标恶化，先关闭对应 feature flag，无需回退二进制。

## 9. 默认假设

1. 优先目标是关键链路稳定性，而非绝对公平吞吐。
2. 维持单线程逻辑模型，不做多线程 ECS 改造。
3. 默认 Tick 仍以 50ms 为起点，但必须配置可控且真实生效。
4. 保留 legacy fallback 作为兜底，但纳入可观测和限流。
5. 初版优先采用双队列 + 权重，避免一次性引入复杂多级调度器。
