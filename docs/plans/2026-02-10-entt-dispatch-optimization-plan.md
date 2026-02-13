# EnTT 同步 Dispatch 改进计划与优化方案（2026-02-10）

## 1. 背景与已验证事实

基于当前代码实现，先确认事实，再给方案：

1. `EventBus::Publish()` 当前固定走同步 `trigger()`，handler 在调用栈内执行。  
   参考：`src/server/ecs/event_bus.h:25`
2. `FlushEvents()` 并非未使用，`World::Update()` 每帧已调用。  
   参考：`src/server/ecs/world.cc:68`
3. 当前为 **每个 World 一份 EventBus**，不是 `LogicServer` 单实例全局 EventBus。  
   参考：`src/server/logic/logic_server.h:201`
4. 代码中 `Publish(...)` 调用点约 100+，但真实订阅点远少于事件定义总数。  
   重点订阅：`EntityDeathEvent`、`TeleportRequestEvent`、`NpcOpenStorageEvent`、`NpcOpenMerchantEvent`、NPC Query 4 类事件。
5. 死亡链最重：`CombatSystem::Die()` 同步发布 `EntityDeathEvent`，触发升级/掉落/（潜在）刷怪处理。  
   参考：`src/server/ecs/systems/combat_system.cc:376`
6. NPC Query 当前是事件模拟同步 RPC，含裸指针出参（`bool*`/`int*`/`std::string*`），同步语义强依赖明显。  
   参考：`src/server/ecs/events/npc_events.h:77`

---

## 2. 目标与非目标

### 2.1 目标

1. 降低战斗热路径同步扇出带来的帧内尖峰。
2. 消除 NPC Query 事件模型中的裸指针与同步耦合。
3. 在不破坏战斗数值语义的前提下，引入可控 deferred 机制。
4. 提供可观测性（指标）与可回滚开关。

### 2.2 非目标

1. 不在本次改造中重写 HotEventPipeline（其异步模型已独立）。
2. 不一次性把所有事件改为 deferred。
3. 不先做高复杂度 batched API（当前对应订阅者不足，收益不确定）。

---

## 3. 关键设计原则

1. **语义优先于性能**：先保证玩法一致，再做延迟分发。
2. **先拆耦，再延迟**：先消除同步 RPC 误用，再分级调度。
3. **快照优先**：deferred 事件不能依赖“死亡实体仍然存在”。
4. **默认兼容**：默认仍 `kImmediate`，逐事件白名单迁移。

---

## 4. 总体优化方案（修正版）

### 4.1 事件分为三类

1. `kImmediate`：必须同步可见结果（如 Query/强一致逻辑）。
2. `kDeferred`：帧末统一处理的通知类事件（使用快照载荷）。
3. `kBatched`：暂不启用，留给未来出现真实高频订阅者时使用。

### 4.2 不直接把 `EntityDeathEvent` 全量改 deferred

原因：升级会修改属性并影响同一技能链后续伤害计算，直接延迟会改变战斗结果。  
修正策略：拆分死亡后处理事件：

1. `EntityDeathExpEvent`（同步）：经验/升级相关。
2. `EntityDeathPostEvent`（延迟）：掉落、刷怪计数等后处理。

---

## 5. 分阶段执行计划

### Phase P0：NPC Query 去事件化（优先级最高）

#### 目标

将 `hasItem/getGold/getPlayerName/getPlayerLevel` 从“事件+结果事件+request_id”改为直接查询服务调用。

#### 改造点

1. 新增查询接口（示例）：
   - `src/server/game/npc/npc_query_service.h`
   - `src/server/game/npc/ecs_npc_query_service.cc`
2. 在 Lua 绑定中替换 query 事件发布：
   - `src/server/game/npc/lua_bindings.cc`
3. NPC AI 侧移除 Query 事件处理器职责：
   - `src/server/ecs/systems/npc_ai_system.cc`
4. 将 `NpcHasItemResultEvent/NpcGetGoldResultEvent/NpcGetPlayerNameResultEvent/NpcGetPlayerLevelResultEvent` 标记 deprecated（先保留结构定义，后续再删除）。

#### 风险与控制

1. 风险：调用链注入复杂度上升。  
   控制：先用接口注入，不直接把 `registry` 暴露给 Lua。
2. 风险：脚本行为回归。  
   控制：补齐 Lua API 等价测试（返回值、失败分支）。

---

### Phase P1：事件分级基础设施（默认兼容）

#### 目标

引入 traits 与调度策略，但默认行为不变。

#### 改造点

1. 新增策略定义：
   - `src/server/ecs/event_dispatch_policy.h`
2. 改造 EventBus：
   - `src/server/ecs/event_bus.h`
   - 增加 `PublishImmediate(...)`
   - `Publish(...)` 根据 `EventTraits<T>::policy` 分流
3. `FlushEvents()` 增加统计返回值（`dispatched/remaining/budget_hit`），便于可观测。

#### 关键约束

1. 默认 `EventTraits<T>` 为 `kImmediate`，确保向后兼容。
2. 对 `kDeferred` 仅允许白名单事件，且要求事件载荷不含裸指针出参。

---

### Phase P1.5：死亡链拆分（语义安全）

#### 目标

在不改变升级时序语义的前提下，削减死亡链同步负载。

#### 改造点

1. 新增事件类型（示例）：
   - `EntityDeathExpEvent`（立即）
   - `EntityDeathPostEvent`（延迟）
2. `CombatSystem::Die()` 同步发布 `EntityDeathExpEvent`，并发布 `EntityDeathPostEvent`（deferred）。
3. `LevelUpSystem` 改订阅 `EntityDeathExpEvent`。
4. `MonsterDropSystem`/`MonsterSpawnSystem` 改订阅 `EntityDeathPostEvent`。

#### 事件载荷建议（快照）

`EntityDeathPostEvent` 至少包含：

1. `monster_template_id`
2. `spawn_point_id`
3. `map_id`
4. `death_position`
5. `killer`
6. `skill_id`

这样 handler 不依赖死亡实体仍有效。

---

### Phase P2：Flush 集成与预算策略

#### 目标

保留现有帧末 flush 位置，增加监控；预算控制按可实现路径推进。

#### 现状

`dispatcher_.update()` 是整批处理，单次调用无法真正“中途停下”。

#### 策略

1. 先保留 `World::Update()` 的帧末 flush（低风险）。
2. 预算需求先通过“每帧 deferred 发布数量上限 + 指标告警”实现。
3. 若后续必须硬预算，再引入自维护 deferred 队列（不直接依赖 `dispatcher_.update()`）。

---

### Phase P3：高频事件批处理（暂缓）

仅当出现真实订阅者（伤害统计、战斗日志等）后再启动：

1. 为 `DamageDealtEvent/SkillCastEvent` 提供 batch handler API。
2. 对比前后 CPU 与尾延迟再决定是否长期保留。

---

## 6. 监控与验收

### 6.1 新增指标建议

1. `logic.ecs.event.publish_total{event,policy}`
2. `logic.ecs.event.flush_dispatched_total`
3. `logic.ecs.event.flush_remaining`
4. `logic.ecs.event.flush_budget_hit_total`
5. `logic.ecs.event.deferred_queue_depth`
6. `logic.ecs.npc_query_direct_total{query,result}`

### 6.2 验收标准

1. 功能一致性：
   - NPC Query 返回结果与旧实现一致；
   - 升级/经验语义不变；
   - 掉落/刷怪不丢失。
2. 性能目标（建议）：
   - AOE 场景下 `Tick` p95/p99 下降；
   - 战斗热点帧内同步耗时下降；
   - 无新增明显 tail spike。

---

## 7. 测试计划

### 7.1 单元测试

1. EventBus policy 分流测试（Immediate/Deferred）。
2. Death event 拆分测试（exp 与 post 事件独立性）。
3. NPC Query 直接调用等价性测试。

### 7.2 集成测试

1. AOE 连杀 + 升级时序测试（确认数值语义未变）。
2. 死亡后实体提前失效场景（确认掉落/刷怪依然正确，依赖快照）。
3. Lua 脚本 query API 回归测试。

### 7.3 压测回归

1. 攻城/怪物密集场景，观察 tick duration 与 flush 相关指标。
2. 对比改造前后 p95/p99。

---

## 8. 灰度与回滚

### 8.1 开关建议

1. `ecs.event_policy_enabled`
2. `ecs.death_post_deferred_enabled`
3. `npc.query_direct_enabled`

### 8.2 回滚策略

1. 关闭 `ecs.death_post_deferred_enabled`，全部回到同步发布。
2. 关闭 `npc.query_direct_enabled`，恢复旧 query 事件链（过渡期保留）。
3. 保留旧代码路径至少一个版本窗口，再做清理。

---

## 9. 里程碑与工期建议

1. Day 1：P0（NPC Query 去事件化）+ 单测
2. Day 2：P1（事件分级基础设施）+ 指标
3. Day 3：P1.5（死亡链拆分）+ 集成测试
4. Day 4：压测、灰度、回滚演练

---

## 10. 本计划的核心修正点（相对原提案）

1. `FlushEvents()` 不是“未使用”，当前已在 world 帧末执行。
2. 不将 `EntityDeathEvent` 直接全量 deferred，避免战斗语义变化。
3. deferred 事件必须快照化，避免对死亡实体生命周期的隐式依赖。
4. 预算 flush 不能仅靠 `dispatcher_.update()` 参数化实现，需分阶段推进。
