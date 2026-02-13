# CoroutineExecutor 工程补充说明

最后更新: 2026-02-11

适用范围: `src/server/logic/coroutine_executor.h`, `src/server/logic/coroutine_executor.cc`, `src/server/logic/task.h`

## 1. 生命周期与关闭语义

`CoroutineExecutor` 的关闭流程是两阶段:

1. `StopAccepting()`:
   - 拒绝新任务 (`SpawnResult::kNotAccepting`)。
   - 仅改变接入状态，不阻塞等待。
2. `DrainAndJoin(timeout)`:
   - 停止接入后，先 `blocking_pool_.join()`。
   - 再等待 `running/suspended/pending_resume_callbacks` 归零。
   - 超时时增加 `logic.coroutine.drain_timeout_total` 指标。

析构函数会执行安全兜底:

1. `StopAccepting()`
2. `DrainAndJoin(5s)`
3. 停止饥饿 watchdog
4. 停止并清空复用 timer 池

## 2. 计数与可观测约束

执行器内核心计数器:

1. `running_count_`
2. `suspended_count_`
3. `pending_resume_callbacks_`
4. `timeout_background_inflight_`

这些计数均对应 gauge 指标，并在减计数时做下溢保护:

1. 计数不会跌破 0（CAS clamp）。
2. 下溢会累计 underflow counter。

## 3. 超时与取消语义

1. `AsyncWithTimeout` 超时后会向调用方抛 `TimeoutError`。
2. 底层 blocking 函数不会被强制中断（仅“调用方超时返回”）。
3. 为避免语义误判，提供:
   - `logic.coroutine.timeout_background_inflight`
   - `logic.coroutine.timeout_background_total`
4. `SleepFor(duration, stop_token)` 在 stop 请求下抛 `CancelledError`。

## 4. 高频 Timer 分配优化（P2-3）

`AsyncWithTimeout` 与 `SleepFor` 统一走复用池:

1. `AcquireReusableTimer()` 从 `ReusableTimerPool` 获取 timer。
2. 自定义 deleter 通过 `RecycleTimer()` 回收。
3. 池上限 `max_cached = 4096`，超出则释放。
4. executor 析构时 `recycle_enabled=false`，防止析构后回收写入。

## 5. Task 基础类型收敛（P2-1）

`Task<T>` 与 `Task<void>` 已合并为统一模板实现:

1. 抽象基类: `detail::TaskPromiseBase`
2. 值存储分层: `detail::TaskPromiseStorage<T>`
3. Promise 模板: `detail::TaskPromise<T>` + `TaskPromise<void>` 特化
4. 新增 `detail::Awaitable` concept 与 `AwaitAsTask(...)` 辅助

## 6. `noexcept` 审计结论（P2-5）

本轮审计策略:

1. 无条件失败也不能传播异常的路径，显式 `noexcept`。
2. 含内存分配/字符串构造/I/O 的路径，不强行标 `noexcept`。

已加固项:

1. 环境变量解析与阈值解析辅助函数 (`Parse*`, `Resolve*`)。
2. 停止感知 sleep 辅助 (`SleepWithStop`)。
3. 默认异常上报器 (`ReportTaskUnhandledExceptionDefault`, `ReportTaskDetachedExceptionDefault`)。
4. `Task` 内部默认异常上报分发 (`ReportTaskUnhandledException`, `ReportTaskDetachedException`)。

未标 `noexcept` 的典型原因:

1. 需要构造 `std::string`（可能分配）。
2. 可能抛出业务异常（如 `await_resume` 的超时/取消语义）。
3. 依赖外部库回调且无法给出无抛保证。

## 7. 压测基准入口（P3-2）

已提供三类基准:

1. `BM_CoroutineStorm_SpawnAndComplete`
2. `BM_TimeoutStorm_AsyncWithTimeout`
3. `BM_ShutdownStorm_DrainAndJoin`

编译与运行:

```bash
cmake --preset vcpkg-benchmark
cmake --build --preset vcpkg-benchmark --target coroutine_executor_stress_benchmark -j$(nproc)

./build/vcpkg-benchmark/bin/coroutine_executor_stress_benchmark \
  --benchmark_filter='BM_(CoroutineStorm|TimeoutStorm|ShutdownStorm).*' \
  --benchmark_min_time=0.2
```
