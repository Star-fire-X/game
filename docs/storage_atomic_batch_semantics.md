# Storage Atomic Batch Semantics

## Guarantees

Atomic batch semantics are guaranteed only within a single concrete backend.

## Backend-specific Behavior

### `StorageEngineBackend`

- Implements `IAtomicBatchStorageBackend`.
- `SaveBatchAtomic` runs one PostgreSQL transaction for the full batch.

### `AccountStorageBackend`

- Implements `IAtomicBatchStorageBackend`.
- Supports atomic batches only when all keys route to the same store:
  - All account keys (`account:username:*`): one transaction in `accounts`.
  - All non-account keys: delegated to KV backend atomic path if available,
    otherwise delegated to KV `SaveBatch`.
- Mixed account + non-account atomic batches are rejected with:
  - `success=false`
  - `error_message="cross-store atomic batch unsupported"`

## Non-goals

- No two-phase commit across `accounts` and `kv_store`.
- No cross-store distributed transaction guarantee.

## Caller Requirements

- Business logic must avoid relying on cross-store atomicity.
- If one workflow needs all-or-nothing behavior, it must be scoped to one
  backend store or redesigned with idempotent compensation.
