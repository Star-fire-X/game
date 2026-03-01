# StorageEngine Phase1 API Contract and Semantics Matrix

## Scope

This document freezes the Phase1 contract for the single-node StorageEngine.
It focuses on API behavior and runtime semantics only.

Out of scope for Phase1:

1. On-disk format migration (V2 header/CRC).
2. Tombstone retention lifecycle and compaction policy.
3. New admin binaries and checkpoint/backup toolchain.

## Source of Truth

1. `src/server/storage_engine/storage_engine.h`
2. `src/server/storage_engine/storage_engine.cc`
3. `src/server/logic/logic_server.cc`
4. `tests/server/storage_engine_test.cc`

## Stable API Surface (Phase1)

### New Core APIs

1. `Put(key, data, WriteOptions)`
2. `Delete(key, DeleteOptions)`
3. `BatchWrite(items) -> BatchWriteResult`
4. `ValidateStorage() -> StorageValidationReport`

### Compatibility Wrappers (still supported)

1. `Set` wraps `Put` with `WriteDurability::kBestEffort`.
2. `SetAsyncDurable` wraps `Put` with `WriteDurability::kDurableAsync`.
3. `SetSync` wraps `Put` with `WriteDurability::kSync`.
4. `BatchSet` wraps `BatchWrite` when `enable_new_write_path=true`.

### Access-Control APIs

1. `PutWithAccess`, `DeleteWithAccess`
2. `BatchWriteWithAccess`, `BatchGetWithAccess`, `BatchSetWithAccess`
3. `GetWithAccess`, `LoadFromDBWithAccess`, `SetSyncWithAccess`

### Runtime Toggle API

1. `ApplyRuntimeConfig(RuntimeTunableConfig)`
2. `RuntimeTunableConfig.enable_new_write_path` supports runtime rollback.
3. Logic server reload path: `SIGUSR1 -> LogicServer::ReloadStorageRuntimeConfig()`.

## Write Durability Semantics Matrix

| Durability | Entry APIs | Sync-prefix auto-upgrade | Success condition (summary) | Typical use |
|---|---|---|---|---|
| `kBestEffort` | `Put`, `Set` | Yes by default (can bypass) | At least one durable path succeeds (`L2` or backend sync or queue/outbox enqueue); key may be upgraded to sync path when prefix-matched | Default gameplay writes |
| `kDurableAsync` | `Put`, `SetAsyncDurable` | No | Uses async durable path without sync-prefix auto-upgrade; still returns `false` when no persistence path is available | High-throughput async writes |
| `kSync` | `Put`, `SetSync` | N/A (explicit sync) | Sync path succeeds (`L2` sync write or backend sync fallback) | Critical writes (trade, billing-like paths) |

Notes:

1. `WriteOptions.bypass_sync_prefix_upgrade=true` disables automatic sync-prefix upgrade for `kBestEffort`.
2. `sync_write_key_prefixes` and `critical_key_prefixes` are treated as sync-write keys in Phase1 behavior.
3. `DeleteOptions` is hard-delete by default in Phase1 (`hard_delete=true`).

## Batch Semantics Matrix

### Non-access Batch APIs

| API | Phase1 behavior |
|---|---|
| `BatchWrite` | Per-item result reporting via `BatchWriteResult` (`failed_keys`, `failure_reasons`, `failure_reason_codes`) |
| `BatchSet` (`enable_new_write_path=true`) | Delegates to `BatchWrite`; allowed items may still succeed when some items fail |
| `BatchSet` (`enable_new_write_path=false`) | Legacy all-or-nothing semantics (first invalid/failing item aborts effective batch success) |

### Access Batch APIs

| API | `enable_new_write_path=true` | `enable_new_write_path=false` |
|---|---|---|
| `BatchWriteWithAccess` | Per-item access filtering + per-item failure details and audit | Legacy deny-fast semantics, full-batch failure on first denied/invalid item |
| `BatchGetWithAccess` | Per-item allow/deny filtering; denied items become `nullopt`; batch summary differentiates full deny vs partial deny | Legacy deny-fast semantics, returns all `nullopt` when any key denied |
| `BatchSetWithAccess` | Built on `BatchWriteWithAccess`; returns `false` if any item fails but preserves successful items | Legacy deny-fast semantics; denied/invalid item keeps whole batch unsuccessful |

## Audit Semantics (Phase1 lock)

1. Batch APIs emit `<batch>` summary entries.
2. `BatchWriteWithAccess` emits per-item failure audit entries for write-stage failures.
3. `BatchSetWithAccess` emits per-item failure audit entries for write-stage failures.
4. `BatchGetWithAccess` summary reason values:
   - `ok` when none denied.
   - `batch_get_partial_denied` when partially denied.
   - `batch_get_failed` when all keys denied.

## Validation API Contract

`ValidateStorage()` always returns a structured `StorageValidationReport`.

1. `ok=false` with `summary="storage_not_initialized"` when engine is not initialized.
2. Backend validation failure is surfaced via `summary` and `ok=false`.
3. Phase1 validation scope is backend-health-oriented and intentionally lightweight.
