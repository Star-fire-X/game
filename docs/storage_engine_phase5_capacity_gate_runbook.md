# StorageEngine Phase5 Capacity Gate Runbook

## Purpose

This gate is the operational check for Phase5 capacity governance.
It answers three questions from an offline RocksDB snapshot or live DB path:

1. Is current L2 usage already too close to the configured ceiling?
2. Have hard-limit rejects happened?
3. Are critical writes bypassing the hard limit path?

The gate reads `storage_admin_health_summary` and emits a single
`phase5_capacity_gate_result` line.

## Prerequisites

1. `mir2_storage_admin` has been built:

```bash
./build-wsl/bin/mir2_storage_admin health --db-path /var/lib/mir2/cache
```

2. StorageEngine Phase5 counters are available in L2 `cf_meta`:

- `l2_soft_limit_write_total`
- `l2_hard_limit_reject_total`
- `l2_hard_limit_bypass_total`

## Direct Health Check

```bash
./build-wsl/bin/mir2_storage_admin health --db-path /var/lib/mir2/cache
```

Expected summary fields now include:

- `approx_size_bytes`
- `l2_soft_limit_write_total`
- `l2_hard_limit_reject_total`
- `l2_hard_limit_bypass_total`

## Capacity Gate Command

Recommended baseline gate:

```bash
bash scripts/run_storage_engine_phase5_capacity_gate.sh \
  --db-path /var/lib/mir2/cache \
  --l2-max-size-mb 512 \
  --soft-limit-ratio 0.85 \
  --hard-limit-ratio 0.95 \
  --max-usage-ratio 0.95 \
  --max-hard-limit-rejects 0 \
  --max-hard-limit-bypasses 0 \
  --report-file logs/phase5_capacity_gate.report.txt
```

## Dry Check With Saved Health Output

```bash
./build-wsl/bin/mir2_storage_admin health --db-path /var/lib/mir2/cache \
  > logs/storage_admin_health_phase5.txt

bash scripts/run_storage_engine_phase5_capacity_gate.sh \
  --input-file logs/storage_admin_health_phase5.txt \
  --l2-max-size-mb 512 \
  --max-hard-limit-rejects 0 \
  --max-hard-limit-bypasses 0
```

## Parameters

1. `--l2-max-size-mb`
- Required.
- Must match the active `storage_engine.l2_max_size_mb`.

2. `--soft-limit-ratio`
- Default: `0.85`
- Used to compute `soft_limit_active` from `approx_size_bytes`.

3. `--hard-limit-ratio`
- Default: `0.95`
- Used to compute `hard_limit_active` from `approx_size_bytes`.

4. `--max-usage-ratio`
- Default: same as `--hard-limit-ratio`
- Independent hard gate for current L2 usage.

5. `--max-soft-limit-writes`
- Optional.
- Leave unset if you do not want to fail the gate on cumulative soft-limit events.

6. `--max-hard-limit-rejects`
- Optional but recommended for rollout windows.
- Use `0` when the rollout window requires zero non-critical drops.

7. `--max-hard-limit-bypasses`
- Optional but recommended for rollout windows.
- Use `0` when the rollout window requires zero critical bypass events.

8. `--require-soft-limit-inactive`
- Default: `false`
- Use `true` only when you require headroom above soft limit.

9. `--require-hard-limit-inactive`
- Default: `true`
- Leave enabled for production gates unless there is a temporary exception.

## Result Interpretation

Pass example:

```text
phase5_capacity_gate_result status=pass usage_ratio=0.123456 soft_limit_active=false hard_limit_active=false l2_soft_limit_write_total=0 l2_hard_limit_reject_total=0 l2_hard_limit_bypass_total=0
```

Fail example:

```text
phase5_capacity_gate_result status=fail reasons=hard_limit_active,l2_hard_limit_reject_total>0 usage_ratio=0.976562 soft_limit_active=true hard_limit_active=true ...
```

Common reasons:

1. `usage_ratio>...`
- Current DB footprint is already above the allowed gate threshold.

2. `soft_limit_active`
- Current size is at or above computed soft limit.

3. `hard_limit_active`
- Current size is at or above computed hard limit.

4. `l2_hard_limit_reject_total>...`
- Non-critical writes were rejected by the hard limit path.

5. `l2_hard_limit_bypass_total>...`
- Critical/sync writes bypassed the hard limit path.

## Recommended Production Use

1. Pre-change:
- Run `storage_admin health`
- Run `phase5_capacity_gate`
- Record the report file

2. During gray:
- Re-run the gate per batch window
- Keep `--max-hard-limit-rejects 0`
- Keep `--max-hard-limit-bypasses 0` unless explicitly approved

3. If gate fails:
- Stop the rollout
- Inspect `usage_ratio`, reject counters, and bypass counters
- Reduce write pressure or expand configured L2 budget before retry
