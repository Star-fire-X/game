# 2026-02-09 最大修复清单（含文件清单）

**状态**: Draft  
**适用范围**: `src/server/*`  
**执行方式**: 按编号逐项勾选，建议每 3-6 项一个 PR。  

---

## A. 先止血（构建与开关）

- [ ] **A-01** 新增 `LEGEND2_ENABLE_LEGACY`（默认 `OFF`），控制 `src/server/legacy/*` 编译。  
  文件清单: `CMakeLists.txt`, `src/server/CMakeLists.txt`, `tests/CMakeLists.txt`

- [ ] **A-02** 将 legacy 源文件从默认 `MIR2_SERVER_SOURCES` 移出，仅在开关开启时编译。  
  文件清单: `src/server/CMakeLists.txt`

- [ ] **A-03** 保留 `LEGEND2_ENABLE_LEGACY=ON` 兜底路径（短期回滚开关）。  
  文件清单: `CMakeLists.txt`, `src/server/CMakeLists.txt`

- [ ] **A-04** 在 CI 增加“默认构建不得包含 legacy 源”的断言。  
  文件清单: `.github/workflows/docker-build.yml`, `.github/workflows/benchmark.yml`, `scripts/check_build_sources.sh` (new)

- [ ] **A-05** 清理 `src/server/CMakeLists.txt` 中“注释与行为不一致”项。  
  文件清单: `src/server/CMakeLists.txt`

- [ ] **A-06** 固化“目录迁移拆小 PR”提交规范。  
  文件清单: `AGENTS.md`, `README.md`, `docs/plans/2026-02-09-server-structure-consolidation-plan.md`

- [ ] **A-07** 固化基线测试组：`logic_*`、`ecs_*`、`network_*`。  
  文件清单: `tests/CMakeLists.txt`, `scripts/run-core-tests.sh` (new)

- [ ] **A-08** 固化基线启动验证：`mir2_logic` 初始化与 handler 注册检查。  
  文件清单: `src/server/apps/logic_main.cc`, `src/server/logic/logic_server.cc`, `tests/integration/logic_bootstrap_test.cc` (new)

---

## B. Handler 收敛（双层目录问题）

- [x] **B-01** 迁移 `client_registry` 到 logic 域（建议 `logic/services`）。  
  文件清单: `src/server/handlers/client_registry.h`, `src/server/handlers/client_registry.cc`, `src/server/logic/services/client_registry.h` (new), `src/server/logic/services/client_registry.cc` (new), `src/server/logic/logic_server.h`, `src/server/logic/logic_server.cc`

- [x] **B-02** 迁移 `movement_validator` 到 `logic/handlers/movement/`。  
  文件清单: `src/server/handlers/movement/movement_validator.h`, `src/server/handlers/movement/movement_validator.cc`, `src/server/logic/handlers/movement/movement_validator.h` (new), `src/server/logic/handlers/movement/movement_validator.cc` (new), `src/server/logic/handlers/movement/movement_handler.h`, `src/server/logic/handlers/movement/movement_handler.cc`

- [x] **B-03** 迁移 `merchant_handler` 到 `logic/services/merchant_service`。  
  文件清单: `src/server/handlers/merchant_handler.h`, `src/server/handlers/merchant_handler.cc`, `src/server/logic/services/merchant_service.h` (new), `src/server/logic/services/merchant_service.cc` (new)

- [x] **B-04** 迁移 `kcp_upgrade_handler` 到 `network/handlers/`（仅改目录，不改职责）。  
  文件清单: `src/server/handlers/network/kcp_upgrade_handler.h`, `src/server/handlers/network/kcp_upgrade_handler.cc`, `src/server/network/handlers/kcp_upgrade_handler.h` (new), `src/server/network/handlers/kcp_upgrade_handler.cc` (new), `src/server/network/dual_channel_manager.cc`

- [x] **B-05** 评估 `entity_broadcast_service`：无引用则先移出默认编译。  
  文件清单: `src/server/handlers/movement/entity_broadcast_service.h`, `src/server/handlers/movement/entity_broadcast_service.cc`, `src/server/CMakeLists.txt`

- [x] **B-06** 批量改 include 路径（logic/network/tests）。  
  文件清单: `src/server/logic/logic_server.h`, `src/server/logic/handlers/chat/chat_handler.h`, `src/server/logic/handlers/guild/guild_handler.h`, `src/server/logic/handlers/character/character_handler.h`, `src/server/logic/handlers/login/login_handler.h`, `src/server/network/dual_channel_manager.cc`, `tests/server/network/kcp_upgrade_handler_test.cc`, `tests/server/logic/login_handler_test.cc`, `tests/server/logic/chat_handler_test.cc`, `tests/server/guild/guild_handler_test.cc`

- [x] **B-07** 从 CMake 移除旧 `handlers/*` 路径。  
  文件清单: `src/server/CMakeLists.txt`, `src/server/logic/handlers/CMakeLists.txt`

- [x] **B-08** 删除空目录 `src/server/handlers/login`。  
  文件清单: `src/server/handlers/login/` (delete)

- [x] **B-09** 删除空目录 `src/server/logic/handlers/combat`。  
  文件清单: `src/server/logic/handlers/combat/` (delete)

- [x] **B-10** 删除旧 `src/server/handlers/` 根目录（确认无引用后）。  
  文件清单: `src/server/handlers/` (delete), `src/server/CMakeLists.txt`, `tests/CMakeLists.txt`

---

## C. 战斗链路压平

- [x] **C-01** 迁移 `combat_core.*` 到 `ecs/systems/`。  
  文件清单: `src/server/combat/combat_core.h`, `src/server/combat/combat_core.cpp`, `src/server/ecs/systems/combat_core.h` (new), `src/server/ecs/systems/combat_core.cc` (new)

- [x] **C-02** 更新 `config_manager.h` 的 include。  
  文件清单: `src/server/config/config_manager.h`

- [x] **C-03** 更新 `combat_system.h` 的 include。  
  文件清单: `src/server/ecs/systems/combat_system.h`

- [x] **C-04** 更新 `damage_calculator.cc` 的 include。  
  文件清单: `src/server/ecs/systems/damage_calculator.cc`

- [x] **C-05** 更新 `ecs_combat_service.cc` 的 include。  
  文件清单: `src/server/logic/services/ecs_combat_service.cc`

- [x] **C-06** 删除 `src/server/combat/` 目录和 CMake 条目。  
  文件清单: `src/server/CMakeLists.txt`, `src/server/combat/` (delete)

- [x] **C-07** 补充迁移后的回归测试执行脚本。  
  文件清单: `tests/CMakeLists.txt`, `tests/server/combat_core_test.cpp`, `scripts/run-combat-regression.sh` (new)

---

## D. Legacy 退场（核心）

- [x] **D-01** 将 `legacy/character_factory.*` 升格为 ECS 正式模块。  
  文件清单: `src/server/legacy/character_factory.h`, `src/server/legacy/character_factory.cc`, `src/server/ecs/persistence/character_codec.h` (new), `src/server/ecs/persistence/character_codec.cc` (new)

- [x] **D-02** `character_entity_manager` 改用新路径。  
  文件清单: `src/server/ecs/character_entity_manager.cc`, `src/server/ecs/character_entity_manager.h`

- [x] **D-03** 下线 `legacy/inventory_system.cpp` 默认编译。  
  文件清单: `src/server/CMakeLists.txt`, `src/server/legacy/inventory_system.cpp`, `src/server/legacy/inventory_system.h`

- [x] **D-04** 下线 `legacy/skill_system.cpp` 默认编译。  
  文件清单: `src/server/CMakeLists.txt`, `src/server/legacy/skill_system.cpp`, `src/server/legacy/skill_system.h`

- [x] **D-05** 下线 `legacy/monster_ai.cpp` 默认编译。  
  文件清单: `src/server/CMakeLists.txt`, `src/server/legacy/monster_ai.cpp`, `src/server/legacy/monster_ai.h`

- [x] **D-06** 下线 `legacy/legacy_monster_adapter.cc` 默认编译。  
  文件清单: `src/server/CMakeLists.txt`, `src/server/legacy/legacy_monster_adapter.cc`, `src/server/legacy/legacy_monster_adapter.h`

- [x] **D-07** 迁移或替换 legacy 专属测试，补齐 ECS 等价覆盖。  
  文件清单: `tests/server/ecs/character_codec_test.cc` (rename from `tests/server/character_factory_test.cc`), `tests/server/ecs/character_entity_manager_test.cpp`, `tests/server/ecs/skill_system_test.cc`, `tests/server/ecs/monster_ai_system_test.cc`, `tests/CMakeLists.txt`

- [x] **D-08** 清理所有 `legacy/` include 与路径引用。  
  文件清单: `src/server/CMakeLists.txt`（移除 `legacy/*` 编译入口）, `src/server/ecs/character_entity_manager.cc`（验证无 `legacy/` 引用）, `tests/CMakeLists.txt`（验证无 `legacy/` 引用）

- [x] **D-09** 删除 `src/server/legacy/` 目录。  
  文件清单: `src/server/legacy/legacy_monster_adapter.h`, `src/server/legacy/legacy_monster_adapter.cc`, `src/server/legacy/inventory_system.h`, `src/server/legacy/inventory_system.cpp`, `src/server/legacy/character.h`, `src/server/legacy/monster_ai.h`, `src/server/legacy/monster_ai.cpp`, `src/server/legacy/monster.h`, `src/server/legacy/skill_system.cpp`, `src/server/legacy/skill_system.h` (all delete)

---

## E. 存储层归并（`db` -> `storage_engine/backends`）

- [x] **E-01** 新建 `storage_engine/backends/` 目录层。  
  文件清单: `src/server/storage_engine/backends/.gitkeep` (new), `src/server/storage_engine/CMakeLists.txt`

- [x] **E-02** 迁移 `storage_engine_backend.*`。  
  文件清单: `src/server/db/storage_engine_backend.h` (delete), `src/server/db/storage_engine_backend.cc` (delete), `src/server/storage_engine/backends/storage_engine_backend.h` (new), `src/server/storage_engine/backends/storage_engine_backend.cc` (new), `src/server/logic/logic_server.cc`, `src/server/CMakeLists.txt`

- [x] **E-03** 迁移 `account_storage_backend.*`。  
  文件清单: `src/server/db/account_storage_backend.h` (delete), `src/server/db/account_storage_backend.cc` (delete), `src/server/storage_engine/backends/account_storage_backend.h` (new), `src/server/storage_engine/backends/account_storage_backend.cc` (new), `src/server/logic/logic_server.cc`, `src/server/CMakeLists.txt`, `tests/server/db/account_storage_backend_test.cpp`

- [x] **E-04** 迁移 `account_storage.*` 到 backends/common。  
  文件清单: `src/server/db/account_storage.h` (delete), `src/server/db/account_storage.cc` (delete), `src/server/storage_engine/backends/common/account_storage_codec.h` (new), `src/server/storage_engine/backends/common/account_storage_codec.cc` (new), `src/server/storage_engine/backends/account_storage_backend.cc`, `src/server/logic/services/storage_login_service.cc`, `src/server/CMakeLists.txt`, `tests/server/storage_engine_test.cc`, `tests/server/db/account_storage_backend_test.cpp`

- [x] **E-05** `LogicServer` 仅从 `storage_engine/*` 装配后端。  
  文件清单: `src/server/logic/logic_server.cc`, `src/server/logic/services/storage_login_service.cc`（`db/*` include 已切换为 `storage_engine/backends/*`）

- [x] **E-06** 归并连接池到存储后端域（或抽成共享 db_core）。  
  文件清单: `src/server/db/pg_connection_pool.h` (delete), `src/server/db/pg_connection_pool.cc` (delete), `src/server/storage_engine/backends/postgres/pg_connection_pool.h` (new), `src/server/storage_engine/backends/postgres/pg_connection_pool.cc` (new), `src/server/storage_engine/backends/storage_engine_backend.h`, `src/server/storage_engine/backends/account_storage_backend.h`, `src/server/storage_engine/backends/postgres/postgres_database.h`, `src/server/CMakeLists.txt`

- [x] **E-07** 评估并处理 `redis_*`、`postgres_database`、`character_repository` 的主链路定位。  
  文件清单: `src/server/CMakeLists.txt`（主链路仅保留 `storage_engine/backends/*`；`storage_engine/backends/postgres/postgres_database.cc`、`storage_engine/backends/repository/character_repository.cc`、`storage_engine/backends/redis/redis_cache.cc`、`storage_engine/backends/redis/redis_manager.cc` 归类为可选链路）

- [x] **E-08** 非主链路 DB 组件改可选目标，不进默认 `mir2_server_lib`。  
  文件清单: `src/server/CMakeLists.txt`（新增 `mir2_db_optional`，并从 `mir2_server_lib` 移出非主链路 DB 源）, `tests/CMakeLists.txt`（`legend2_tests` 按条件链接 `mir2_db_optional`）

- [x] **E-09** 完成后删除 `src/server/db/`。  
  文件清单: `src/server/db/` (delete), `src/server/storage_engine/backends/postgres/postgres_database.h` (new), `src/server/storage_engine/backends/postgres/postgres_database.cc` (new), `src/server/storage_engine/backends/repository/character_repository.h` (new), `src/server/storage_engine/backends/repository/character_repository.cc` (new), `src/server/storage_engine/backends/redis/redis_cache.h` (new), `src/server/storage_engine/backends/redis/redis_cache.cc` (new), `src/server/storage_engine/backends/redis/redis_manager.h` (new), `src/server/storage_engine/backends/redis/redis_manager.cc` (new), `src/server/CMakeLists.txt`, `tests/server/db/postgres_database_test.cpp`, `tests/server/db/redis_cache_test.cpp`, `tests/server/db/character_repository_test.cpp`, `tests/integration/db_integration_test.cpp`

---

## F. `game/entity` 与 ECS 边界重划

- [x] **F-01** 新建 `player_presence_service`（ECS 访问层）。  
  文件清单: `src/server/logic/services/player_presence_service.h` (new), `src/server/logic/services/player_presence_service.cc` (new), `src/server/logic/CMakeLists.txt`

- [x] **F-02** `ChatHandler` 去 `PlayerManager` 依赖。  
  文件清单: `src/server/logic/handlers/chat/chat_handler.h`, `src/server/logic/handlers/chat/chat_handler.cc`, `src/server/game/chat/chat_service.h`, `src/server/game/chat/chat_service.cc`, `src/server/logic/logic_server.h`, `src/server/logic/logic_server.cc`, `tests/server/logic/chat_handler_test.cc`

- [x] **F-03** `GuildHandler` 去 `PlayerManager` 依赖。  
  文件清单: `src/server/logic/handlers/guild/guild_handler.h`, `src/server/logic/handlers/guild/guild_handler.cc`, `src/server/logic/logic_server.cc`

- [x] **F-04** 将聊天/行会相关 ID 与在线状态查询统一迁到 ECS 组件查询。  
  文件清单: `src/server/ecs/components/character_components.h`, `src/server/ecs/components/guild_component.h`, `src/server/ecs/components/party_component.h`, `src/server/game/chat/chat_service.cc`

- [x] **F-05** 下线 `game/entity/player.*`（无引用后）。  
  文件清单: `src/server/game/entity/player.h`, `src/server/game/entity/player.cc`

- [x] **F-06** 下线 `game/entity/player_manager.*`（无引用后）。  
  文件清单: `src/server/game/entity/player_manager.h`, `src/server/game/entity/player_manager.cc`

- [x] **F-07** `logic_server` 不再 include `player_manager.h`。  
  文件清单: `src/server/logic/logic_server.cc`, `src/server/logic/services/player_presence_service.h`, `src/server/logic/services/player_presence_service.cc`

- [x] **F-08** 更新相关测试用例依赖注入方式。  
  文件清单: `tests/server/logic/chat_handler_test.cc`, `tests/server/guild/guild_handler_test.cc`

---

## G. `world/` 归并到 logic 域

- [x] **G-01** 迁移 `role_store.*` 到 `logic/services/session_role_store.*`。  
  文件清单: `src/server/world/role_store.h`, `src/server/world/role_store.cc`, `src/server/logic/services/session_role_store.h` (new), `src/server/logic/services/session_role_store.cc` (new)

- [x] **G-02** 迁移 `role_record.h` 到同域。  
  文件清单: `src/server/world/role_record.h`, `src/server/logic/services/role_record.h` (new)

- [x] **G-03** 更新 handler 与 logic server 引用。  
  文件清单: `src/server/logic/logic_server.h`, `src/server/logic/logic_server.cc`, `src/server/logic/handlers/login/login_handler.h`, `src/server/logic/handlers/login/login_handler.cc`, `src/server/logic/handlers/character/character_handler.h`, `src/server/logic/handlers/character/character_handler.cc`, `src/server/logic/handlers/attack_handler.h`, `src/server/logic/handlers/attack_handler.cc`, `src/server/logic/handlers/skill_handler.h`, `src/server/logic/handlers/skill_handler.cc`

- [x] **G-04** 更新测试引用。  
  文件清单: `tests/server/logic/login_handler_test.cc`, `tests/server/logic/character_handler_test.cc`, `tests/server/logic/player_mailbox_causal_test.cc`, `tests/integration/protocol_integration_test.cpp`

- [x] **G-05** 删除 `src/server/world/`。  
  文件清单: `src/server/world/` (delete), `src/server/CMakeLists.txt`

---

## H. 命名与代码卫生

- [x] **H-01** 统一新代码后缀为 `.cc`，存量 `.cpp` 分批改造。  
  文件清单: `src/server/**/*.cc`, `src/server/CMakeLists.txt`

- [x] **H-02** 清理空目录与死目录。  
  文件清单: `src/server/handlers/login/` (delete), `src/server/logic/handlers/combat/` (delete), `scripts/clean-empty-dirs.sh` (new)

- [x] **H-03** 统一 include guard 与头文件命名风格。  
  文件清单: `src/server/**/*.h` (batch)

- [x] **H-04** 统一命名空间归属（去除旧 `legend2::handlers` 残留）。  
  文件清单: `src/server/logic/logic_server.h`, `src/server/logic/handlers/*.h`, `src/server/logic/services/*.h`, `tests/server/**/*.cc`

- [x] **H-05** 清理编译未使用文件与无引用头。  
  文件清单: `src/server/CMakeLists.txt`, `src/server/logic/CMakeLists.txt`, `src/server/logic/handlers/CMakeLists.txt`, `tests/CMakeLists.txt`

---

## I. 测试与 CI 强化

- [x] **I-01** 增加“禁止新增 legacy 依赖”检查。  
  文件清单: `.github/workflows/docker-build.yml`, `scripts/check_no_legacy_refs.sh` (new)

- [x] **I-02** 增加“禁止引用旧 handlers 路径”检查。  
  文件清单: `.github/workflows/docker-build.yml`, `scripts/check_no_old_handlers_refs.sh` (new)

- [x] **I-03** 增加“空目录检测”检查。  
  文件清单: `.github/workflows/docker-build.yml`, `scripts/check_no_empty_dirs.sh` (new)

- [x] **I-04** 增加默认链接审计（不链接 legacy 对象）。  
  文件清单: `scripts/check_link_targets.sh` (new), `src/server/CMakeLists.txt`, `.github/workflows/docker-build.yml`

- [x] **I-05** 固化迁移回归脚本（登录、移动、KCP、战斗、背包、技能、怪物 AI）。  
  文件清单: `scripts/run-core-tests.sh` (new), `scripts/run-kcp-tests.sh`, `tests/CMakeLists.txt`

- [x] **I-06** 增加迁移期性能对比（tick/战斗/背包热路径）。  
  文件清单: `benchmarks/`, `scripts/check_benchmark.py`, `.github/workflows/benchmark.yml`

---

## J. 交付与回滚

- [ ] **J-01** 每阶段创建回滚点（tag/branch）。  
  文件清单: `docs/ROLLBACK.md`, `scripts/rollback.sh`

- [ ] **J-02** 每阶段输出迁移说明（影响面、风险、回滚步骤）。  
  文件清单: `docs/plans/2026-02-09-server-structure-consolidation-plan.md`, `docs/plans/` (phase reports new)

- [ ] **J-03** 阶段完成后同步更新架构文档与开发说明。  
  文件清单: `AGENTS.md`, `README.md`, `docs/`

- [ ] **J-04** 最终输出“旧目录删除报告 + 新结构索引”。  
  文件清单: `docs/plans/2026-02-09-removal-report.md` (new), `docs/plans/2026-02-09-new-structure-index.md` (new)

- [ ] **J-05** 形成 ADR（为何删除/保留哪些层）。  
  文件清单: `docs/adr/0001-server-structure-consolidation.md` (new)

---

## 推荐执行顺序

1. `B -> C -> D -> E -> F -> G -> H/I/J`  
2. 每个阶段再拆 2-4 个小 PR。  
3. 第一优先级：`B + C`（收益最高、风险可控）。  
