---
name: "Phase G - world 归并"
about: "执行 G 阶段：RoleStore 归并到 logic/services"
title: "[Phase G] world 归并"
labels: "refactor,server-structure,phase-g,logic"
assignees: ""
---

## 目标

将 `world/role_store` 从独立目录归并到逻辑服务域，减少跨层路径噪音。

## 任务清单

- [ ] **G-01** 迁移 `role_store.*` 到 `logic/services/session_role_store.*`。  
  文件清单: `src/server/world/role_store.h`, `src/server/world/role_store.cc`, `src/server/logic/services/session_role_store.h` (new), `src/server/logic/services/session_role_store.cc` (new)

- [ ] **G-02** 迁移 `role_record.h` 到同域。  
  文件清单: `src/server/world/role_record.h`, `src/server/logic/services/role_record.h` (new)

- [ ] **G-03** 更新 handler 与 logic server 引用。  
  文件清单: `src/server/logic/logic_server.h`, `src/server/logic/logic_server.cc`, `src/server/logic/handlers/login/login_handler.h`, `src/server/logic/handlers/login/login_handler.cc`, `src/server/logic/handlers/character/character_handler.h`, `src/server/logic/handlers/character/character_handler.cc`, `src/server/logic/handlers/attack_handler.h`, `src/server/logic/handlers/attack_handler.cc`, `src/server/logic/handlers/skill_handler.h`, `src/server/logic/handlers/skill_handler.cc`

- [ ] **G-04** 更新测试引用。  
  文件清单: `tests/server/logic/login_handler_test.cc`, `tests/server/logic/character_handler_test.cc`, `tests/server/logic/player_mailbox_causal_test.cc`, `tests/integration/protocol_integration_test.cpp`

- [ ] **G-05** 删除 `src/server/world/`。  
  文件清单: `src/server/world/` (delete), `src/server/CMakeLists.txt`

## 验收标准

- [ ] `rg "world/role_store.h"` 无结果。
- [ ] 登录/角色/战斗入口测试通过。
- [ ] `world/` 目录删除完成。

## 依赖关系

- 前置阶段: Phase E
- 后续阶段: Phase H

## 参考

- `docs/plans/2026-02-09-max-fix-checklist.md`

