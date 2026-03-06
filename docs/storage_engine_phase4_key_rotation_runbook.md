# StorageEngine Phase4 密钥轮换 Runbook

## 1. 目标

1. 在不中断业务的前提下完成 StorageEngine 静态加密密钥轮换。
2. 支持灰度发布（5% -> 25% -> 100%）与快速回退。
3. 确保旧密钥窗口内“新老数据可读”。

## 2. 适用范围

1. `storage_engine.enable_data_encryption=true` 场景。
2. `enable_v2_encode=true` 写路径（加密仅作用于 V2 payload）。
3. 通过 `SIGUSR1` 触发 `LogicServer::ReloadStorageRuntimeConfig()` 的热更新路径。

## 3. 配置项

`config/logic.yaml`：

1. `storage_engine.enable_data_encryption`
2. `storage_engine.encryption_active_key_id`
3. `storage_engine.encryption_key_env`

环境变量格式（`encryption_key_env` 指向的变量）：

1. `key_id=64_hex_chars`
2. 多 key 使用 `,` 或 `;` 分隔，例如：
   - `k1=<hex32>,k2=<hex32>`

说明：

1. `active_key_id` 必须在 keyring 中存在。
2. keyring 需要在轮换窗口同时保留“旧 key + 新 key”。

## 4. 前置门禁

1. 已完成阶段4加密能力上线验证（读写、重启、恢复无异常）。
2. 当前实例 `l2_decrypt_failures` 为稳定低值（建议 0）。
3. 已配置监控：
   - `enable_data_encryption`
   - `l2_encrypted_decode_reads`
   - `l2_decrypt_failures`
   - 业务错误率与写路径 P99
4. 具备回退能力（配置回滚 + `SIGUSR1`）。

可用离线命令快速确认（`mir2_storage_admin health`）：

```bash
./build-wsl/bin/mir2_storage_admin health --db-path /var/lib/mir2/cache
```

输出应包含：

1. `encrypted_decode_reads=<n>`
2. `decrypt_failures=<n>`
3. `runtime_config_audit_key_enable_data_encryption_total=<n>`
4. `runtime_config_audit_key_encryption_key_env_total=<n>`

## 5. 灰度步骤

### A. 5% 灰度

1. 目标实例注入 keyring：`旧key + 新key`。
2. 将 `encryption_active_key_id` 切到新 key。
3. 发送 `SIGUSR1` 热重载。
4. 观察至少 2 小时。

放行条件：

1. 热重载成功日志存在。
2. `enable_data_encryption=true`。
3. `l2_decrypt_failures` 无持续增长。
4. 业务错误率、P99 不超过基线门限。

### B. 25% 灰度

1. 扩到 25% 实例，保持同样 keyring（新旧双 key）。
2. 分批重载，逐批验收。
3. 观察至少 6 小时（覆盖高峰）。

放行条件：

1. A 阶段门禁持续满足。
2. 无 P1/P0 事故。

### C. 100% 全量

1. 全量实例切换到新 active key。
2. 保留旧 key 至少一个完整数据轮换窗口（建议 >=7 天）。
3. 观察至少 24 小时后，再清理旧 key。

## 6. 回退流程

触发条件（任一满足）：

1. 重载后实例出现持续性加密读失败（`l2_decrypt_failures` 快速增长）。
2. 业务错误率或延迟显著恶化并可归因到轮换。

回退动作：

1. 将 `encryption_active_key_id` 改回旧 key。
2. 保持 keyring 中同时存在旧 key（可包含新 key）。
3. 发送 `SIGUSR1`。
4. 5 分钟内确认指标回稳。

## 7. 记录模板

1. 批次：5% / 25% / 100%
2. 实例范围：
3. 开始/结束时间：
4. active key 切换：`old -> new`
5. 关键指标结论：
6. 是否进入下一批：
7. 是否触发回退：

## 8. 脚本化执行（推荐）

脚本：`scripts/run_storage_engine_phase4_key_rotation.sh`

### A. 预检查

```bash
export MIR2_STORAGE_ENCRYPTION_KEYS='k_old=<64hex>,k_new=<64hex>'
bash scripts/run_storage_engine_phase4_key_rotation.sh \
  --precheck \
  --config config/logic.yaml \
  --target-key-id k_new \
  --env-name MIR2_STORAGE_ENCRYPTION_KEYS \
  --report-file logs/phase4_key_rotation_precheck.report.txt
```

### B. 灰度切换（5% / 25% / 100%）

```bash
bash scripts/run_storage_engine_phase4_key_rotation.sh \
  --apply \
  --config config/logic.yaml \
  --target-key-id k_new \
  --env-name MIR2_STORAGE_ENCRYPTION_KEYS \
  --pid <mir2_logic_pid> \
  --report-file logs/phase4_key_rotation_apply_<batch>.report.txt
```

说明：

1. 默认会发送 `SIGUSR1` 触发热更新。
2. 仅演练不改配置时可加 `--dry-run`。
3. 不希望脚本发信号时可加 `--skip-reload`，手工执行 `kill -USR1 <pid>`。
4. `--precheck`、`--apply`、`--rollback` 三种模式严格互斥，每次只能选一种。

### C. 回退

```bash
bash scripts/run_storage_engine_phase4_key_rotation.sh \
  --rollback \
  --config config/logic.yaml \
  --target-key-id k_old \
  --env-name MIR2_STORAGE_ENCRYPTION_KEYS \
  --pid <mir2_logic_pid> \
  --report-file logs/phase4_key_rotation_rollback.report.txt
```

### D. 门禁校验（推荐自动化）

```bash
bash scripts/run_storage_engine_phase4_health_gate.sh \
  --db-path /var/lib/mir2/cache \
  --max-decrypt-failures 0 \
  --max-decode-errors 0 \
  --min-encrypted-decode-reads 1 \
  --max-runtime-enable-data-encryption-audits 0 \
  --max-runtime-encryption-key-env-audits 0 \
  --require-encryption-enabled true \
  --require-v2-encode true \
  --report-file logs/phase4_health_gate_<batch>.report.txt
```

说明：

1. 脚本返回码 `0` 表示通过，非 `0` 表示不通过。
2. 可用 `--input-file` 对离线采样结果做复核。
3. 默认要求 `runtime_config_audit_key_enable_data_encryption_total <= 0` 与
   `runtime_config_audit_key_encryption_key_env_total <= 0`。
4. 特殊演练场景可临时设置 `--require-encryption-enabled false` 或
   `--require-v2-encode false`，发布后应恢复为 `true`。

### E. 一键灰度批次（5% / 25% / 100%）

```bash
bash scripts/run_storage_engine_phase4_gray_batch.sh \
  --batch 5 \
  --config config/logic.yaml \
  --target-key-id k_new \
  --rollback-key-id k_old \
  --auto-rollback-on-gate-fail \
  --env-name MIR2_STORAGE_ENCRYPTION_KEYS \
  --db-path /var/lib/mir2/cache \
  --max-decrypt-failures 0 \
  --max-decode-errors 0 \
  --min-encrypted-decode-reads 1 \
  --max-runtime-enable-data-encryption-audits 0 \
  --max-runtime-encryption-key-env-audits 0 \
  --require-encryption-enabled true \
  --require-v2-encode true \
  --acceptance-csv logs/phase4_gray_acceptance.csv \
  --report-file logs/phase4_gray_batch_5.report.txt
```

将 `--batch` 依次切换为 `25`、`100` 即可推进后续批次。

说明：

1. 批次脚本会串行执行“密钥切换 -> 健康门禁”。
2. 门禁失败时脚本返回非 `0` 并立即中断该批次。
3. 启用 `--auto-rollback-on-gate-fail` 后，门禁失败会自动回滚到 `--rollback-key-id`。
4. 若同时使用 `--skip-gate`，则不会触发自动回滚，也不要求传 `--rollback-key-id`。
5. `--acceptance-csv` 会追加结构化验收记录（批次、状态、原因、回滚尝试、回滚结果、前后 active key、门禁阈值快照、门禁实测值与失败原因）。
6. 若使用 `--skip-gate`，验收行会显式记录 `gate_status=skipped`、`gate_reasons=skip_gate`。
7. 若 `--acceptance-csv` 既有表头与当前版本不一致，脚本会直接失败；请先轮转或新建 CSV 文件。
