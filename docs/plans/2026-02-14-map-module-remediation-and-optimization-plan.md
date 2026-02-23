# Map 模块改进计划与优化方案（基于审查报告）

- 文档日期：2026-02-14
- 适用范围：`src/server/game/map/`、`src/server/ecs/`、相关测试
- 目标：修复高风险逻辑缺陷，消除并发隐患，补齐验证与可维护性，避免地图/区域事件回归

## 1. 总体策略

1. 先修复行为错误与一致性问题（P0），再处理并发与健壮性（P1），最后进行可维护性优化（P2）。
2. 每个改动必须配套测试（单测 + 集成测试），并提供回滚开关或回滚路径。
3. 对“语义待确认”的问题先补规范决策，再落代码，避免实现偏航。

## 2. 优先级与里程碑

### P0（本周完成）

1. 持续效果移除不同步（事件关闭后仍在地图里生效）。
2. 跨地图传送的短暂双地图窗口。
3. `load_walkability=false` 时缺少宽高合法性校验。

### P1（下周完成）

1. Mine 语义统一（一次触发 vs 持续触发）并修正实现。
2. AreaEvent 同步分发导致潜在死锁。
3. 地图瓦片读入顺序确认与兼容策略。
4. `tick_interval <= 0` 的最小间隔保护。

### P2（滚动优化）

1. AOI 事件坐标语义文档化。
2. AOI 重复代码提取。
3. 路径基准缓存策略完善（Map/Gate）。
4. 日志统一（替换 `std::cerr`）。

## 3. 分项改进方案

## 3.1 Mine 实现（语义决策后执行）

- 问题：当前 `kMine` 走 `ContinuousAreaEffect`，按 tick 周期触发。
- 方案 A（推荐）：改为 `AreaTrigger` 一次触发。
  - `MapEventManager::AddMineEvent` 创建 `AreaTrigger`，`on_enter` 发布 `MineEvent`。
  - 触发后立即 `RemoveAreaTrigger(trigger_id)`。
- 方案 B（兼容旧逻辑）：保留持续效果，但增加“实体-效果已触发集合”，每实体只触发一次。
- 验收：
  - 玩家进入地雷范围仅触发一次。
  - 同一地雷对同一玩家不重复触发。
  - 原有火墙/圣言等持续效果行为不受影响。

## 3.2 持续效果移除同步（P0）

- 问题：`MapEventManager::RemoveEvent`/自然结束仅删除 `active_events_`，未删 `AreaEventProcessor::effects_`。
- 方案：
  - 在 `AreaEventProcessor` 增加 `RemoveContinuousEffect(uint32_t effect_id)`。
  - 在 `MapInstance` 增加 `RemoveContinuousAreaEffect(uint32_t effect_id)` 包装。
  - `MapEventManager` 在 `RemoveEvent` 与自然结束分支同步调用 map 侧移除。
- 验收：
  - `RemoveEvent` 后不再触发 tick 事件。
  - 自然过期后下一帧起无残留触发。

## 3.3 Teleport 原子性窗口（P0）

- 问题：先加目标图再删源图，短窗口内实体可能存在两图。
- 方案：
  - `SceneManager::TeleportEntity` 改为“校验目标坐标 -> 源图移除 -> 目标图添加 -> 索引更新”。
  - 添加失败回滚：目标添加失败则尝试恢复源图；回滚失败记录高优先级错误日志并上抛业务处理。
  - 继续依赖 `entity_ops_mutex_` 保证操作串行。
- 验收：
  - 任意时刻实体最多属于一个 `MapInstance`。
  - 失败路径保持实体仍可查询、位置一致。

## 3.4 `LoadMapDataUnlocked` 参数校验（P0）

- 问题：`load_walkability=false` 时直接采用配置宽高，未校验 > 0。
- 方案：
  - 在 `LoadMapDataUnlocked` 入口添加宽高合法性检查。
  - 非法配置直接返回失败并输出统一日志。
- 验收：
  - 宽高非法配置时 `CreateMap/GetOrCreateMap` 明确失败，无半初始化地图。

## 3.5 AreaEvent 分发死锁风险（P1）

- 问题：`MapInstance::UpdateAreaEvents` 持 `mutex_`，`EventBus::Publish` 同步触发回调，可能重入 MapInstance 写锁。
- 方案（推荐）：
  - `AreaEventProcessor::Update` 不直接 `Publish`，改为收集事件到本地队列。
  - 释放 `MapInstance::mutex_` 后统一发布（或交给 logic tick 统一消费）。
- 备选：
  - 保持同步发布，但约束订阅者不得重入 map 写操作（约束难长期保证，不推荐）。
- 验收：
  - 压测场景下无死锁。
  - 事件顺序符合原语义（同一 effect tick 顺序稳定）。

## 3.6 地图瓦片加载顺序验证（P1）

- 问题：`ReadTiles` 采用列优先映射，格式假设未被文档与样本验证。
- 方案：
  - 新增 map loader 校验测试，使用已知样本地图比对关键坐标瓦片值。
  - 在代码注释明确“当前采用列优先/行优先”依据。
  - 若需兼容双格式：加配置开关 `tile_order: column_major|row_major`（默认保持现状）。
- 验收：
  - 样本地图读取后的关键坐标与期望一致。
  - 不破坏现有线上地图解析。

## 3.7 `tick_interval <= 0` 保护（P1）

- 问题：当前每次 `Update` 至多触发一次，但仍可能导致高频事件。
- 方案：
  - 写入时钳制：`tick_interval = max(tick_interval, kMinTickInterval)`。
  - 建议 `kMinTickInterval = 0.05f`（20Hz）或按配置下限。
- 验收：
  - 非法/异常间隔输入不会造成事件风暴。

## 3.8 AOI 语义与可维护性（P2）

- 坐标语义：
  - 在 `AOIEventType::kLeave` 注释明确“目标坐标是离开方最新坐标（当前实现）”。
  - 如上层需要旧坐标，扩展事件结构携带 `old_x/old_y`。
- 重复代码提取：
  - 抽取 `CollectSurroundingEntities`/`EmitGridDeltaEvents` 辅助函数，减少 `Enter/Leave/Move` 重复逻辑。

## 3.9 路径基准缓存与日志统一（P2）

- 路径基准：
  - 现状是静态缓存（有意避免漂移）。
  - 增补注释并增加可选配置基准路径（优先配置绝对路径）。
- 日志统一：
  - `GateManager` 的 `std::cerr` 改为统一日志宏（`SYSLOG_*` 或项目 logger）。

## 4. 测试计划

## 4.1 单元测试

1. `MapEventManagerTest`：
   - 移除事件后不再触发 tick。
   - Mine 单次触发语义（若采用方案 A）。
2. `AreaEventProcessorTest`：
   - `RemoveContinuousEffect` 后停止分发。
   - `tick_interval` 下限钳制。
3. `SceneManagerTest`：
   - `TeleportEntity` 全流程原子性与失败回滚。
4. `MapLoaderTest`（新增）：
   - 行/列顺序验证（样本地图断言）。

## 4.2 集成测试

1. 跨地图传送 + AOI 同步 + 区域事件并发场景。
2. 高 tick 频率下的地图事件稳定性与无死锁验证。
3. 地雷触发链路（移动进入、离开再进入、多人并发进入）。

## 5. 实施顺序（建议）

1. 先做 P0：3.2 -> 3.3 -> 3.4（风险最高且改动相对独立）。
2. 再做 P1：3.5（先解锁并发安全）-> 3.1（Mine 语义）-> 3.6 -> 3.7。
3. 最后做 P2：3.8/3.9。

## 6. 回滚与灰度

1. Mine 语义切换加开关：`map.mine_trigger_mode = continuous|on_enter_once`。
2. Teleport 回滚失败场景输出结构化日志，触发告警并进入补偿任务。
3. AreaEvent 异步发布保留同步兼容开关，便于快速回退。

## 7. 交付物清单

1. 代码改动：`map_event_manager`、`map_instance`、`area_event_processor`、`scene_manager`、`map_loader`、`gate_manager`。
2. 文档更新：地图事件语义、AOI leave 坐标语义、地图加载顺序说明。
3. 测试改动：对应单测/集成测试新增与回归通过记录。

## 8. 完成判定（DoD）

1. P0/P1 项测试全部通过，且无新增已知死锁。
2. 地图事件在 30 分钟压力回放中无异常事件风暴。
3. 代码审查通过，文档与实现一致。
