# MovementHandler / MovementValidator 改进计划与优化方案

## 1. 目标与范围

本文档基于当前代码评审结论，输出可执行的修复与优化方案，覆盖：

- `src/server/logic/handlers/movement/movement_handler.cc`
- `src/server/logic/handlers/movement/movement_validator.cc`
- 相关依赖：`SceneManager`、`TeleportSystem`、`ClientRegistry`、`AntiCheat`

不在本次范围：协议大版本升级、跨服移动架构改造。

## 2. 优先级总览

### P0（必须立即处理）

1. 广播范围从全量客户端改为 AOI 广播。
2. 修复移动执行结果未校验导致的“失败仍返回成功并广播”问题。
3. 修复方向字段固定为 `0` 的问题。

### P1（近期处理）

1. 缩小 `move_mutex_` 锁粒度，移除锁内不必要操作。
2. 明确并固化 ECS 线程模型约束（断言/注释/测试）。
3. 统一构造函数语义，避免 `ecs_registry_` 可空导致调用方误用。

### P2（持续优化）

1. 速度校验算法按游戏规则参数化（欧氏/切比雪夫/步数制）。
2. 移除魔数与硬编码严重度，改配置化。
3. `TryParseMapId` 从异常路径迁移为 `from_chars`。

## 3. 详细改进方案

### 3.1 P0-1：AOI 广播替代全量广播

问题现状：
- `BroadcastEntityMove` 遍历 `client_registry_.GetAll()` 全量发送。

改造方案：
- 新增 AOI 查询接口（优先复用 `SceneManager` / `MapInstance` 现有索引）。
- 在 `HandleMove` 成功后，仅向同地图且在可见范围的玩家广播。
- 兜底策略：AOI 查询失败时仅回包给自身，不做全服广播。

验收标准：
- 万人在线压测中，单次移动广播发送量与 AOI 内玩家数线性相关，而非与总在线人数线性相关。
- `MovementHandlerTest` 新增“多地图+远距玩家不收到广播”测试。

### 3.2 P0-2：移动执行结果强校验

问题现状：
- `SetPosition` / `AddEntityToMap` / `UpdateEntityPosition` 返回值未用于最终结果判定。

改造方案：
- 对以下调用结果做强校验，任一步失败则返回明确错误码并禁止广播：
  - `character_manager_.SetPosition(...)`
  - `scene_manager_.AddEntityToMap(...)`
  - `scene_manager_.UpdateEntityPosition(...)`
- 增加失败日志，包含：`entity_id/map_id/x/y/step`。

验收标准：
- 相关调用人为注入失败时，`MoveRsp.code != kOk`，且无 `EntityMove` 广播。
- 新增单元测试覆盖三类失败分支。

### 3.3 P0-3：方向字段正确化

问题现状：
- 广播调用固定 `direction=0`。

改造方案：
- 使用 `from -> to` 计算方向并写入 `CharacterStateComponent.direction`。
- `BroadcastEntityMove` 使用计算后的方向。
- 若 `from == to`，沿用旧方向。

验收标准：
- 客户端可观察到八方向转向正确。
- 新增方向映射单元测试（8 邻域 + 原地）。

### 3.4 P1-1：锁粒度优化

问题现状：
- `move_mutex_` 覆盖验证、状态更新、反作弊记录、场景更新等完整流程。

改造方案：
- 第一阶段：仅保护 `last_move_time_ms_` 读写。
- 第二阶段：若仍需串行化，改为“按实体分片锁（striped mutex）”替代全局单锁。
- 将 `RecordMoveViolation` 移出锁外执行。

验收标准：
- 并发移动场景下，锁等待时间明显下降。
- 无新增数据竞争（TSan 或并发单测通过）。

### 3.5 P1-2：线程模型固化

问题现状：
- 项目文档声明 ECS 单线程，但 `MovementHandler` 未体现显式约束。

改造方案：
- 在 `MovementHandler::HandleMove` 增加线程约束注释。
- Debug 模式增加线程一致性断言（复用逻辑线程 ID）。
- 在代码评审基线中加入“禁止跨线程访问 registry”的检查项。

验收标准：
- 调试环境误用可被断言快速发现。

### 3.6 P1-3：构造函数收敛

问题现状：
- 现有双构造函数导致 `ecs_registry_` 可空，调用方难以静态保证。

改造方案：
- 推荐收敛为单构造：强制要求 `entt::registry&`。
- 若需兼容，保留过渡版本但标记 `[[deprecated]]`，并在初始化时打印告警。

验收标准：
- 新代码路径不再出现 `registry == nullptr` 的运行时分支。

### 3.7 P2-1：速度验证算法参数化

问题现状：
- 当前速度使用欧氏距离，是否契合设计需明确。

改造方案：
- 在 `MovementValidator::Config` 增加 `distance_metric`：
  - `euclidean`
  - `chebyshev`
  - `manhattan`
  - `step_based`
- 默认保持现状，逐步灰度切换。

验收标准：
- 新增对角移动/直线移动的规则一致性测试。

### 3.8 P2-2：配置化与可维护性

改造项：
- 将 `0x8000` 提取为命名常量（如 `kNoFlyMask`）。
- 反作弊严重度 `10/5` 下沉到配置（可热更新优先）。

验收标准：
- 代码中移除对应魔数。

### 3.9 P2-3：低开销 map_id 解析

改造项：
- `TryParseMapId` 使用 `std::from_chars`，避免异常路径开销。

验收标准：
- 功能等价测试通过（非法输入、边界值、溢出）。

## 4. 测试计划

### 单元测试

- `MovementHandlerTest`：
  - AOI 广播范围正确性。
  - 执行失败不广播。
  - 方向编码正确。
  - 并发移动场景下状态一致性。
- `MovementValidatorTest`：
  - 不同距离度量下的速度判定。
  - 对角移动规则与阻挡判定。

### 集成测试

- 同图多玩家移动同步。
- 跨图传送触发后移动链路正确。
- 高并发移动下服务器稳定性与延迟。

### 性能测试

- 指标：
  - 每次移动平均广播包数
  - 移动处理 p95/p99 延迟
  - 锁等待时间占比
- 对比基线：改造前 vs 改造后。

## 5. 发布与回滚

发布策略：

1. 先上线“执行失败强校验 + 日志增强”。
2. 再灰度 AOI 广播（可配置开关：`movement.aoi_broadcast_enabled`）。
3. 最后上线锁粒度优化与速度模型参数化。

回滚策略：

- 保留开关：`movement.aoi_broadcast_enabled=false` 可快速回退到旧广播路径（仅建议临时）。
- 若出现移动拒绝率异常升高，回退速度模型配置到旧值。

## 6. 任务拆解（建议工单）

1. `movement-p0-01`：AOI 广播改造 + 单测。
2. `movement-p0-02`：移动执行结果强校验 + 单测。
3. `movement-p0-03`：方向计算与广播修复 + 单测。
4. `movement-p1-01`：锁粒度优化与并发测试。
5. `movement-p1-02`：线程模型断言与文档补充。
6. `movement-p1-03`：构造函数收敛与迁移。
7. `movement-p2-01`：速度模型参数化。
8. `movement-p2-02`：魔数/严重度配置化。
9. `movement-p2-03`：`from_chars` 替换与基准验证。

## 7. 预期收益

- 广播成本从“与全服在线人数线性相关”下降到“与 AOI 局部人数线性相关”。
- 减少“状态更新失败但客户端显示成功”的一致性故障。
- 提升移动表现正确性（方向同步）。
- 降低并发场景锁竞争与维护风险。

## 8. 测试计划执行结果（2026-02-13）

已完成项（本地可执行）：

- 单元测试：`MovementHandlerTest.*`（5/5 通过）
- 单元测试：`MovementValidatorTest.*`（16/16 通过）
- 配置解析测试：
  - `GatewayErrorHandlingTest.ServerConfig_DefaultLoginIpRateLimitValues`（通过）
  - `GatewayErrorHandlingTest.LoadConfig_LoginIpRateLimitFieldsParsed`（通过）

集中回归命令：

```bash
./build-wsl/bin/legend2_tests \
  --gtest_filter=MovementHandlerTest.*:MovementValidatorTest.*:GatewayErrorHandlingTest.ServerConfig_DefaultLoginIpRateLimitValues:GatewayErrorHandlingTest.LoadConfig_LoginIpRateLimitFieldsParsed
```

结果：`23/23` 通过。

说明：

- 测试日志中的 `Combat config load failed: /tmp/combat_config.yaml` 来自临时配置测试环境，不影响本次 movement/anti-cheat 配置链路验证。
- 计划中的压测与线上指标观测（p95/p99、锁等待占比）需在集成/压测环境执行，已具备代码与开关基础。
