# StorageEngine Phase6 Logic-Process Runbook

## 目标

在真实 `mir2_logic + PostgreSQL` 环境下执行 Phase6 第五批的进程级演练，覆盖两条场景：

1. `startup_validation`
2. `checkpoint_restore`

该 runbook 只验证逻辑服进程启动/恢复链路，不覆盖 `gateway + logic` 端到端协议写入。

## 前置条件

1. PostgreSQL 已启动，且逻辑服可用连接参数已确认。
2. 先重编真实演练二进制，避免用到旧产物：

```bash
cmake --build --preset vcpkg-wsl-debug \
  --target mir2_logic mir2_storage_admin mir2_storage_engine_phase6_fault_driver \
  -j$(nproc)
```

3. 建议为每次演练使用独立临时目录，避免复用旧 RocksDB。

示例 PostgreSQL 参数：

```bash
export PHASE6_DB_HOST=127.0.0.1
export PHASE6_DB_PORT=5432
export PHASE6_DB_USER=mir2
export PHASE6_DB_PASSWORD=mir2_password
export PHASE6_DB_NAME=mir2_game
```

## 场景一：startup_validation

目标：离线注入 L2 损坏后，`mir2_logic` 必须因 `startup_fail_on_validation_error=true` 而 fail-closed。

```bash
ROOT="$(mktemp -d /tmp/mir2_phase6_logic_startup_XXXXXX)"

bash scripts/run_storage_engine_phase6_logic_process_drill.sh \
  --scenario startup_validation \
  --logic-bin ./build-wsl/bin/mir2_logic \
  --fault-driver-bin ./build-wsl/bin/mir2_storage_engine_phase6_fault_driver \
  --admin-bin ./build-wsl/bin/mir2_storage_admin \
  --logic-process-gate-script scripts/run_storage_engine_phase6_logic_process_gate.sh \
  --config-template config/logic.yaml \
  --db-host "${PHASE6_DB_HOST}" \
  --db-port "${PHASE6_DB_PORT}" \
  --db-user "${PHASE6_DB_USER}" \
  --db-password "${PHASE6_DB_PASSWORD}" \
  --db-name "${PHASE6_DB_NAME}" \
  --db-path "${ROOT}/db" \
  --backend-state-path "${ROOT}/backend_state.txt" \
  --report-file "${ROOT}/drill.report.txt" \
  --acceptance-csv "${ROOT}/acceptance.csv" \
  --logic-log-file "${ROOT}/logic.log" \
  --prepare-log "${ROOT}/prepare.log" \
  --ready-file "${ROOT}/ready" \
  --health-file "${ROOT}/health.txt" \
  --validate-file "${ROOT}/validate.txt" \
  --gate-report-file "${ROOT}/gate.report.txt" \
  --temp-config-path "${ROOT}/logic.yaml"
```

通过条件：

1. `phase6_logic_process_drill_result status=pass scenario=startup_validation`
2. `phase6_logic_process_gate_result status=pass scenario=startup_validation`
3. `logic.log` 包含 `StorageEngine init failed` 与 `LogicServer init failed`
4. `validate.txt` 中 `total_corrupted>0`

## 场景二：checkpoint_restore

目标：对离线 checkpoint 执行 create/restore 后，`mir2_logic` 在恢复库上可以成功启动，且校验结果干净。

```bash
ROOT="$(mktemp -d /tmp/mir2_phase6_logic_checkpoint_XXXXXX)"

bash scripts/run_storage_engine_phase6_logic_process_drill.sh \
  --scenario checkpoint_restore \
  --logic-bin ./build-wsl/bin/mir2_logic \
  --fault-driver-bin ./build-wsl/bin/mir2_storage_engine_phase6_fault_driver \
  --admin-bin ./build-wsl/bin/mir2_storage_admin \
  --logic-process-gate-script scripts/run_storage_engine_phase6_logic_process_gate.sh \
  --config-template config/logic.yaml \
  --db-host "${PHASE6_DB_HOST}" \
  --db-port "${PHASE6_DB_PORT}" \
  --db-user "${PHASE6_DB_USER}" \
  --db-password "${PHASE6_DB_PASSWORD}" \
  --db-name "${PHASE6_DB_NAME}" \
  --db-path "${ROOT}/db" \
  --backend-state-path "${ROOT}/backend_state.txt" \
  --checkpoint-path "${ROOT}/checkpoint" \
  --restore-db-path "${ROOT}/restore_db" \
  --report-file "${ROOT}/drill.report.txt" \
  --acceptance-csv "${ROOT}/acceptance.csv" \
  --logic-log-file "${ROOT}/logic.log" \
  --prepare-log "${ROOT}/prepare.log" \
  --ready-file "${ROOT}/ready" \
  --health-file "${ROOT}/health.txt" \
  --validate-file "${ROOT}/validate.txt" \
  --gate-report-file "${ROOT}/gate.report.txt" \
  --checkpoint-create-file "${ROOT}/checkpoint_create.txt" \
  --checkpoint-restore-file "${ROOT}/checkpoint_restore.txt" \
  --temp-config-path "${ROOT}/logic.yaml"
```

通过条件：

1. `phase6_logic_process_drill_result status=pass scenario=checkpoint_restore`
2. `phase6_logic_process_gate_result status=pass scenario=checkpoint_restore`
3. `checkpoint_create.txt` 中 `status=ok`
4. `checkpoint_restore.txt` 中 `status=ok`
5. `validate.txt` 中 `total_corrupted=0`
6. `logic.log` 包含 `LogicServer initialized`

## 验收产物

每次演练至少保留以下文件：

1. `drill.report.txt`
2. `gate.report.txt`
3. `logic.log`
4. `health.txt`
5. `validate.txt`
6. `acceptance.csv`

`checkpoint_restore` 额外保留：

1. `checkpoint_create.txt`
2. `checkpoint_restore.txt`

## 常见失败定位

1. `password authentication failed`
原因：临时 `logic.yaml` 未正确写入数据库参数，或使用了旧模板字段。
处理：检查 `temp_config_path`，确认只有一份 `database:` 段，且密码来自命令行参数。

2. `startup_validation` 未 fail-closed
原因：常见是使用了旧版 `mir2_logic` 二进制。
处理：先重编 `mir2_logic`、`mir2_storage_admin`、`mir2_storage_engine_phase6_fault_driver`，再重跑。

3. `storage_admin validate` 无法打开 DB
原因：逻辑服仍在占用 RocksDB lock。
处理：确认 `mir2_logic` 已退出，再执行离线 `validate`。
