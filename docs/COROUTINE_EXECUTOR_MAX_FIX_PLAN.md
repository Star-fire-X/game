# Coroutine Executor 可执行迭代计划

最后更新: 2026-02-11

范围: `CoroutineExecutor` / `Task` / `LogicServer` / `HandlerRegistry` / `PrewarmManager`

目标: 将“最大修复清单”拆解为可执行的两期迭代，优先完成阻断上线风险，再收敛稳定性和可维护性问题。

---

## Sprint-1（P0，阻断上线）

### Sprint 目标

1. 消除关停生命周期 UAF 风险。
2. 落地统一过载投递与拒绝降级机制。
3. 修复原子计数竞态并补齐回归测试。

### 交付门禁（全部满足才可关闭 Sprint）

1. ASAN 下反复启停无 UAF/崩溃。
2. 过载场景下协程拒绝可观测、业务不悬挂。
3. `legend2_tests` 中新增/相关逻辑测试稳定通过。

### 工作包与执行顺序

#### WP1 关停协议与生命周期（P0-1, P0-2, P0-3）

任务:
1. 调整 `LogicServer::Shutdown()` 顺序，保证 executor drain 前 `io_context` 不被销毁。
2. 在执行器增加两阶段关闭：`StopAccepting()`、`DrainAndJoin(timeout)`。
3. 明确 `work_guard` 释放时机，确保回调执行完再退出 `io_context`。

涉及文件:
`src/server/logic/logic_server.cc`, `src/server/logic/coroutine_executor.h`, `src/server/logic/coroutine_executor.cc`, `src/server/core/application.cc`

验收:
1. 无“回调未执行完 io_context 已退出”场景。
2. 关闭期间新增任务被拒绝且有指标。

#### WP2 投递接口统一化与并发上限（P0-5, P0-6）

任务:
1. 增加 `SpawnResult`。
2. 增加 `SpawnOrDrop(Task, ErrorCallback)`，业务层避免散落 `if (!TrySpawn)`。
3. 增加全局协程上限策略（内部可保留 `TrySpawn`）。

涉及文件:
`src/server/logic/coroutine_executor.h`, `src/server/logic/coroutine_executor.cc`, `src/server/logic/handler_registry.cc`, `src/server/logic/logic_server.cc`, `src/server/logic/services/storage_login_service.cc`, `src/server/logic/prewarm_manager.cc`

验收:
1. 调用点可统一处理拒绝原因。
2. 压测下协程数量受控，无异常内存飙升。

#### WP3 业务降级与过载可观测（P0-7, P0-8）

任务:
1. 邮箱/登录/预热的拒绝路径补齐状态回收、回包或计数。
2. 增加过载指标：拒绝总数、当前排队、拒绝原因。

涉及文件:
`src/server/logic/logic_server.cc`, `src/server/logic/services/storage_login_service.cc`, `src/server/logic/prewarm_manager.cc`, `src/server/monitor/metrics.h`, `src/server/logic/coroutine_executor.cc`

验收:
1. 不出现 mailbox 卡死与请求悬挂。
2. Prometheus 可直接观察背压效果。

#### WP4 原子计数修复（P0-4）

任务:
1. 移除 `store(0)` 夹断逻辑。
2. 修复 `DecrementSuspended/DecrementRunning` 的 TOCTOU 覆盖写。

涉及文件:
`src/server/logic/coroutine_executor.cc`

验收:
1. 并发压测下计数不负值、不跳变归零。

#### WP5 回归测试与发布检查（P0-9）

任务:
1. 增加启停竞态回归测试。
2. 增加超时后后台任务完成语义测试。
3. 增加协程上限拒绝与计数正确性测试。

涉及文件:
`tests/server/logic/coroutine_executor_test.cc`, `tests/server/logic/`

验收:
1. 测试稳定通过，支持持续复跑。

### Sprint-1 风险与应对

1. 风险: shutdown 顺序改动引入新死锁。
应对: 先引入超时版 drain，再加死锁 watchdog 日志。
2. 风险: 过载拒绝导致业务行为变化。
应对: 用 `SpawnResult` 分类统计，先灰度启用阈值。

---

## Sprint-2（P1，稳定性与可维护性）

### Sprint 目标

1. 消除协程基础语义风险（尤其 `SleepAwaiter`）。
2. 以最小成本引入取消能力（先覆盖高收益等待点）。
3. 补齐异常可观测与关键路径性能改进。

### 交付门禁（全部满足才可关闭 Sprint）

1. `SleepAwaiter` 无 inline resume 风险。
2. `AsyncWithTimeout`/`SleepFor` 具备首阶段取消收益。
3. 未捕获协程异常可追踪（日志+指标）。

### 工作包与执行顺序

#### WP1 协程语义安全（P1-13, P1-14）

任务:
1. `[P1-High]` 修复 `SleepAwaiter::await_suspend`，去掉 inline `resume`，统一对称转移或 `post`。
2. `await_resume()` 的 `void` 分支改为显式返回。

涉及文件:
`src/server/logic/coroutine_executor.h`

验收:
1. 深层 await 场景无栈增长风险。
2. 编译告警为零。

#### WP2 生命周期安全增强（P1-10）

任务:
1. 将 `AsyncState` 中原始 `CoroutineExecutor*` 替换为共享状态（`shared_ptr/weak_ptr` + stop 标记）。

涉及文件:
`src/server/logic/coroutine_executor.h`

验收:
1. executor 析构后回调不会触达悬空对象。

#### WP3 分阶段取消与超时语义（P1-11, P1-12）

任务:
1. 引入 `std::stop_token` 第一阶段：先覆盖 `AsyncWithTimeout` 和 `SleepFor`。
2. 文档化“超时不取消底层阻塞函数”并增加资源占用指标。

涉及文件:
`src/server/logic/task.h`, `src/server/logic/coroutine_executor.h`, `src/server/logic/logic_server.cc`

验收:
1. 主要等待点具备可观取消收益。
2. 语义与指标一致可验证。

#### WP4 异常可观测性（P1-18）

任务:
1. `unhandled_exception` 和 detached 终态补齐日志与指标，禁止静默吞错。

涉及文件:
`src/server/logic/task.h`, `src/server/logic/coroutine_executor.h`, `src/server/logic/coroutine_executor.cc`

验收:
1. 未捕获异常均有统一可观测出口，不影响进程稳定性。

#### WP5 并发组合与热路径优化（P1-15, P1-16, P1-17）

任务:
1. 增加 `WhenAll/WhenAny`，替换 `latch.wait()` 占用 blocking pool。
2. 优化 `HandlerRegistry` 热路径（降低 `std::function` 和哈希开销）。
3. 邮箱增加全局上限与分层背压。

涉及文件:
`src/server/logic/coroutine_executor.h`, `src/server/logic/prewarm_manager.cc`, `src/server/logic/handler_registry.h`, `src/server/logic/handler_registry.cc`, `src/server/logic/logic_server.cc`

验收:
1. 预热不再占用阻塞线程等待。
2. 热路径延迟有可测下降。
3. 极端压力下内存曲线平稳。

### Sprint-2 风险与应对

1. 风险: 取消语义改动影响现有 handler 行为。
应对: 先只改等待点，再按模块灰度扩展。
2. 风险: 热路径优化引入行为回归。
应对: 先建立基准，再做替换，保留回退开关。

---

## Backlog（Sprint-2 之后）

### P2（结构优化）

1. [x] 合并 `Task<T>` / `Task<void>`，抽 `promise_base`，引入 Awaitable Concepts 约束。
2. [x] 指标解耦：`IMetricsSink` 注入。
3. [x] 高频 timer 分配优化（状态内嵌或对象池）。
4. [x] 原子内存序显式化。
5. [x] `noexcept` 审计。
6. [x] 执行器文档补全。
7. [x] 协程饥饿检测（长时间不挂起告警）。

### P3（工程化保障）

1. [x] CI 加 ASAN/TSAN 任务（`.github/workflows/sanitizers.yml`）。
2. [x] 压测基准：协程风暴/超时风暴/关停风暴。
3. [x] 发布门禁：P0/P1 测试与指标阈值强制通过（`.github/workflows/coroutine-release-gate.yml`）。
4. [x] `thread_local` 协程兼容性审计与清单化整改建议。

### 2026-02-11 本轮落地产物

1. `P2-1`: `src/server/logic/task.h`
2. `P2-3`: `src/server/logic/coroutine_executor.h`, `src/server/logic/coroutine_executor.cc`
3. `P2-5`: `src/server/logic/task.h`, `src/server/logic/coroutine_executor.cc`, `docs/coroutine_executor_engineering_guide.md`
4. `P2-6`: `docs/coroutine_executor_engineering_guide.md`
5. `P3-2`: `benchmarks/coroutine_executor_stress_benchmark.cpp`, `benchmarks/CMakeLists.txt`
6. `P3-4`: `docs/thread_local_coroutine_compat_audit.md`
