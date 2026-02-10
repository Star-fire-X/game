---
name: "Phase D - Legacy 退场"
about: "执行 D 阶段：legacy 默认下线与替代链路切换"
title: "[Phase D] Legacy 退场"
labels: "refactor,server-structure,phase-d,legacy"
assignees: ""
---

## 目标

将遗留实现从默认主链路剥离，并把仍需能力升格为 ECS 正式模块。

## 任务清单

- [ ] **D-01** 将 `legacy/character_factory.*` 升格为 ECS 正式模块。  
  文件清单: `src/server/legacy/character_factory.h`, `src/server/legacy/character_factory.cc`, `src/server/ecs/persistence/character_codec.h` (new), `src/server/ecs/persistence/character_codec.cc` (new)

- [ ] **D-02** `character_entity_manager` 改用新路径。  
  文件清单: `src/server/ecs/character_entity_manager.cc`, `src/server/ecs/character_entity_manager.h`

- [ ] **D-03** 下线 `legacy/inventory_system.cpp` 默认编译。  
  文件清单: `src/server/CMakeLists.txt`, `src/server/legacy/inventory_system.cpp`, `src/server/legacy/inventory_system.h`

- [ ] **D-04** 下线 `legacy/skill_system.cpp` 默认编译。  
  文件清单: `src/server/CMakeLists.txt`, `src/server/legacy/skill_system.cpp`, `src/server/legacy/skill_system.h`

- [ ] **D-05** 下线 `legacy/monster_ai.cpp` 默认编译。  
  文件清单: `src/server/CMakeLists.txt`, `src/server/legacy/monster_ai.cpp`, `src/server/legacy/monster_ai.h`

- [ ] **D-06** 下线 `legacy/legacy_monster_adapter.cc` 默认编译。  
  文件清单: `src/server/CMakeLists.txt`, `src/server/legacy/legacy_monster_adapter.cc`, `src/server/legacy/legacy_monster_adapter.h`

- [ ] **D-07** 迁移或替换 legacy 专属测试，补齐 ECS 等价覆盖。  
  文件清单: `tests/server/ecs/character_codec_test.cc` (rename from `tests/server/character_factory_test.cc`), `tests/server/ecs/character_entity_manager_test.cpp`, `tests/server/ecs/skill_system_test.cc`, `tests/server/ecs/monster_ai_system_test.cc`, `tests/CMakeLists.txt`

- [ ] **D-08** 清理所有 `legacy/` include 与路径引用。  
  文件清单: `src/server/CMakeLists.txt`, `src/server/ecs/character_entity_manager.cc`, `tests/CMakeLists.txt`

- [ ] **D-09** 删除 `src/server/legacy/` 目录。  
  文件清单: `src/server/legacy/` (delete)

## 验收标准

- [ ] 默认构建不再含 `legacy/*`。
- [ ] 角色加载/保存与 ECS 链路完整可用。
- [ ] 遗留目录删除后测试通过。

## 依赖关系

- 前置阶段: Phase A, Phase B, Phase C
- 后续阶段: Phase E, Phase F

## 参考

- `docs/plans/2026-02-09-max-fix-checklist.md`
