---
name: "Phase B - Handler 收敛"
about: "执行 B 阶段：收敛旧 handlers 与 logic/network 边界"
title: "[Phase B] Handler 收敛"
labels: "refactor,server-structure,phase-b"
assignees: ""
---

## 目标

消除 `src/server/handlers/` 与 `src/server/logic/handlers/` 的双轨并存，明确网络 handler 与逻辑 handler 边界。

## 任务清单

- [ ] **B-01** 迁移 `client_registry` 到 logic 域（建议 `logic/services`）。  
  文件清单: `src/server/handlers/client_registry.h`, `src/server/handlers/client_registry.cc`, `src/server/logic/services/client_registry.h` (new), `src/server/logic/services/client_registry.cc` (new), `src/server/logic/logic_server.h`, `src/server/logic/logic_server.cc`

- [ ] **B-02** 迁移 `movement_validator` 到 `logic/handlers/movement/`。  
  文件清单: `src/server/handlers/movement/movement_validator.h`, `src/server/handlers/movement/movement_validator.cc`, `src/server/logic/handlers/movement/movement_validator.h` (new), `src/server/logic/handlers/movement/movement_validator.cc` (new), `src/server/logic/handlers/movement/movement_handler.h`, `src/server/logic/handlers/movement/movement_handler.cc`

- [ ] **B-03** 迁移 `merchant_handler` 到 `logic/services/merchant_service`。  
  文件清单: `src/server/handlers/merchant_handler.h`, `src/server/handlers/merchant_handler.cc`, `src/server/logic/services/merchant_service.h` (new), `src/server/logic/services/merchant_service.cc` (new)

- [ ] **B-04** 迁移 `kcp_upgrade_handler` 到 `network/handlers/`（仅改目录，不改职责）。  
  文件清单: `src/server/handlers/network/kcp_upgrade_handler.h`, `src/server/handlers/network/kcp_upgrade_handler.cc`, `src/server/network/handlers/kcp_upgrade_handler.h` (new), `src/server/network/handlers/kcp_upgrade_handler.cc` (new), `src/server/network/dual_channel_manager.cc`

- [ ] **B-05** 评估 `entity_broadcast_service`：无引用则先移出默认编译。  
  文件清单: `src/server/handlers/movement/entity_broadcast_service.h`, `src/server/handlers/movement/entity_broadcast_service.cc`, `src/server/CMakeLists.txt`

- [ ] **B-06** 批量改 include 路径（logic/network/tests）。  
  文件清单: `src/server/logic/logic_server.h`, `src/server/logic/handlers/chat/chat_handler.h`, `src/server/logic/handlers/guild/guild_handler.h`, `src/server/logic/handlers/character/character_handler.h`, `src/server/logic/handlers/login/login_handler.h`, `src/server/network/dual_channel_manager.cc`, `tests/server/network/kcp_upgrade_handler_test.cc`, `tests/server/logic/login_handler_test.cc`, `tests/server/logic/chat_handler_test.cc`, `tests/server/guild/guild_handler_test.cc`

- [ ] **B-07** 从 CMake 移除旧 `handlers/*` 路径。  
  文件清单: `src/server/CMakeLists.txt`, `src/server/logic/handlers/CMakeLists.txt`

- [ ] **B-08** 删除空目录 `src/server/handlers/login`。  
  文件清单: `src/server/handlers/login/` (delete)

- [ ] **B-09** 删除空目录 `src/server/logic/handlers/combat`。  
  文件清单: `src/server/logic/handlers/combat/` (delete)

- [ ] **B-10** 删除旧 `src/server/handlers/` 根目录（确认无引用后）。  
  文件清单: `src/server/handlers/` (delete), `src/server/CMakeLists.txt`, `tests/CMakeLists.txt`

## 验收标准

- [ ] 默认构建不再编译旧 `handlers/*`。
- [ ] KCP 升级、移动、登录相关测试通过。
- [ ] 目录无空壳 handler 模块。

## 依赖关系

- 前置阶段: Phase A
- 后续阶段: Phase C, Phase D

## 参考

- `docs/plans/2026-02-09-max-fix-checklist.md`

