# Account ID Uint64 Boundary Guide

Last updated: 2026-02-14

## 1. Goal

Define one stable rule for account identifiers across server/client boundaries:

- Canonical type: `uint64_t`
- Invalid sentinel: `0`
- String account IDs are no longer part of runtime truth

This document is the single boundary reference for protocol, DB, snapshot, and JSON behavior.

## 2. Canonical Data Model

Current canonical fields:

- `mir2::common::CharacterData::account_id` is `uint64_t`
- `mir2::common::CharacterCreateRequest::account_id` is `uint64_t`
- `mir2::ecs::CharacterIdentityComponent::account_id` is `AccountId` (`uint64_t`)

Design invariant:

- Any non-zero account ID is considered valid.
- `0` means unknown/invalid/unbound account.

## 3. Protocol Boundary

Network protocol account ID fields remain unsigned 64-bit integers:

- `schemas/login.fbs`
  - `LoginRsp.account_id: ulong`
  - `RoleListReq.account_id: ulong`
  - `LogoutReq.account_id: ulong`
- `schemas/system.fbs`
  - `ConnectionContext.account_id: ulong`

Implication:

- Protocol boundary is already aligned with `uint64_t`.
- No string conversion is required in request/response handlers.

## 4. DB Boundary

SQLite characters table:

- Column type: `account_id INTEGER NOT NULL`

DB interfaces:

- `IDatabase::load_characters_by_account(uint64_t account_id)`
- `SQLiteDatabase::load_characters_by_account(uint64_t account_id)`
- `PostgresDatabase::load_characters_by_account(uint64_t account_id)` (signature aligned)

Write/read behavior:

- Writes always bind `account_id` as `int64`.
- Reads prefer integer decode.
- Compatibility fallback remains for legacy SQLite rows where `account_id` may still be TEXT:
  - Numeric text is parsed to `uint64_t`.
  - Non-numeric text degrades to `0` (invalid sentinel).

## 5. Snapshot Boundary

`CharacterSnapshot` in `schemas/persistence.fbs` now uses:

- `account_id_u64: ulong = 0`

Removed:

- `account_id: string`

Current codec behavior (`src/server/ecs/character_snapshot_codec.cc`):

- Serialize: writes only `account_id_u64`.
- Deserialize: reads only `account_id_u64`.

Compatibility policy:

- Legacy snapshot read compatibility is retained temporarily via prewarm-time migration fallback.
- New writes always use `account_id_u64`; legacy payloads are rewrite-migrated online.

## 6. JSON Boundary

`CharacterData` JSON behavior:

- Serializer writes numeric `account_id`.
- Deserializer accepts:
  - unsigned number
  - signed number (`> 0` only)
  - numeric string (compatibility input)

Use this only as boundary compatibility parsing. Runtime logic must stay numeric.

## 7. Operational Compatibility Strategy

Current strategy after snapshot string removal:

1. Network protocol: fully compatible (already `ulong`).
2. DB: forward-compatible, plus legacy TEXT read fallback in SQLite.
3. Snapshot: compatible via temporary legacy decode + rewrite migration during prewarm restore.

Release requirement:

- Deploy with prewarm migration metrics enabled.
- Remove legacy snapshot fallback only after migration window confirms zero decode-failed and zero legacy-hit trends.

### 7.1 Legacy Snapshot Migration/Cleanup (Implemented)

Runtime prewarm restore now includes automatic legacy snapshot handling:

1. Decode path first tries current snapshot schema.
2. If semantic check fails, it falls back to legacy schema (`account_id:string` layout).
3. Legacy string account IDs are parsed to `uint64_t`:
   - numeric string -> parsed value
   - non-numeric string -> `0`
4. Migrated snapshot payload is rewritten in current schema (`account_id_u64`).
5. If snapshot account ID is still `0` but prewarm context has a valid account ID, account ID is backfilled and rewritten.

Observability metrics:

- `logic.prewarm.snapshot_legacy_migrated_total`
- `logic.prewarm.snapshot_account_backfill_total`
- `logic.prewarm.snapshot_rewrite_failed_total`
- `logic.prewarm.snapshot_decode_failed_total`

Operational cleanup guidance:

- Keep auto-migration enabled for one release window after schema switch.
- Monitor `logic.prewarm.snapshot_decode_failed_total`; if non-zero, inspect corresponding `char:<player_id>` keys and purge unrecoverable blobs in DB/L2 after backup.
- After decode-failed count stabilizes at zero for the migration window, legacy fallback can be removed in a later cleanup release.

## 8. Coding Rules (Must Follow)

For new code:

1. Never introduce `std::string account_id` in domain/runtime structs.
2. Never convert account ID through `std::to_string`/manual parse in business logic.
3. Keep `0` checks explicit when validating authentication/binding.
4. If a boundary needs string parsing, keep it local to that boundary adapter only.

## 9. Quick Audit Checklist

- [ ] No new `string account_id` fields in ECS/common domain models
- [ ] No handler-level `std::to_string(account_id)` conversions
- [ ] DB query/filter APIs use `uint64_t account_id`
- [ ] Snapshot schema keeps only `account_id_u64`
- [ ] Tests cover account ID continuity in role restore/login paths
