---
name: "Phase J - 交付与回滚"
about: "执行 J 阶段：发布物料、回滚机制、最终 ADR 与结构索引"
title: "[Phase J] 交付与回滚"
labels: "docs,ops,server-structure,phase-j"
assignees: ""
---

## 目标

形成完整交付闭环，确保重构结果可追踪、可回滚、可长期维护。

## 任务清单

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

## 验收标准

- [ ] 每阶段均有可执行回滚路径。
- [ ] 最终结构文档、删除报告、ADR 齐全。
- [ ] 新成员可仅依赖文档理解当前服务端结构。

## 依赖关系

- 前置阶段: Phase I
- 后续阶段: 无（收尾阶段）

## 参考

- `docs/plans/2026-02-09-max-fix-checklist.md`

