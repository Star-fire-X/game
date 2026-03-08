# Logic Services 代码评审整改与优化方案

日期: 2026-02-13  
范围: `src/server/logic/services/*`

## 1. 目标

1. 修复已确认的逻辑正确性问题，避免角色/物品状态污染。
2. 降低服务层并发与状态管理风险。
3. 在不破坏现有行为的前提下，提升可维护性与可观测性。

## 2. 优先级总览

## P0（本周必须落地）

1. 查询路径误用 `GetOrCreate`，导致幽灵角色创建与持久化。
2. `FindGroundItem` 直接实体ID快速路径缺失 `instance_id` 校验。
3. `SellItem` 库存回补 `stock += count` 溢出风险。

## P1（本迭代建议完成）

1. `PickupItem` 关键物品持久化在实体销毁后可能被静默跳过。
2. `OpenShopMap` 全局静态状态设计（线程与跨world污染风险）。
3. `BindClientRole` 缺少“被顶号旧连接”的通知与清理协作。

## P2（技术债与性能优化）

1. `ToLegacyError` 重复实现。
2. `merchant_service.cc` 异常路径使用 `std::cerr`。
3. 负缓存每次查询全量清理导致锁竞争。
4. `FindPlayerIdByName` 线性扫描优化（按在线规模择机）。

## 3. 逐项整改建议

## 3.1 P0-1: `GetOrCreate` 查询误用

涉及文件:
- `src/server/logic/services/ecs_combat_service.cc:87`
- `src/server/logic/services/ecs_combat_service.cc:169`
- `src/server/logic/services/ecs_inventory_service.cc:233`
- `src/server/logic/services/ecs_inventory_service.cc:309`
- `src/server/logic/services/ecs_inventory_service.cc:383`

问题说明:
- `CharacterEntityManager::GetOrCreate` 在不存在时会创建默认角色并触发保存，查询接口出现副作用。

修复建议:
1. 服务层改为“纯查询”路径: `TryGet` + `TryGetRegistry`，不存在直接返回业务错误码。
2. 新增辅助函数（建议放在 service 文件匿名命名空间）统一解析角色:
   - 输入 `character_id`
   - 输出 `(entt::registry*, entt::entity)` 或 `std::nullopt`
3. 禁止在 `Attack/UseSkill/PickupItem/UseItem/DropItem` 这类请求处理中调用 `GetOrCreate`。

测试建议:
1. 请求不存在 `character_id` 时，不应新增 `CharacterIdentityComponent` 实体。
2. 请求不存在角色后，存储层不应新增该角色记录。
3. 回归 `Attack/UseSkill/PickupItem` 正常路径。

---

## 3.2 P0-2: `FindGroundItem` 快速路径误命中

涉及文件:
- `src/server/logic/services/ecs_inventory_service.cc:149`
- `src/server/logic/services/ecs_inventory_service.cc:156`

问题说明:
- 快速路径按 `item_id -> entt::entity` 直接试探，`is_candidate` 未校验 `item.instance_id == item_id`，可误返回错误物品实体。

修复建议（二选一，推荐A）:
1. A方案: 在 `is_candidate` 内增加 `instance_id` 比对。
2. B方案: 删除 direct 快速路径，统一走遍历匹配 `instance_id`。

建议实现:
- 优先 A，兼顾性能与正确性。

测试建议:
1. 构造 `entity_id == 请求item_id` 但 `instance_id != 请求item_id`，应返回未找到或正确实体。
2. 地图过滤与背包/装备过滤场景回归。

---

## 3.3 P0-3: `SellItem` 库存回补溢出

涉及文件:
- `src/server/logic/services/merchant_service.cc:400`

问题说明:
- `selected_item->stock += count` 缺少上界检查，可能溢出为负值。

修复建议:
1. 在加法前检查 `selected_item->stock > INT_MAX - count`，溢出则拒绝交易并记录告警日志。
2. 如业务允许，改用“饱和上限”策略（例如钳制到 `INT_MAX`），但必须与策划规则对齐。

测试建议:
1. `stock = INT_MAX-1, count=2` 交易应失败（或按策略钳制）。
2. 正常小数量卖出不受影响。

## 3.4 P1-1: `PickupItem` 关键持久化窗口

涉及文件:
- `src/server/logic/services/ecs_inventory_service.cc:275`

问题说明:
- `InventorySystem::PickupItem` 后再从 `ground_item` 读取组件，实体若已销毁将跳过关键保存。

修复建议:
1. 在调用 `InventorySystem::PickupItem` 前缓存 `ItemComponent` 副本（仅用于关键性判定与日志）。
2. 拾取成功后使用缓存副本调用 `TryPersistCriticalPickup`。

测试建议:
1. 可堆叠物品被并入导致原实体消失时，关键保存逻辑仍执行。

## 3.5 P1-2: `OpenShopMap` 状态管理

涉及文件:
- `src/server/logic/services/merchant_service.cc:107`
- `src/server/logic/services/merchant_service.cc:147`
- `src/server/logic/services/merchant_service.cc:346`

问题说明:
- 进程级静态 `unordered_map` 带来潜在并发问题；且 key 只用 `entity` 整数，跨 world/registry 存在污染可能。

修复建议:
1. 将 `OpenShopMap` 收敛为 `MerchantService` 成员变量（实例内状态）。
2. key 改为稳定玩家ID（`CharacterIdentityComponent::id`），避免 `entt::entity` 重用污染。
3. 若后续确认跨线程访问，补充互斥或限定单线程调用契约并加断言。

测试建议:
1. 同一玩家跨场景实体重建后仍能正确匹配打开商店状态。
2. 多玩家并发买卖场景不串店。

## 3.6 P1-3: `BindClientRole` 顶号清理

涉及文件:
- `src/server/logic/services/session_role_store.cc:38`

问题说明:
- 当前是“后登录踢前登录”策略，但仅修改映射，缺少对旧会话的主动通知/断链协作。

修复建议:
1. `BindClientRole` 返回被替换的旧 `client_id`（可选值）。
2. 上层 handler 收到后发送“账号在别处登录”并主动关闭旧连接。
3. 增加审计日志: `player_id/new_client/old_client`。

测试建议:
1. 同角色双端登录，旧连接收到踢线消息并断开。
2. 新连接保持有效映射。

## 3.7 P2 技术债与性能项

1. `ToLegacyError` 提取公共函数  
涉及: `src/server/logic/services/ecs_combat_service.cc:21`、`src/server/logic/services/ecs_inventory_service.cc:26`

2. 异常日志统一到 `SYSLOG_*`  
涉及: `src/server/logic/services/merchant_service.cc:263`

3. 负缓存剪枝降频  
涉及: `src/server/logic/services/storage_login_service.cc:162`、`src/server/logic/services/storage_login_service.cc:192`  
建议:
- 引入 `next_prune_at`（例如每 1 秒或每 N 次查询清理一次）。
- 或采用分桶TTL结构，避免每次 O(N) 扫描。

4. `FindPlayerIdByName` 建立可选索引  
涉及: `src/server/logic/services/player_presence_service.cc:56`  
建议:
- 在线人数 > 阈值（如 5k）时启用 `name -> player_id` 索引；小规模保持线性扫描。

## 4. 实施顺序与排期

## 阶段A（1-2天，风险止血）

1. 修复 `GetOrCreate` 查询误用。
2. 修复 `FindGroundItem` 快速路径。
3. 修复 `SellItem` 库存溢出检查。

验收门槛:
- 新增单测全部通过。
- `legend2_tests` 对应模块回归通过。

## 阶段B（2-3天，行为一致性）

1. `PickupItem` 关键持久化前置缓存。
2. `BindClientRole` 增加旧连接回收协作。
3. `OpenShopMap` 实例化与 key 稳定化。

验收门槛:
- 顶号与商店流程集成测试通过。
- 无新增跨线程告警/断言失败。

## 阶段C（持续优化）

1. 提取 `ToLegacyError` 公共工具。
2. 统一日志风格。
3. 负缓存与在线名查找性能优化。

## 5. 回归与验证矩阵

必测清单:
1. 战斗: `Attack/UseSkill` 目标不存在、攻击者不存在、跨map目标。
2. 背包: 拾取、使用、丢弃，含不存在角色/物品、背包满、堆叠合并。
3. 商店: 买入/卖出、库存上下界、打开商店后卖出定价一致性。
4. 会话: 重复登录顶号、旧连接消息与断链。
5. 登录: 负缓存命中/过期/高频压力。

建议测试文件（新增或扩展）:
1. `tests/server/logic/ecs_inventory_service_test.cc`
2. `tests/server/logic/ecs_combat_service_test.cc`
3. `tests/server/logic/merchant_service_test.cc`
4. `tests/server/logic/session_role_store_test.cc`
5. `tests/server/logic/storage_login_service_test.cc`

## 6. 风险与回滚

1. 角色查询改 `TryGet` 后，历史上依赖“隐式创建角色”的路径可能暴露为失败，需要同步排查调用方。
2. 商店状态key切换为 `player_id` 后，旧状态不会自动迁移，建议版本窗口内允许回退逻辑并打印兼容日志。
3. 顶号主动断链属于行为变更，需在公告/运维SOP中明确。

回滚建议:
1. P0 修复可按文件粒度回滚，不建议整批回退。
2. 顶号通知与断链可加 feature flag（默认开）便于灰度控制。

## 7. 交付物定义

1. 代码修复PR（按阶段拆分，避免大PR）。
2. 每阶段一份测试结果摘要（失败用例、修复commit、最终通过率）。
3. 最终“整改完成报告”更新至 `docs/`。
