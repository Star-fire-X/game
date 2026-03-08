# StorageEngine Phase6 Single-Node Drill Runbook

## Goal

Phase6 single-node drill validates that `StorageEngine` can survive process kill and still recover the expected state after restart.

This runbook covers:

1. `durable_async`
2. `tombstone_gc`
3. `startup_validation`
4. `checkpoint_restore`
5. kill points:
- `prepare_ready`
- `recover_wait`

## Components

1. Fault driver
- `./build-wsl/bin/mir2_storage_engine_phase6_fault_driver`

2. Drill script
- `scripts/run_storage_engine_phase6_single_node_drill.sh`

3. Release gate
- `scripts/run_storage_engine_phase6_release_gate.sh`

4. Offline validator
- `./build-wsl/bin/mir2_storage_admin`

## Scenario A: durable_async

### prepare_ready

```bash
bash scripts/run_storage_engine_phase6_single_node_drill.sh \
  --scenario durable_async \
  --kill-point prepare_ready \
  --driver-bin ./build-wsl/bin/mir2_storage_engine_phase6_fault_driver \
  --admin-bin ./build-wsl/bin/mir2_storage_admin \
  --release-gate-script scripts/run_storage_engine_phase6_release_gate.sh \
  --db-path /tmp/mir2_phase6_durable_async_db \
  --backend-state-path /tmp/mir2_phase6_durable_async_backend_state.txt \
  --report-file /tmp/mir2_phase6_durable_async.report.txt \
  --acceptance-csv /tmp/mir2_phase6_durable_async.acceptance.csv
```

### recover_wait

```bash
bash scripts/run_storage_engine_phase6_single_node_drill.sh \
  --scenario durable_async \
  --kill-point recover_wait \
  --driver-bin ./build-wsl/bin/mir2_storage_engine_phase6_fault_driver \
  --admin-bin ./build-wsl/bin/mir2_storage_admin \
  --release-gate-script scripts/run_storage_engine_phase6_release_gate.sh \
  --db-path /tmp/mir2_phase6_batch2_durable_async_db \
  --backend-state-path /tmp/mir2_phase6_batch2_durable_async_backend_state.txt \
  --report-file /tmp/mir2_phase6_batch2_durable_async.report.txt \
  --acceptance-csv /tmp/mir2_phase6_batch2_durable_async.acceptance.csv
```

### Pass criteria

1. `backend_key_present=true`
2. `outbox_depth=0`
3. `storage_admin_validate_summary total_corrupted=0`

## Scenario B: tombstone_gc

### prepare_ready

```bash
bash scripts/run_storage_engine_phase6_single_node_drill.sh \
  --scenario tombstone_gc \
  --kill-point prepare_ready \
  --driver-bin ./build-wsl/bin/mir2_storage_engine_phase6_fault_driver \
  --admin-bin ./build-wsl/bin/mir2_storage_admin \
  --release-gate-script scripts/run_storage_engine_phase6_release_gate.sh \
  --db-path /tmp/mir2_phase6_tombstone_gc_db \
  --backend-state-path /tmp/mir2_phase6_tombstone_gc_backend_state.txt \
  --report-file /tmp/mir2_phase6_tombstone_gc.report.txt \
  --acceptance-csv /tmp/mir2_phase6_tombstone_gc.acceptance.csv
```

### recover_wait

```bash
bash scripts/run_storage_engine_phase6_single_node_drill.sh \
  --scenario tombstone_gc \
  --kill-point recover_wait \
  --driver-bin ./build-wsl/bin/mir2_storage_engine_phase6_fault_driver \
  --admin-bin ./build-wsl/bin/mir2_storage_admin \
  --release-gate-script scripts/run_storage_engine_phase6_release_gate.sh \
  --db-path /tmp/mir2_phase6_batch2_tombstone_gc_db \
  --backend-state-path /tmp/mir2_phase6_batch2_tombstone_gc_backend_state.txt \
  --report-file /tmp/mir2_phase6_batch2_tombstone_gc.report.txt \
  --acceptance-csv /tmp/mir2_phase6_batch2_tombstone_gc.acceptance.csv
```

### Pass criteria

1. `tombstone_gc_pending=0`
2. `tombstone_gc_reclaimed_total>=1`
3. `tombstone_gc_failed_total=0`
4. `storage_admin_validate_summary total_corrupted=0`

## Scenario C: startup_validation

### prepare_ready

```bash
bash scripts/run_storage_engine_phase6_single_node_drill.sh \
  --scenario startup_validation \
  --driver-bin ./build-wsl/bin/mir2_storage_engine_phase6_fault_driver \
  --admin-bin ./build-wsl/bin/mir2_storage_admin \
  --release-gate-script scripts/run_storage_engine_phase6_release_gate.sh \
  --db-path /tmp/mir2_phase6_batch3_startup_validation_db \
  --backend-state-path /tmp/mir2_phase6_batch3_startup_validation_backend_state.txt \
  --report-file /tmp/mir2_phase6_batch3_startup_validation.report.txt \
  --acceptance-csv /tmp/mir2_phase6_batch3_startup_validation.acceptance.csv
```

### Pass criteria

1. recover summary reports `status=init_failed`
2. release gate passes because startup failed closed
3. `storage_admin_validate_summary total_corrupted>0`

### Notes

1. This is an expected fail-closed drill, not a healthy-startup drill.
2. `storage_admin validate` is expected to exit non-zero and print
   `corrupted entries detected`.

## Scenario D: checkpoint_restore

### prepare_ready

```bash
bash scripts/run_storage_engine_phase6_single_node_drill.sh \
  --scenario checkpoint_restore \
  --driver-bin ./build-wsl/bin/mir2_storage_engine_phase6_fault_driver \
  --admin-bin ./build-wsl/bin/mir2_storage_admin \
  --release-gate-script scripts/run_storage_engine_phase6_release_gate.sh \
  --db-path /tmp/mir2_phase6_batch4_checkpoint_restore_db \
  --backend-state-path /tmp/mir2_phase6_batch4_checkpoint_restore_backend_state.txt \
  --report-file /tmp/mir2_phase6_batch4_checkpoint_restore.report.txt \
  --acceptance-csv /tmp/mir2_phase6_batch4_checkpoint_restore.acceptance.csv
```

### Pass criteria

1. `storage_admin_checkpoint_create_summary status=ok`
2. `storage_admin_checkpoint_restore_summary status=ok`
3. recover summary reports `restored_key_present=true`
4. `storage_admin_validate_summary total_corrupted=0`

## Acceptance CSV

Current acceptance CSV fields:

1. `timestamp_utc`
2. `scenario`
3. `kill_point`
4. `status`
5. `prepare_killed`
6. `recover_exit_code`
7. `gate_exit_code`
8. `db_path`
9. `backend_state_path`
10. `report_file`
11. `prepare_log`
12. `recover_log`
13. `health_file`
14. `validate_file`
15. `gate_report_file`

## Release Gate

The drill script already invokes:

```bash
scripts/run_storage_engine_phase6_release_gate.sh
```

Manual invocation example:

```bash
bash scripts/run_storage_engine_phase6_release_gate.sh \
  --scenario durable_async \
  --kill-point recover_wait \
  --summary-file /tmp/mir2_phase6_batch2_durable_async.recover.txt \
  --health-file /tmp/mir2_phase6_batch2_durable_async.health.txt \
  --validate-file /tmp/mir2_phase6_batch2_durable_async.validate.txt \
  --report-file /tmp/mir2_phase6_batch2_durable_async.gate.report.txt
```

## Notes

1. `prepare_ready` means kill after the prepare-side ready marker is emitted.
2. `recover_wait` means start one recover attempt, kill it during the wait window, then rerun recover.
3. `startup_validation` means prepare writes a valid record, then corrupts raw L2 payload before the restart attempt.
4. `checkpoint_restore` means prepare writes a seed key, then the drill uses `storage_admin checkpoint-create` and `checkpoint-restore` before running recover on the restored DB.
5. This runbook is still single-node only. It does not exercise `mir2_logic` end-to-end.
