# StorageEngine Phase0 Rollback Template

## Incident Metadata

1. Date/Time:
2. Environment:
3. Owner:
4. Trigger:

## Gate Result Snapshot

1. Failed checks:
2. Current `p50/p95/p99`:
3. Current reject rate:
4. Current queue depth:
5. Current recovery time:

## Rollback Actions

1. Switch traffic back to previous stable build.
2. Disable new phase0 gate-enforced rollout window.
3. Preserve logs, metrics snapshots, and test output artifacts.
4. Verify service health and data accessibility after rollback.

## Post-Rollback Verification

1. `StorageEngineTest.HealthMetrics` passes.
2. No sustained outbox/dead-letter growth.
3. Startup recovery succeeds in restart drill.

## Follow-Up

1. Root cause:
2. Fix owner:
3. Fix ETA:
4. Re-entry gate criteria:
