---
name: "Phase I - 测试与 CI 强化"
about: "执行 I 阶段：防回归检查、链接审计、迁移回归与性能对比"
title: "[Phase I] 测试与 CI 强化"
labels: "test,ci,server-structure,phase-i"
assignees: ""
---

## 目标

把阶段性重构规则固化进 CI，避免目录和依赖回退。

## 任务清单

- [ ] **I-01** 增加“禁止新增 legacy 依赖”检查。  
  文件清单: `.github/workflows/docker-build.yml`, `scripts/check_no_legacy_refs.sh` (new)

- [ ] **I-02** 增加“禁止引用旧 handlers 路径”检查。  
  文件清单: `.github/workflows/docker-build.yml`, `scripts/check_no_old_handlers_refs.sh` (new)

- [ ] **I-03** 增加“空目录检测”检查。  
  文件清单: `.github/workflows/docker-build.yml`, `scripts/check_no_empty_dirs.sh` (new)

- [ ] **I-04** 增加默认链接审计（不链接 legacy 对象）。  
  文件清单: `scripts/check_link_targets.sh` (new), `src/server/CMakeLists.txt`

- [ ] **I-05** 固化迁移回归脚本（登录、移动、KCP、战斗、背包、技能、怪物 AI）。  
  文件清单: `scripts/run-core-tests.sh` (new), `scripts/run-kcp-tests.sh`, `tests/CMakeLists.txt`

- [ ] **I-06** 增加迁移期性能对比（tick/战斗/背包热路径）。  
  文件清单: `benchmarks/`, `scripts/check_benchmark.py`, `.github/workflows/benchmark.yml`

## 验收标准

- [ ] CI 可自动阻断 legacy/旧路径回流。
- [ ] 回归脚本可在本地与 CI 复用。
- [ ] 性能基线可量化对比。

## 依赖关系

- 前置阶段: Phase H
- 后续阶段: Phase J

## 参考

- `docs/plans/2026-02-09-max-fix-checklist.md`

