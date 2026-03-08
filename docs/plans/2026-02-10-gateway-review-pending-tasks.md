# Gateway 模块待完成任务清单（2026-02-10）

## 1. 说明

本清单用于承接网关代码审查后的**剩余事项**。  
已完成项（如断线重试队列、Payload 限制、KCP 通道透传、RingBuffer 回滚安全）不在此重复展开。

---

## 2. P0（必须优先完成）

### 2.1 Logic 侧会话一致性补齐（与 Gateway 重试队列配套）

**现状**

- Gateway 已实现 Logout 至少一次重试。
- Logic 侧仍缺少“Gateway 断开后的批量会话清理 + ContextRestore 对账清理”闭环。

**涉及文件**

- `src/server/logic/logic_server.h`
- `src/server/logic/logic_server.cc`
- 参考：`docs/plans/2026-02-10-gateway-logic-session-consistency-remediation-plan.md`

**验收标准**

1. Gateway 断连后，Logic 能批量清理在线客户端状态。
2. ContextRestore 后，Logic 本地会话集合与 Gateway 快照收敛一致。
3. 对应单测/集成测试可稳定复现并通过。

---

## 3. P1（本迭代建议完成）

### 3.1 `session_map_mutex_` 锁拆分与热点路径降锁

**现状**

- `session_map_ / connection_holders_ / backpressure / disconnect_queue` 共用同一把 `shared_mutex`。
- 大量连接下，Tick 中多个阶段互相竞争，存在吞吐上限。

**涉及文件**

- `src/server/gateway/gateway_server.h`
- `src/server/gateway/gateway_server.cc`

**验收标准**

1. 将 backpressure / disconnect_queue 从主会话锁中拆分。
2. `FlushBufferedMessages`、`ProcessDisconnectRetryQueue` 减少锁内遍历时间。
3. 压测下锁等待时间与 Tick 抖动下降。

### 3.2 `ScheduleReconnect` 生命周期显式化

**现状**

- 重连定时器回调捕获 `this`，目前依赖 shutdown 顺序与 io_context 停止语义。

**涉及文件**

- `src/server/gateway/gateway_server.h`
- `src/server/gateway/gateway_server.cc`

**建议方向**

1. 引入可取消 timer 容器并在 `Shutdown()` 统一 cancel。
2. 或将对象生命周期改为 `weak_ptr` 守护回调执行。

**验收标准**

1. 在“连接抖动 + 快速启动/关闭”场景下无悬挂回调风险。
2. 对应并发/生命周期测试通过。

### 3.3 MessageRouter 废弃代码彻底移除

**现状**

- `message_router.h/.cc` 已无业务逻辑，仅保留空壳兼容。

**涉及文件**

- `src/server/gateway/message_router.h`
- `src/server/gateway/message_router.cc`
- `src/server/CMakeLists.txt`
- `tests/server/message_router_test.cc`
- `tests/server/gateway_config_test.cc`
- `tests/server/gateway_routing_test.cc`

**验收标准**

1. 删除无效实现与引用。
2. 相关测试替换为“无 Router 依赖”的结构性断言。

### 3.4 新增硬编码参数配置化

**现状**

- 以下关键阈值仍为代码常量：
  - 转发最大消息体
  - 断线重试初始退避/最大退避/TTL/队列上限

**涉及文件**

- `src/server/config/config_manager.h`
- `src/server/config/config_manager.cc`
- `config/gateway.yaml`
- `src/server/gateway/gateway_server.cc`

**验收标准**

1. 参数可通过配置下发并含默认值/边界校验。
2. 配置异常时具备告警日志与回退策略。

---

## 4. P2（下个迭代）

### 4.1 协议层面澄清 `EnterGameReq` 缺失问题

**现状**

- `MsgId` 中无 `kEnterGameReq`，但业务流程存在“进入游戏”语义。

**涉及文件**

- `src/common/enums.h`
- `schemas/*.fbs`（登录/游戏相关）
- `src/server/gateway/gateway_server.cc`
- `src/server/logic/handlers/character/*`

**验收标准**

1. 明确“是否存在 EnterGameReq”的协议决策（新增或文档化不需要）。
2. Gateway/Logic/Client 三端行为一致且有测试覆盖。

### 4.2 网关重试队列边界测试补齐

**现状**

- 已有“离线入队、恢复回放”路径测试。
- TTL 过期、容量溢出、指数退避节奏尚缺自动化覆盖。

**涉及文件**

- `tests/server/gateway_forwarding_test.cc`
- `tests/integration/connection_holding_test.cc`（可扩展）

**验收标准**

1. TTL 到期会清理并计数。
2. 队列溢出会丢弃最旧并计数。
3. 重试间隔符合退避策略。

### 4.3 `HandleForwardMessage` 线程模型约束显式化

**现状**

- 当前行为默认依赖 io_context 串行执行模型。
- 缺少注释/断言约束，后续改线程模型时存在误用风险。

**涉及文件**

- `src/server/gateway/gateway_server.cc`
- `src/server/network/*`（消息分发入口）

**验收标准**

1. 在关键路径增加线程模型约束注释或断言（debug）。
2. 文档中明确 Gateway 消息处理线程语义。

---

## 5. 执行顺序建议

1. 先完成 Logic 一致性闭环（2.1）。
2. 再做锁拆分与生命周期安全（3.1、3.2）。
3. 同步推进配置化与测试补齐（3.4、4.2）。
4. 最后清理技术债（3.3、4.1、4.3）。
