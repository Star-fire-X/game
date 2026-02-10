# Server 结构整理与迁移方案（ECS 收口）

**文档版本**: v1.0  
**日期**: 2026-02-09  
**范围**: `src/server/*`

## 1. 背景与目标

当前服务端处于“ECS 新链路 + 旧实现并存”的过渡态，主要问题是：

- Handler 存在双层目录并行（`handlers/` 与 `logic/handlers/`）
- `legacy/` 仍参与编译且部分路径仍被主链路间接使用
- `combat/` 独立目录仅承载 `combat_core`，与 `ecs/systems` 关系紧耦合
- `game/entity` 与 ECS 管理职责交叉（特别是聊天/行会链路）
- 命名与目录卫生问题（空目录、`.cc/.cpp` 混用）

本方案目标：

1. 将运行时主链路明确收敛到 `logic + ecs + storage_engine + network`
2. 逐步下线 `legacy` 与旧目录，不做一次性大爆炸重构
3. 通过分阶段、可回滚、可验收方式完成目录与职责重整

## 2. 关键事实（作为约束）

### 2.1 必须修正的认知

- `db/` 与 `storage_engine/` 当前不是二选一关系。现状是：`db/*` 作为 `storage_engine` 的后端实现被 `LogicServer` 直接装配（`StorageEngineBackend + AccountStorageBackend`）。
- `kcp_upgrade_handler` 属于网络升级握手逻辑，应保留在网络域，不应迁入 `logic/services`。
- `world/role_store` 并非“极小占位文件”，已被登录/角色/战斗入口路径广泛依赖。

### 2.2 整理原则

- **先减编译入口，再改调用，再删代码**。
- 每阶段独立 PR，允许在任意阶段停止并保持可运行。
- 每次删旧代码前必须有替代链路的测试覆盖。

## 3. 目标结构（修正版）

```text
src/server/
├── apps/
├── core/
├── config/
├── network/
│   └── handlers/                 # KCP upgrade 等网络侧 handler
├── gateway/
├── logic/
│   ├── handlers/
│   ├── services/
│   └── events/
├── ecs/
│   ├── components/
│   ├── systems/                  # 含 combat_core
│   └── events/
├── game/                         # 地图/NPC/聊天等“内容逻辑”
├── storage_engine/
│   ├── interfaces/
│   ├── backends/                 # 可迁入 postgres/account backend（由 db/ 迁移而来）
│   ├── l1/
│   ├── l2/
│   └── persistence/
├── data/
├── log/
├── monitor/
└── security/
```

> 备注：`db/` 的目标是“后端实现归并到 storage_engine/backends”，不是直接删除并断链。

## 4. 分阶段执行计划

## Phase 0：基线与防护（1-2 天）

### 动作

- 补齐并固定当前主链路测试分组（logic/gateway/ecs/network）。
- 新增“目录收敛追踪”文档与删除清单（本文件即总计划）。

### 验收

- `legend2_tests` 可跑，至少保证以下集合通过：`logic_*`、`ecs_*`、`network_*`。
- 建立“删除前必须无引用”的 `rg` 检查命令清单。

---

## Phase 1：Handler 收敛（2-4 天）

### 动作

1. `src/server/handlers/client_registry.*` 迁入 `src/server/logic/services/`（或 `logic/runtime/`，二选一后固定）。
2. `src/server/handlers/movement/movement_validator.*` 迁入 `src/server/logic/handlers/movement/`。
3. `src/server/handlers/movement/entity_broadcast_service.*`  
   若仍无调用，先从默认编译源移除；若恢复使用，再放入 `logic/services/`。
4. `src/server/handlers/merchant_handler.*` 迁入 `logic/services/merchant_service.*`（命名改为 service，避免“handler”语义混乱）。
5. `src/server/handlers/network/kcp_upgrade_handler.*` 迁入 `src/server/network/handlers/`（仅改目录，不改职责）。
6. 删除空目录：`src/server/handlers/login`、`src/server/logic/handlers/combat`。

### 验收

- `src/server/CMakeLists.txt` 不再包含旧 `handlers/*` 路径。
- 全部 include 更新后编译通过，网络握手/移动/登录测试通过。

### 风险与回滚

- 风险：大规模 include 改动引发编译雪崩。  
- 回滚：按子步骤拆 PR（registry/movement/network/merchant），逐个可回退。

---

## Phase 2：战斗层级压平（2-3 天）

### 动作

1. 将 `src/server/combat/combat_core.*` 迁入 `src/server/ecs/systems/combat_core.*`。
2. 更新引用：
   - `config_manager.h`
   - `ecs/systems/combat_system.h`
   - `ecs/systems/damage_calculator.cc`
   - `logic/services/ecs_combat_service.cc`
3. 删除 `src/server/combat/` 目录与 CMake 条目。
4. 若需平滑期，可保留 1 个版本兼容头（转发 include，标记 deprecated）。

### 验收

- 战斗、技能、combat_core 独立测试通过。
- `rg "server/combat/combat_core.h"` 结果为 0。

---

## Phase 3：Legacy 退出机制（4-7 天）

### 动作

1. 引入 CMake 开关：`LEGEND2_ENABLE_LEGACY`（默认 `OFF`，过渡期可临时 `ON`）。
2. 把 `legacy/inventory_system.cpp`、`legacy/skill_system.cpp`、`legacy/monster_ai.cpp`、`legacy/legacy_monster_adapter.cc` 从默认编译集合移除。
3. 将 `legacy/character_factory.*` 升格为 ECS 正式模块：
   - 新路径建议：`src/server/ecs/persistence/character_codec.*`
   - `character_entity_manager` 改 include 新路径。
4. 全部引用切换完成后删除 `src/server/legacy/`。

### 验收

- 默认配置下不编译 `legacy/*`，且服务器可启动。
- `rg "legacy/" src/server` 仅剩文档注释或 0。

### 风险与回滚

- 风险：角色加载/保存链路受影响。  
- 回滚：保留 `LEGEND2_ENABLE_LEGACY=ON` 兜底，直到新模块稳定。

---

## Phase 4：`game/entity` 与 ECS 边界重划（3-5 天）

### 动作

1. 为聊天/行会建立 ECS 访问服务（例如 `logic/services/player_presence_service`），替代 `PlayerManager` 直接依赖。
2. `ChatHandler`、`GuildHandler` 改为依赖 ECS 实体标识与组件查询，不再使用 `game::entity::Player` wrapper。
3. `game/entity/player*`、`player_manager*` 进入废弃清单；无引用后删除。

### 验收

- `logic_server` 不再 include `game/entity/player_manager.h`。
- 聊天、行会测试回归通过。

---

## Phase 5：存储目录归并（2-4 天）

### 动作

1. 将 `db/storage_engine_backend.*`、`db/account_storage_backend.*` 迁入 `storage_engine/backends/`。
2. 将 `db/account_storage.*` 这类键编解码工具迁入 `storage_engine/backends/common/`（或 `storage_engine/utils/`）。
3. 明确 `db/` 仅保留“独立数据库访问层”或完全清空；建议最终清空并删除目录。

### 验收

- `LogicServer` 仅从 `storage_engine/*` 装配后端，不再 include `db/*backend*`。
- 登录链路与持久化链路测试通过。

---

## Phase 6：`world/` 归并（1-2 天）

### 动作

1. 将 `world/role_store.*`、`world/role_record.h` 迁至 `logic/services/session_role_store.*`（命名可微调，但放在 logic 域）。
2. 更新登录、角色、战斗 handler 引用。
3. 删除 `src/server/world/`。

### 验收

- `rg "world/role_store.h"` 为 0。
- 登录/角色相关测试通过。

---

## Phase 7：命名与目录卫生（持续）

### 动作

- 统一源码后缀到 `.cc`（先新文件遵守，旧文件分批改）。
- 清理空目录与无引用头文件。
- 在 CI 增加“空目录检测 + 禁止新增 `.cpp`（可选）”。

### 验收

- 目录无空壳模块。
- 代码树后缀策略一致。

## 5. 推荐 PR 切分

1. PR-1: Handler 收敛（不改行为，仅移动+include）  
2. PR-2: `combat_core` 迁移  
3. PR-3: `character_factory` 升格 + legacy 默认下线  
4. PR-4: chat/guild 去 `PlayerManager` 依赖  
5. PR-5: 存储后端归并至 `storage_engine/backends`  
6. PR-6: `world/` 归并 + 目录清理  

## 6. Done 定义（最终状态）

- 默认构建不包含 `legacy/`、旧 `handlers/`、`combat/`、`world/`。
- 目录职责单一：网络升级在 `network`，业务处理在 `logic`，状态与规则在 `ecs`。
- 存储层结构清晰：`storage_engine` 为唯一入口，后端实现在其子目录内。
- 关键测试组全部通过，且无明显功能回归。

## 7. 执行建议（本周可启动）

先做 **Phase 1 + Phase 2**（收益最大、风险可控）。  
完成后再进入 legacy 退出与实体边界重划，避免一次性改动过大。

