# Storage Backend ABI Compatibility Rules

## Scope

This document defines ABI governance for `src/server/storage_engine/interfaces/storage_backend.h`.

## Rules

1. Do not add new virtual methods directly to `IStorageBackend`.
2. New optional capabilities must be added through extension interfaces
   (for example `IAtomicBatchStorageBackend`) with an independent vtable.
3. Call sites must perform runtime capability detection (`dynamic_cast`) and
   fallback to the base interface behavior when an extension is unavailable.
4. Every release that modifies storage backend interfaces requires a full
   rebuild of all backend implementations and dependent binaries.

## Current Extension Pattern

- Base interface: `IStorageBackend`
  - Required operations: `Save`, `SaveBatch`, `Load`, `LoadAll`, `Validate`, `IsHealthy`.
- Optional extension: `IAtomicBatchStorageBackend`
  - Optional operation: `SaveBatchAtomic`.

## Migration Guidance

- Existing backends should implement `IStorageBackend` first.
- Backends that support atomic batch semantics should additionally implement
  `IAtomicBatchStorageBackend`.
- Queues and services that require atomic batch should prefer extension
  interface detection and fallback to `SaveBatch` when atomic support is absent.
