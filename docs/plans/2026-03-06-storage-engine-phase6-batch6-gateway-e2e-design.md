# StorageEngine Phase6 Batch6 Gateway E2E Design

## Goal

把 Phase6 演练从 `mir2_logic + PostgreSQL` 进程级继续抬高到
`mock client -> mir2_gateway -> mir2_logic -> StorageEngine -> PostgreSQL`
端到端链路，并且在同一条业务路径里覆盖一次真实存储读和一次真实存储写。

## Scope

本批只新增一条最小健康业务链路：

1. mock client 连接 `mir2_gateway`
2. 发送 `LoginReq`
3. 发送 `SelectRoleReq`
4. 发送 `MoveReq`
5. 主动断开连接
6. 离线验收 `char:<player_id>` 已发生真实回写

继续保留前一批已有的：

1. `startup_validation`
2. `checkpoint_restore`

但本批新增的 gateway 端到端场景只做健康读写链路，不把损坏注入和 checkpoint restore 再重复拉一遍。

## Why This Path

`login` 会经过 `StorageLoginService` 真实读取
`account:username:<name>`；
`select_role` 后，`move` 会把角色状态标脏；
客户端断开后，`CharacterEntityManager::OnDisconnect()` 会触发
`SaveIfDirty()`，最终把 `char:<player_id>` 写回 `StorageEngine`。

这比 `CreateRole` 更合适，因为当前 `CreateRole` 主要更新内存 `RoleStore`，
并不会形成稳定的真实存储写路径。

## Architecture

新增一条 gateway e2e drill 资产链：

1. `mir2_storage_engine_phase6_fault_driver`
   负责预置账号记录和角色快照。
2. `mir2_storage_engine_phase6_gateway_mock_client`
   负责连接 gateway、发送登录/选角/移动请求、等待响应、断开连接。
3. `run_storage_engine_phase6_gateway_e2e_drill.sh`
   负责编排 `mir2_logic`、`mir2_gateway`、mock client、`mir2_storage_admin`。
4. `run_storage_engine_phase6_gateway_e2e_gate.sh`
   负责读取 client report / logic log / validate 结果并做门禁判定。

## Acceptance

通过条件：

1. `LoginRsp.code == ERR_OK`
2. `SelectRoleRsp.code == ERR_OK`
3. `MoveRsp.code == ERR_OK`
4. mock client 报告 `disconnect_sent=true`
5. `storage_admin validate` 返回 `total_corrupted=0`
6. 离线读取 `char:<player_id>` 快照时，可观察到移动后的坐标已持久化

## Files

计划新增：

1. `src/server/apps/storage_engine_phase6_gateway_mock_client.cc`
2. `src/server/apps/storage_engine_phase6_gateway_mock_client_main.cc`
3. `scripts/run_storage_engine_phase6_gateway_e2e_drill.sh`
4. `scripts/run_storage_engine_phase6_gateway_e2e_gate.sh`
5. `tests/server/storage_engine/phase6_gateway_e2e_shell_test.cc`
6. `docs/storage_engine_phase6_gateway_e2e_runbook.md`

## Non-Goals

1. 不做真实客户端
2. 不做交易、背包、拍卖等复杂业务写路径
3. 不做 `gateway + logic` 多客户端并发演练
4. 不把 `startup_validation` / `checkpoint_restore` 再扩成 gateway 端到端版本
