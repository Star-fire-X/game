# Gateway-Logic 会话一致性修复计划（P0/P1/P2）

## 1. 背景与目标

当前双进程链路（Gateway <-> Logic）存在 3 个关键缺口：

1. Gateway 断线通知（Logout）在 Logic 不可达时会丢失。
2. Gateway 与 Logic 连接断开时，Logic 不会批量清理客户端状态。
3. Logic 重连后 ContextRestore 缺少对账清理，可能保留僵尸会话。

本计划目标：

- 在不破坏现有主流程的前提下，补齐 P0/P1/P2 修复路径。
- 确保故障恢复后状态收敛。
- 提供可回退开关、可观测指标和异常路径测试覆盖。

## 2. 已锁定决策

- P0 批量清理策略：`Gateway 断连时 Logic 全量清理客户端状态`。
- P0 断线通知语义：`至少一次投递`（允许重复 Logout，依赖 Logic 幂等）。
- P1 对账策略：`ContextRestore 缺失即清理`。
- 僵尸检测策略：`心跳超时 + 对账缺失` 双条件。
- 上线策略：`默认开启 + 可配置降级`。
- 重试参数档位：`中等容量 + 有上限`（防内存失控）。

---

## 3. P0（立即修复）

### 3.1 Gateway 断线通知重试队列

**涉及文件**

- `src/server/gateway/gateway_server.h`
- `src/server/gateway/gateway_server.cc`

**新增数据结构（建议）**

- `PendingDisconnectEvent`
  - `uint64_t client_id`
  - `uint64_t sequence`
  - `int64_t first_seen_ms`
  - `int64_t next_retry_ms`
  - `uint32_t retry_count`

**新增成员（GatewayServer）**

- `std::deque<PendingDisconnectEvent> pending_disconnect_events_`
- `uint64_t next_disconnect_sequence_ = 1`

**新增私有方法（GatewayServer）**

- `void EnqueueDisconnectEvent(uint64_t client_id);`
- `void ProcessDisconnectRetryQueue(int64_t now_ms);`
- `bool TrySendDisconnectEvent(const PendingDisconnectEvent& ev);`
- `void TrimExpiredDisconnectEvents(int64_t now_ms);`

**行为变更**

- `UnregisterSession()`：
  - 现状：直接 `NotifyClientDisconnected(client_id)`。
  - 修改后：入队 + 立即尝试发送；失败保留队列重试。
- `ForwardToLogic()` 对 Logout 投递失败不再直接丢弃。
- 在 `Tick()` 中周期执行 `ProcessDisconnectRetryQueue(now_ms)`。

**默认参数（后续进入配置）**

- 初始退避：`500ms`
- 最大退避：`30000ms`
- 事件 TTL：`300000ms`（5 分钟）
- 队列容量：`100000`

**边界处理**

- 队列超限：丢弃最旧事件并计数。
- TTL 超时：丢弃并计数。
- Logic 恢复后：按队列顺序继续回放（至少一次语义）。

### 3.2 Gateway 断连时 Logic 批量客户端清理

**涉及文件**

- `src/server/logic/logic_server.h`
- `src/server/logic/logic_server.cc`

**新增方法（LogicServer）**

- `void OnGatewayDisconnected();`
- `void CleanupAllClientSessions(const char* reason);`

**触发时机**

- `HandleServiceHello()` 中的 `session->SetDisconnectedHandler(...)`：
  - 现状：仅 `gateway_session_.reset()`。
  - 修改后：`gateway_session_.reset()` 后触发 `OnGatewayDisconnected()`。

**清理逻辑**

- 主线程执行（统一 `PostToMainThread`），避免跨线程 ECS 访问。
- 遍历 `client_registry_.GetAll()`，逐个执行：
  - `role_store_->GetRoleId(client_id)` 命中则 `character_manager.OnDisconnect(role_id)`。
  - `role_store_->UnbindClient(client_id)`。
  - `client_registry_.Remove(client_id)`。
  - 清理 mailbox / backpressure 关联状态（若存在）。

---

## 4. P1（本周内）

### 4.1 ContextRestore 对账清理

**涉及文件**

- `src/server/logic/logic_server.h`
- `src/server/logic/logic_server.cc`

**新增方法（LogicServer）**

- `void ReconcileWithGatewaySnapshot(const std::unordered_set<uint64_t>& restored_client_ids,
                                    uint32_t request_id);`

**处理顺序**

1. `HandleContextRestoreResponse()` 解析网关快照，构建 `restored_client_ids`。
2. 先执行对账清理：
   - 本地 `client_registry_` 存在、但不在 `restored_client_ids` 的客户端立即清理。
3. 再执行现有 prewarm 恢复流程（恢复快照中的会话）。

**语义**

- 网关快照为在线真源。
- 本地多余会话按僵尸会话处理，立即移除。

### 4.2 僵尸会话检测

**涉及文件**

- `src/server/logic/logic_server.h`
- `src/server/logic/logic_server.cc`

**新增状态（LogicServer）**

- `std::unordered_map<uint64_t, int64_t> client_last_activity_ms_`
- `std::unordered_set<uint64_t> last_reconciled_client_ids_`
- `int64_t last_zombie_scan_ms_ = 0`

**新增方法（LogicServer）**

- `void MarkClientActivity(uint64_t client_id, int64_t now_ms);`
- `void ScanZombieSessions(int64_t now_ms);`

**触发与判定**

- 在 `HandleRoutedMessage()` 成功收到客户端路由包时更新活跃时间。
- 在 `Tick()` 周期调用 `ScanZombieSessions()`。
- 双条件命中才清理：
  - `now_ms - last_activity_ms >= zombie_idle_timeout_ms`
  - `client_id` 不在 `last_reconciled_client_ids_`

**建议默认值**

- 扫描周期：`5000ms`
- 空闲阈值：`120000ms`

---

## 5. P2（下个迭代）

### 5.1 监控指标完善

**Gateway 指标**

- `gateway.disconnect_queue.size`
- `gateway.disconnect_queue.enqueue_total`
- `gateway.disconnect_queue.retry_total`
- `gateway.disconnect_queue.delivered_total`
- `gateway.disconnect_queue.expired_total`
- `gateway.disconnect_queue.dropped_overflow_total`

**Logic 指标**

- `logic.session.cleanup_all_total`
- `logic.session.cleanup_reconcile_total`
- `logic.session.cleanup_zombie_total`
- `logic.session.reconcile.local_only_total`
- `logic.session.reconcile.snapshot_size`
- `logic.session.zombie_scan_total`

**涉及文件**

- `src/server/monitor/metrics.h`
- `src/server/monitor/metrics.cc`
- 如未启用 Prometheus，对应 `metrics_stub.cc` 保持兼容

### 5.2 异常路径自动化测试

**单测补充**

- Gateway：
  - Logic 离线时断线事件入队不丢。
  - Logic 恢复后重试队列可投递并清空。
  - TTL/容量边界行为。
- Logic：
  - Gateway 断开触发全量清理。
  - ContextRestore 对账清理本地多余会话。
  - 僵尸检测双条件命中与误删保护。

**集成测试补充**

- Gateway-Logic 链路抖动 + 客户端断开 + 重连回放。
- ContextRestore 空快照/子集快照下的状态收敛。

### 5.3 配置化参数

**涉及文件**

- `src/server/config/config_manager.h`
- `src/server/config/config_manager.cc`
- `config/gateway.yaml`
- `config/logic.yaml`

**新增配置建议（默认开启）**

- Gateway：
  - `disconnect_retry.enabled: true`
  - `disconnect_retry.max_queue_size: 100000`
  - `disconnect_retry.ttl_ms: 300000`
  - `disconnect_retry.initial_backoff_ms: 500`
  - `disconnect_retry.max_backoff_ms: 30000`
- Logic：
  - `session_cleanup_on_gateway_disconnect: true`
  - `reconcile_cleanup_enabled: true`
  - `zombie_detection.enabled: true`
  - `zombie_detection.scan_interval_ms: 5000`
  - `zombie_detection.idle_timeout_ms: 120000`

---

## 6. 验收标准

1. Logic 不可达期间发生的客户端断开，恢复后可被回放到 Logic，不再静默丢失。
2. Gateway-Logic 连接断开后，Logic 内存态会话可在一次批量流程中清理。
3. ContextRestore 后，Logic 会话集合与网关快照收敛一致。
4. 僵尸会话可在双条件命中时被清理，活跃会话不误删。
5. 新增测试覆盖异常路径，CI 可稳定通过。

## 7. 风险与回滚

- 风险1：重复 Logout 导致副作用。
  - 缓解：保持 Logout 路径幂等；仅基于 `client_id` 清理。
- 风险2：对账清理误删。
  - 缓解：默认开启但可配置关闭；保留关键日志与指标。
- 风险3：重试队列内存增长。
  - 缓解：容量上限 + TTL + 指标告警。

**回滚策略**

- 通过配置关闭：`disconnect_retry` / `session_cleanup_on_gateway_disconnect` /
  `reconcile_cleanup_enabled` / `zombie_detection.enabled`。
- 保留现有主链路，开关关闭后退回当前行为。

