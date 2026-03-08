---
name: "Phase H - 命名与代码卫生"
about: "执行 H 阶段：后缀统一、空目录清理、命名与头文件规范收敛"
title: "[Phase H] 命名与代码卫生"
labels: "refactor,server-structure,phase-h,cleanup"
assignees: ""
---

## 目标

完成目录收口后的代码卫生治理，降低长期维护成本。

## 任务清单

- [ ] **H-01** 统一新代码后缀为 `.cc`，存量 `.cpp` 分批改造。  
  文件清单: `src/server/legacy/inventory_system.cpp`, `src/server/legacy/skill_system.cpp`, `src/server/legacy/monster_ai.cpp`, `src/server/CMakeLists.txt`

- [ ] **H-02** 清理空目录与死目录。  
  文件清单: `src/server/handlers/login/` (delete), `src/server/logic/handlers/combat/` (delete), `scripts/clean-empty-dirs.sh` (new)

- [ ] **H-03** 统一 include guard 与头文件命名风格。  
  文件清单: `src/server/**/*.h` (batch)

- [ ] **H-04** 统一命名空间归属（去除旧 `legend2::handlers` 残留）。  
  文件清单: `src/server/logic/logic_server.h`, `src/server/logic/handlers/*.h`, `src/server/logic/services/*.h`, `tests/server/**/*.cc`

- [ ] **H-05** 清理编译未使用文件与无引用头。  
  文件清单: `src/server/CMakeLists.txt`, `src/server/logic/handlers/CMakeLists.txt`, `tests/CMakeLists.txt`

## 验收标准

- [ ] 空目录检测通过。
- [ ] 后缀与命名策略符合约定。
- [ ] 无无效编译单元残留。

## 依赖关系

- 前置阶段: Phase F, Phase G
- 后续阶段: Phase I, Phase J

## 参考

- `docs/plans/2026-02-09-max-fix-checklist.md`

