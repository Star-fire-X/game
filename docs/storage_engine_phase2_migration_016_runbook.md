# StorageEngine Phase2 Migration 016 Runbook

## 1. 目标

- 生产执行 `016_kv_store_soft_delete.sql`。
- 完成上线后验收。
- 具备可执行、可验证的回滚路径。

## 2. 变更内容

- 新增 `kv_store.is_deleted BOOLEAN NOT NULL DEFAULT FALSE`
- 新增索引 `idx_kv_store_is_deleted`

对应脚本：

- 执行/校验/回滚统一入口：`scripts/run_kv_store_soft_delete_migration_016.sh`
- 上线 SQL：`migrations/016_kv_store_soft_delete.sql`
- 回滚 SQL：`migrations/016_kv_store_soft_delete_rollback.sql`

## 3. 前置条件

1. 已完成全量备份（至少 `kv_store` 表级备份）。
2. 变更窗口内暂停高风险写流量（建议维护窗口）。
3. 具备数据库连接信息（`PGHOST/PGPORT/PGUSER/PGPASSWORD/PGDATABASE`）。
4. 应用侧配置保持保守值（`enable_v2_encode=false`，`enable_v2_read_fallback=true`）。

## 4. 生产执行步骤

1. 预检查（只读）

```bash
scripts/run_kv_store_soft_delete_migration_016.sh --verify --report-file logs/migration_016_precheck.report.txt || true
```

2. 执行迁移

```bash
scripts/run_kv_store_soft_delete_migration_016.sh --apply --report-file logs/migration_016_apply.report.txt
```

3. 执行后验收

```bash
scripts/run_kv_store_soft_delete_migration_016.sh --verify --report-file logs/migration_016_verify.report.txt
```

## 5. 验收门禁

以下条件全部满足才算通过：

1. `is_deleted` 列存在。
2. `is_deleted` 为 `NOT NULL` 且默认值为 `FALSE`。
3. `idx_kv_store_is_deleted` 索引存在。
4. `kv_store` 中 `is_deleted IS NULL` 行数为 0。
5. 逻辑服健康指标无异常波动（错误率、P99 延迟、重试率）。

## 6. 回滚路径与验收

回滚触发条件（任一满足）：

1. 迁移后出现持续性 SQL 错误并可归因到本次 schema 变更。
2. 业务核心 SLI（登录成功率/关键写成功率）显著下降且持续。
3. 迁移验收门禁失败且无法在窗口内修复。

执行回滚：

```bash
# 默认保护：若已出现 is_deleted=true 数据，脚本会阻止回滚
scripts/run_kv_store_soft_delete_migration_016.sh --rollback --report-file logs/migration_016_rollback.report.txt
```

如果确认必须强制回滚（接受 tombstone 信息丢失风险）：

```bash
scripts/run_kv_store_soft_delete_migration_016.sh --rollback --force-rollback --report-file logs/migration_016_rollback_force.report.txt
```

回滚后验收条件：

1. `is_deleted` 列不存在。
2. `idx_kv_store_is_deleted` 索引不存在。
3. 应用恢复到阶段1兼容路径配置，并完成重启/热重载确认。

## 7. 演练建议（先预发后生产）

1. 在预发执行 `--apply -> --verify -> --rollback -> --verify` 全链路演练。
2. 记录每一步报告文件与耗时。
3. 生产仅复用已验证命令，不临场改脚本。
