# StorageEngine Phase5 Tombstone GC Runbook

## Goal

Phase5 tombstone GC turns logical deletes into delayed hard deletes.

This closes the lifecycle for:

1. `Delete(..., write_tombstone=true)` logical delete
2. retention window hold
3. background hard delete reclaim
4. restart-safe replay from L2 meta

## Behavior Summary

1. Logical delete path
- When `tombstone_retention_seconds > 0`
- `Delete()` writes a logical delete to backend
- A tombstone GC task is appended into L2 `cf_meta`

2. Background reclaim
- Scheduler wakes every `tombstone_gc_interval_seconds`
- Due tasks call backend `Delete(key, version, hard_delete=true)`
- Success removes the task from L2 meta

3. Restart recovery
- Pending GC tasks are loaded from L2 meta on restart
- Due tasks continue to be reclaimed after restart

## Config

`config/logic.yaml`

```yaml
storage_engine:
  tombstone_retention_seconds: 604800
  tombstone_gc_interval_seconds: 3600
```

## Runtime Reload

`SIGUSR1` reload now applies:

1. `tombstone_retention_seconds`
2. `tombstone_gc_interval_seconds`

If both remain greater than `0`, the GC scheduler stays enabled.
If either becomes `0`, the scheduler stops.

## Health Fields

`StorageEngine::GetHealthMetrics()` now exposes:

1. `tombstone_gc_pending`
2. `tombstone_gc_reclaimed_total`
3. `tombstone_gc_failed_total`

`mir2_storage_admin health` now also exposes:

1. `tombstone_gc_pending`
2. `tombstone_gc_reclaimed_total`
3. `tombstone_gc_failed_total`

Recommended interpretation:

1. `pending` rising steadily
- GC not keeping up or backend hard delete failing

2. `reclaimed_total` increasing
- delayed reclaim is working

3. `failed_total` increasing
- backend unhealthy, hard delete failed, or ack cleanup failed

## Recommended Defaults

1. Production
- `tombstone_retention_seconds: 604800`
- `tombstone_gc_interval_seconds: 3600`

2. Pre-release / staging validation
- `tombstone_retention_seconds: 60`
- `tombstone_gc_interval_seconds: 5`

## Rollout Checklist

1. Before enable
- confirm backend hard delete path is healthy
- confirm L2 path is persistent and writable

2. After enable
- watch `tombstone_gc_pending`
- watch `tombstone_gc_reclaimed_total`
- confirm `tombstone_gc_failed_total == 0`

3. Restart test
- create a logical delete
- restart process before due time
- verify reclaim still happens after restart

## Tombstone GC Gate

Direct gate from live DB:

```bash
bash scripts/run_storage_engine_phase5_tombstone_gc_gate.sh \
  --db-path /var/lib/mir2/cache \
  --max-tombstone-gc-pending 0 \
  --min-tombstone-gc-reclaimed 0 \
  --max-tombstone-gc-failed 0 \
  --report-file logs/phase5_tombstone_gc_gate.report.txt
```

Dry check from saved health output:

```bash
./build-wsl/bin/mir2_storage_admin health --db-path /var/lib/mir2/cache \
  > logs/storage_admin_health_tombstone_gc.txt

bash scripts/run_storage_engine_phase5_tombstone_gc_gate.sh \
  --input-file logs/storage_admin_health_tombstone_gc.txt \
  --max-tombstone-gc-pending 0 \
  --min-tombstone-gc-reclaimed 1 \
  --max-tombstone-gc-failed 0
```

Pass example:

```text
phase5_tombstone_gc_gate_result status=pass tombstone_gc_pending=0 tombstone_gc_reclaimed_total=1 tombstone_gc_failed_total=0
```

Fail example:

```text
phase5_tombstone_gc_gate_result status=fail reasons=tombstone_gc_pending>0,tombstone_gc_failed_total>0 ...
```

## Failure Handling

1. If `tombstone_gc_failed_total` increases
- check backend health
- check circuit breaker state
- check DB delete permission / SQL errors

2. If reclaim must be paused
- set `tombstone_gc_interval_seconds: 0` and reload

3. If logical delete retention must be disabled
- set `tombstone_retention_seconds: 0` and reload
- new deletes fall back to immediate hard delete

## Verification Commands

Targeted tests:

```bash
./build-wsl/bin/storage_engine_focus_tests --gtest_filter='RocksDBCacheP1Test.TombstoneGcAppendReplayAckRoundTrip:StorageEnginePhase5TombstoneGcTest.SchedulerReclaimsLogicalDeleteAfterRetention:StorageEnginePhase5TombstoneGcTest.PendingGcTasksSurviveRestart'
```

Full focus suite:

```bash
./build-wsl/bin/storage_engine_focus_tests
```
