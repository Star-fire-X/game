# StorageEngine Phase0 SLO Baseline

## Scope

This document defines the phase0 baseline signals and default gate thresholds for
single-node production rollout.

## Baseline Signals

1. Write latency: `p50`, `p95`, `p99` (ms)
2. Reject rate: failed/rejected writes ratio in the sampling window `[0, 1]`
3. Queue depth: `high_priority + normal_priority + outbox + dead_letter`
4. Recovery time: startup recovery elapsed time (ms)

## Default Gate Thresholds

1. `p50_latency_ms <= 5`
2. `p95_latency_ms <= 10`
3. `p99_latency_ms <= 20`
4. `reject_rate <= 0.01`
5. `queue_depth <= 1000`
6. `recovery_time_ms <= 30000`

## Gate Command

```bash
./scripts/run-storage-phase0-gate.sh
```

## Notes

1. Thresholds are phase0 defaults and can be tightened per environment.
2. Gate failures must include failure reasons and a rollback decision.
