# Map System 模块整改落地方案（2026-02-10）

## 1. 目标与范围

基于 `docs/MAP_SYSTEM_CODE_REVIEW_REVISED.md` 的复核结果，按“线上风险优先、可回归优先”推进地图模块整改。

本方案目标：

1. 先消除并发一致性风险（P0）。
2. 消除随机卷轴读盘热路径（P1）。
3. 收敛遗留设计债（P2）。
4. 形成可验收、可回滚、可观测的交付。

---

## 2. 优先级重排（执行基线）

### P0（本周必须完成）

1. `MapInstance::AddEntity/RemoveEntity/UpdateEntityPosition` 的分段加锁一致性窗口。
2. `SceneManager` 持锁调用 `MapInstance` 变更接口的重入/锁顺序风险。
3. `SceneManager` 对外返回裸指针的并发生命周期风险（补强接口语义）。

### P1（P0 后紧跟）

1. `ScrollTeleport::UseDungeonScroll` 每次使用随机卷轴都从磁盘加载 walkability。

### P2（下个迭代）

1. `walkability_` 与 `tile_data_->walkable` 双写收敛。
2. `PathfindingHelper` 无 checker 重载去歧义（弃用或改名）。
3. Client `MapSystem` 双命名 API 收敛（snake_case / PascalCase）。
4. `ChunkManager` / `MapEventManager` 的定位重整（接入或降级为实验模块）。

---

## 3. 分阶段实施

## Phase A：并发回归用例先行（0.5~1 天）

目标：先把风险固化为可重复测试，避免“修了又回归”。

涉及文件（新增）：

- `tests/server/map/map_instance_concurrency_test.cpp`
- `tests/server/map/scene_manager_concurrency_test.cpp`

关键用例：

1. `AddEntity` 与 `RemoveEntity` 并发交错，验证 AOI/实体集合不出现残留与重复事件。
2. `ReloadMap/DestroyMap` 与 `GetMapByEntity/UpdateEntityPosition` 并发，验证不崩溃、不悬挂。
3. 1000+ 次循环压力回归（可在 CI 夜测阶段放大到 10000 次）。

---

## Phase B：P0 并发一致性修复（1.5~2.5 天）

### B1. MapInstance 原子操作收敛

涉及文件：

- `src/server/game/map/map_instance.h`
- `src/server/game/map/map_instance.cc`

改造要点：

1. 为实体增删改引入统一操作临界区（建议独立 `entity_ops_mutex_` 或等价串行化机制）。
2. 移除“先插入/删除实体集合，再解锁调用 AOI，再回滚”的分段流程。
3. 保证每个实体生命周期变更在逻辑上只有一次提交点，AOI 与 `area_event_processor_` 状态一致。

验收标准：

1. 新并发测试稳定通过（无 flaky）。
2. `MapInstance` 单测和现有集成测试不回退。

### B2. SceneManager 锁边界重构

涉及文件：

- `src/server/game/map/scene_manager.h`
- `src/server/game/map/scene_manager.cc`

改造要点：

1. 不在持有 `SceneManager::mutex_`（unique）期间调用 `map->AddEntity/RemoveEntity/UpdateEntityPosition`。
2. 将流程改为“读锁快照 -> 解锁执行 map 变更 -> 写锁提交索引（带重检）”。
3. `entity_to_map_` 索引语义收敛（建议改为 map_id 索引或等价安全结构，避免裸指针长期保存）。

验收标准：

1. `SceneManager` 并发用例稳定通过。
2. `TeleportEntity`、`ReloadMap`、`AddEntityToMap` 行为与原语义一致（功能测试通过）。

### B3. 对外接口安全补强

涉及文件：

- `src/server/game/map/scene_manager.h`
- `src/server/game/map/scene_manager.cc`
- `src/server/logic/handlers/movement/movement_handler.cc`

改造要点：

1. 增加“按 `map_id` 查询”的安全接口（例如 `TryGetEntityMapId`），减少跨锁域长期持有 `MapInstance*`。
2. 业务调用优先使用 map_id + 即时查询模式，降低悬挂风险。

---

## Phase C：P1 热路径性能修复（0.5~1 天）

### C1. 随机卷轴去读盘

涉及文件：

- `src/server/game/map/scroll_teleport.h`
- `src/server/game/map/scroll_teleport.cc`
- `src/server/game/item/item_effect_processor.cc`
- `tests/server/map/scroll_teleport_test.cpp`

改造要点：

1. `UseDungeonScroll` 不再内部读取 `.map` 文件。
2. 随机落点统一走已加载地图内存数据（`MapInstance::IsWalkable`）或显式注入 walkability provider。
3. 删除或冻结 `scroll_teleport.cc` 内部读盘候选路径逻辑，避免再次回流。

验收标准：

1. 随机卷轴调用路径无磁盘 IO（可通过日志埋点或 mock 验证）。
2. `item_effect_processor` 相关用例通过。

---

## Phase D：P2 设计债收敛（2~4 天，可拆多 PR）

### D1. walkability 单一数据源

涉及文件：

- `src/server/game/map/map_instance.h`
- `src/server/game/map/map_instance.cc`
- `src/server/game/map/map_loader.h`
- `src/server/game/map/map_loader.cc`

目标：

1. 确认唯一权威数据（建议 `tile_data_->walkable` 或独立只读快照二选一）。
2. 删除双写同步路径，避免后续维护分叉。

### D2. PathfindingHelper API 去歧义

涉及文件：

- `src/server/ecs/systems/pathfinding_helper.h`
- `src/server/ecs/systems/pathfinding_helper.cc`

目标：

1. 无 checker 重载标记弃用（`[[deprecated]]`）或重命名为 `FindPathStraightLine`。
2. 所有生产调用统一要求显式 walkable checker。

### D3. Client MapSystem API 收敛

涉及文件：

- `src/client/game/map/map_system.h`
- `src/client/game/map/map_system.cpp`

目标：

1. 保留单一公开命名风格（建议 snake_case）。
2. 兼容层通过短期 wrapper + `TODO(remove in N release)` 逐步下线。

### D4. ChunkManager / MapEventManager 定位

涉及文件：

- `src/server/game/map/chunk_manager.h`
- `src/server/game/map/chunk_manager.cc`
- `src/server/game/map/map_event_manager.h`
- `src/server/game/map/map_event_manager.cc`

决策门槛：

1. 若近期不接入主链路：标注实验态并降低维护成本。
2. 若要接入：补齐调用点、复杂度改造与完整测试后再上主链。

---

## 4. 交付拆分建议（PR 粒度）

1. PR-1：并发测试基线（Phase A）。
2. PR-2：MapInstance 原子操作修复（B1）。
3. PR-3：SceneManager 锁边界重构 + 安全接口（B2/B3）。
4. PR-4：随机卷轴去读盘（C1）。
5. PR-5~PR-8：P2 各专项（D1~D4）。

每个 PR 要求：

1. 只做一类问题，便于回归定位。
2. 附带最小复现测试。
3. 变更说明明确“行为保持不变”与“行为调整点”。

---

## 5. 验证与验收

建议最小验证命令：

```bash
cmake --build --preset vcpkg-wsl-debug --target legend2_tests -j$(nproc)
./build-wsl/bin/legend2_tests --gtest_filter='MapInstance*:*SceneManager*:*ScrollTeleport*:*MovementValidator*:*TeleportSystem*'
ctest --test-dir build-wsl --output-on-failure -R "map_|scene_|teleport_"
```

并发专项建议：

1. 新增并发用例单独循环 1000 次跑稳。
2. 条件允许时增加 TSAN 任务（夜测）。

总体验收标准：

1. P0/P1 用例全绿且 0 flaky。
2. 地图迁移、随机传送、实体移动主流程无行为回退。
3. 随机卷轴路径不再触发磁盘读取。

---

## 6. 风险与回滚

主要风险：

1. 锁边界重构引入新死锁或遗漏索引提交。
2. 接口收敛导致旧调用点遗漏。
3. 随机卷轴逻辑改造后出现落点分布偏差。

回滚策略：

1. 保持 PR 小步合并，逐个可回退。
2. 对关键行为加临时开关（仅过渡期使用，稳定后移除）。
3. 回滚优先级：先回滚行为改动 PR，保留测试 PR 作为守护网。

---

## 7. 执行顺序（建议）

1. 先合 PR-1（测试基线）。
2. 再合 PR-2 + PR-3（P0 一次性收敛）。
3. 然后合 PR-4（P1 性能止血）。
4. 最后按资源推进 P2 技术债。

该顺序可在不阻塞主功能迭代的前提下，先把稳定性和热路径风险降到可控范围。
