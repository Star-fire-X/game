---
name: "Phase C - 战斗链路压平"
about: "执行 C 阶段：combat_core 并入 ECS systems"
title: "[Phase C] 战斗链路压平"
labels: "refactor,server-structure,phase-c,combat"
assignees: ""
---

## 目标

将 `combat_core` 归并至 ECS 战斗域，消除 `src/server/combat/` 孤立目录。

## 任务清单

- [ ] **C-01** 迁移 `combat_core.*` 到 `ecs/systems/`。  
  文件清单: `src/server/combat/combat_core.h`, `src/server/combat/combat_core.cpp`, `src/server/ecs/systems/combat_core.h` (new), `src/server/ecs/systems/combat_core.cc` (new)

- [ ] **C-02** 更新 `config_manager.h` 的 include。  
  文件清单: `src/server/config/config_manager.h`

- [ ] **C-03** 更新 `combat_system.h` 的 include。  
  文件清单: `src/server/ecs/systems/combat_system.h`

- [ ] **C-04** 更新 `damage_calculator.cc` 的 include。  
  文件清单: `src/server/ecs/systems/damage_calculator.cc`

- [ ] **C-05** 更新 `ecs_combat_service.cc` 的 include。  
  文件清单: `src/server/logic/services/ecs_combat_service.cc`

- [ ] **C-06** 删除 `src/server/combat/` 目录和 CMake 条目。  
  文件清单: `src/server/CMakeLists.txt`, `src/server/combat/` (delete)

- [ ] **C-07** 补充迁移后的回归测试执行脚本。  
  文件清单: `tests/CMakeLists.txt`, `tests/server/combat_core_test.cpp`, `scripts/run-combat-regression.sh` (new)

## 验收标准

- [ ] `rg "server/combat/combat_core.h"` 无结果。
- [ ] 战斗/技能/相关测试全部通过。
- [ ] `src/server/combat/` 已删除。

## 依赖关系

- 前置阶段: Phase A
- 后续阶段: Phase D

## 参考

- `docs/plans/2026-02-09-max-fix-checklist.md`

