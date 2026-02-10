---
name: "Phase A - 先止血（构建与开关）"
about: "执行 A 阶段：构建开关、CI 止血、基线验证"
title: "[Phase A] 先止血（构建与开关）"
labels: "refactor,server-structure,phase-a"
assignees: ""
---

## 目标

建立安全改造基线，确保后续目录迁移不会造成默认构建失稳。

## 任务清单

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

## 验收标准

- [ ] 默认配置下构建通过，且不包含 legacy 源。
- [ ] 基线测试组可稳定执行。
- [ ] 形成可复用的回滚开关与 CI 防护。

## 依赖关系

- 前置阶段: 无
- 后续阶段: Phase B, Phase C

## 参考

- `docs/plans/2026-02-09-max-fix-checklist.md`
- `docs/plans/2026-02-09-server-structure-consolidation-plan.md`

