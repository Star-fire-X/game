---
name: "Phase E - 存储后端归并"
about: "执行 E 阶段：db 后端归并到 storage_engine/backends"
title: "[Phase E] 存储后端归并"
labels: "refactor,server-structure,phase-e,storage"
assignees: ""
---

## 目标

将 `db` 中被 `StorageEngine` 依赖的后端实现迁入统一存储域，收口依赖边界。

## 任务清单

- [ ] **E-01** 新建 `storage_engine/backends/` 目录层。  
  文件清单: `src/server/storage_engine/backends/` (new), `src/server/storage_engine/CMakeLists.txt`

- [ ] **E-02** 迁移 `storage_engine_backend.*`。  
  文件清单: `src/server/db/storage_engine_backend.h`, `src/server/db/storage_engine_backend.cc`, `src/server/storage_engine/backends/storage_engine_backend.h` (new), `src/server/storage_engine/backends/storage_engine_backend.cc` (new)

- [ ] **E-03** 迁移 `account_storage_backend.*`。  
  文件清单: `src/server/db/account_storage_backend.h`, `src/server/db/account_storage_backend.cc`, `src/server/storage_engine/backends/account_storage_backend.h` (new), `src/server/storage_engine/backends/account_storage_backend.cc` (new)

- [ ] **E-04** 迁移 `account_storage.*` 到 backends/common。  
  文件清单: `src/server/db/account_storage.h`, `src/server/db/account_storage.cc`, `src/server/storage_engine/backends/common/account_storage_codec.h` (new), `src/server/storage_engine/backends/common/account_storage_codec.cc` (new)

- [ ] **E-05** `LogicServer` 仅从 `storage_engine/*` 装配后端。  
  文件清单: `src/server/logic/logic_server.cc`, `src/server/logic/services/storage_login_service.cc`

- [ ] **E-06** 归并连接池到存储后端域（或抽成共享 db_core）。  
  文件清单: `src/server/db/pg_connection_pool.h`, `src/server/db/pg_connection_pool.cc`, `src/server/storage_engine/backends/postgres/pg_connection_pool.h` (new), `src/server/storage_engine/backends/postgres/pg_connection_pool.cc` (new)

- [ ] **E-07** 评估并处理 `redis_*`、`postgres_database`、`character_repository` 的主链路定位。  
  文件清单: `src/server/db/redis_manager.h`, `src/server/db/redis_manager.cc`, `src/server/db/redis_cache.h`, `src/server/db/redis_cache.cc`, `src/server/db/postgres_database.h`, `src/server/db/postgres_database.cc`, `src/server/db/character_repository.h`, `src/server/db/character_repository.cc`

- [ ] **E-08** 非主链路 DB 组件改可选目标，不进默认 `mir2_server_lib`。  
  文件清单: `src/server/CMakeLists.txt`, `tests/CMakeLists.txt`, `CMakeLists.txt`

- [ ] **E-09** 完成后删除 `src/server/db/`。  
  文件清单: `src/server/db/` (delete), `src/server/CMakeLists.txt`

## 验收标准

- [ ] `LogicServer` 不再 include `db/*backend*`。
- [ ] 登录与持久化链路回归通过。
- [ ] `db/` 目录按目标状态收敛（可选或删除）。

## 依赖关系

- 前置阶段: Phase D
- 后续阶段: Phase G

## 参考

- `docs/plans/2026-02-09-max-fix-checklist.md`

