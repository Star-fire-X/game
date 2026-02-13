# `thread_local` 协程兼容性审计

最后更新: 2026-02-11

范围: `src/server/logic/*`, `src/server/log/*`, `src/server/logic/events/*`, 以及服务端 `thread_local` 使用点。

目标: 识别“协程可跨线程恢复”场景下的 `thread_local` 风险，并给出可执行整改清单。

## 1. 审计结论摘要

1. 当前 `CoroutineExecutor` 的协程恢复点统一在 `io_context` 线程，主路径可控。
2. 高风险点不是“协程帧本身”，而是 `Async` blocking 线程中对 `thread_local` 上下文的一致性。
3. 现存 `thread_local` 使用中，随机数生成器类大多为低风险；上下文绑定类为中风险。

## 2. 使用点分级

### A. 中风险（与协程/上下文传播直接相关）

1. `src/server/logic/coroutine_executor.h`
   - `CurrentRuntimeSlot()` 使用 `thread_local std::shared_ptr<CoroutineRuntimeState>`。
   - 风险: 若未来允许跨执行器线程恢复，需要明确上下文迁移策略。
2. `src/server/log/logger.cc`
   - `thread_local TraceLogContext g_trace_context`。
   - 风险: blocking 线程中的日志默认不会继承 io 线程 trace 上下文。
3. `src/server/logic/events/event_arena.cc`
   - `thread_local ThreadBinding g_thread_binding`。
   - 风险: 依赖“线程 -> producer_id”绑定；若调用线程模型变化，可能出现 producer 分配扩张或复用不符合预期。

### B. 低风险（统计/随机用途，线程隔离本身合理）

1. `src/server/ecs/systems/*.cc` 多处 `thread_local rng`。
2. `src/server/game/*` 和 `src/server/network/kcp_session.cc` 的 `thread_local rng`。

这些点不承载协程生命周期状态，且线程本地随机源符合设计目的。

## 3. 已确认安全边界

1. `ResumeHandle(...)` 在恢复协程前后维护运行态与 trace 作用域。
2. 关闭路径有 `lifetime_state->RequestStop()`，回调在 stop 后不会继续触达执行器状态更新入口。
3. 计数器与 watchdog 不依赖 `thread_local` 存储状态，主要依赖原子与受保护容器。

## 4. 整改建议清单（按优先级）

1. P0: 约束不变式
   - 在文档和代码注释中明确: 协程恢复必须通过 `ResumeHandle()`，禁止绕过直接 `resume()`。
2. P1: trace 传播增强
   - 为 `Async` blocking 工作线程补充 trace/coroutine 上下文注入方案（wrapper 或 scoped context）。
3. P1: EventArena 线程模型防漂移
   - 在关键入口增加诊断日志/指标，观察 `producer_id` 使用密度和增长。
4. P2: `thread_local` 新增使用准入
   - 新增代码若在协程路径使用 `thread_local`，需同时提供“跨线程恢复语义说明”。

## 5. 回归检查建议

1. 压测期间检查日志 trace_id 连续性（io 线程 + blocking 线程）。
2. 在高压 soak 下观察 EventArena producer 使用分布是否异常扩散。
3. 与 `CoroutineExecutor` 的 hung/starvation 检测一起复验，避免上下文漂移导致误报漏报。
