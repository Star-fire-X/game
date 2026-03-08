# ECS 组件整改与优化计划（2026-02-14）

## 1. 目标与范围

- 范围：`src/server/ecs/components/*.h`，以及直接依赖这些组件的系统与迁移代码。
- 目标：
  - 先修复稳定性风险（类型截断、sentinel 初始化、未使用字段）。
  - 再收敛数据模型（物品/背包双模型并存）。
  - 最后做性能优化（热路径查询、分配与线性扫描）。

## 2. 已确认问题（按优先级）

### P0 必修（高优先级）

1. `entt::null` 初始化不一致，存在槽位空值判定风险。  
   - `src/server/ecs/components/storage_component.h`
   - `src/server/ecs/components/trade_component.h`
2. `ActiveEffect::source_entity` 使用 `uint32_t`，与 `ENTT_ID_TYPE=std::uint64_t` 存在截断风险。  
   - `src/server/ecs/components/effect_component.h`
   - `src/server/ecs/systems/skill_system.cc`
   - `src/server/ecs/systems/combat_system.cc`
3. `MonsterAggroComponent::hate_clear_time` 目前未落地使用。  
   - `src/server/ecs/components/monster_component.h`
4. `pk_component.h` 使用 `std::remove_if` 但缺少 `<algorithm>` 显式依赖。  
   - `src/server/ecs/components/pk_component.h`

### P1 架构收敛（中优先级）

1. 物品模型“双轨并存”：  
   - 运行时：`ItemComponent + InventoryOwnerComponent + EquipmentSlotComponent + StorageComponent`
   - 兼容快照：`InventoryComponent(std::optional<ItemData>)`
2. 行会 rank 结构使用成员名（`member_names`），改名后同步成本高。  
   - `src/server/ecs/components/guild_component.h`
   - `src/server/ecs/systems/guild_system.cc`

### P2 统一治理（中低优先级）

1. `account_id` 在 ECS/持久化多处为 `std::string`，与网络层 `uint64_t` 并存。
2. 行会内实体引用与业务 ID 混用，需要明确边界（世界内实体 vs 跨世界稳定 ID）。

## 3. 分阶段实施计划

## Phase P0（1-2 天）：稳定性修复与护栏

### 3.1 代码改造项

1. 统一空槽初始化为 `entt::null`
   - `StorageComponent::slots` 显式 `fill(entt::null)` 初始化。
   - `TradeComponent::offered_items` 显式 `fill(entt::null)` 初始化。
2. 统一 effect 来源实体类型
   - 方案：`ActiveEffect::source_entity` 改为 `entt::entity`。
   - 同步调整所有赋值与读取路径，删除 `static_cast<uint32_t>(entity)` 写法。
3. 落地仇恨超时清理
   - 在 `MonsterAggroComponent` 增加“上次仇恨更新时间戳”或“按目标活跃时间戳”。
   - 在 `DecayHatred()` 内新增超时清理分支，实际使用 `hate_clear_time`。
4. 显式补齐头文件依赖
   - `pk_component.h` 增加 `<algorithm>`。
5. 增加编译期/运行期护栏
   - `static_assert(sizeof(entt::entity) >= sizeof(uint32_t))`（若保留历史字段）。
   - 对关键组件增加最小单测，防止回归。

### 3.2 验收标准

1. 组件单测通过：
   - Storage/Trade 空槽默认值为 `entt::null`。
   - Effect source 可往返保存实体值。
   - Monster 仇恨超时后可清空。
2. 回归测试通过：
   - `inventory_system`、`storage_system`、`trade_system`、`skill_system` 关键路径不回退。

## Phase P1（3-5 天）：物品模型收敛与兼容收口

### 3.3 目标策略

- 以“实体化物品模型”为唯一运行时真相。
- `InventoryComponent` 降级为“兼容快照/边界转换结构”，避免参与运行时业务判断。

### 3.4 代码改造项

1. 明确组件职责并文档化
   - `ItemComponent`：实例数据真相。
   - `InventoryOwnerComponent/EquipmentSlotComponent/StorageComponent/TradeComponent`：关系真相。
   - `InventoryComponent`：仅 JSON/存档边界兼容。
2. 迁移路径
   - 保留 `inventory_migration` 双向转换，但禁止新业务逻辑直接读 `InventoryComponent`。
   - 通过注释、命名或 `deprecated` 标记明确“兼容层”属性。
3. 收敛 Dirty 标记语义
   - 逐步弱化 `inventory_dirty` 总开关，统一以 `items/equipment/skills` 三类标记驱动。

### 3.5 验收标准

1. 运行时模块（背包/装备/仓库/交易/掉落）不依赖 `InventoryComponent` 进行判定。
2. 存档读写结果与现网格式兼容（JSON round-trip 不丢字段）。
3. 迁移日志中重复槽位、非法槽位告警可观测且不崩溃。

## Phase P2（3-4 天）：ID 策略统一与社交组件治理

### 3.6 目标策略

- 规则分层：
  - 世界内短期引用：`entt::entity`
  - 跨世界/跨会话稳定标识：`uint32_t/uint64_t` 业务 ID
- 不强推一步到位替换 `account_id`，采用“类型别名 + 适配层”渐进迁移。

### 3.7 代码改造项

1. 定义统一 ID 别名（示例）
   - `AccountId`、`CharacterId`、`GuildId`。
2. 行会 rank 数据结构治理
   - `GuildRank::member_names` 逐步迁移为稳定 ID（建议 `character_id`）。
   - 展示名在渲染/查询层动态解析，避免改名同步问题。
3. 统一行会关系语义
   - 成员实体列表保留 `entt::entity`（世界内操作）。
   - 战争/同盟维持 `GuildId`（跨世界稳定）。
   - 文档中明确“禁止混用场景”。

### 3.8 验收标准

1. 改名后 rank 映射不失效。
2. 行会战争/同盟逻辑与成员管理逻辑职责清晰，无双向隐式转换。

## Phase P3（可并行，2-3 天）：性能优化

### 3.9 优化方案

1. `GuildComponent::IsMember` / `PartyComponent::IsMember`
   - 增加缓存集合（`unordered_set<entt::entity>`）或统一通过系统级索引查询。
2. `EffectListComponent` 分类访问
   - 提供回调/迭代接口，减少每次返回 vector 的分配。
3. `MonsterAggroComponent`
   - 大规模仇恨场景下优化“最高仇恨目标”维护策略，减少重复遍历。
4. 观测指标
   - 组件层热路径耗时（combat/effect/guild/trade）。
   - per-tick 分配次数与容器容量变化。

### 3.10 验收标准

1. 压测场景下 tick p95/p99 不回退，至少持平。
2. 热路径分配次数下降（effect/guild 相关路径）。

## 4. 风险与回滚

1. 风险：`source_entity` 类型切换会影响序列化/网络映射。  
   - 预案：保留过渡适配函数，先灰度到内部逻辑路径。
2. 风险：Inventory 兼容层收敛可能影响旧存档。  
   - 预案：保留 `inventory_migration` 双向通道一个版本周期。
3. 风险：社交组件 ID 策略调整影响面广。  
   - 预案：先落别名和转换边界，再迁移数据结构。

## 5. 执行清单（建议）

1. 本周先完成 P0（稳定性修复）并补齐对应单测。
2. 下周推进 P1（物品模型收敛）与 P2（行会/ID 策略）中的低风险子项。
3. 最后在压测环境执行 P3，并以指标结果决定是否全量启用。
