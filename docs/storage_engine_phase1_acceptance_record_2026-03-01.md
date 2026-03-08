# StorageEngine Phase1 Acceptance Record (2026-03-01)

## Scope

Phase1 acceptance for API contract hardening (no storage format migration).

Acceptance criteria from the Phase1 plan:

1. Legacy behavior remains available behind rollback toggle.
2. New API contract is test-covered and passing.
3. No schema migration is required for Phase1 rollout.

## Candidate Commit Window

Base:

1. `d857439` (`feat(storage): add phase0 gate and phase1 api foundations`)

Accepted tip:

1. `76fe1d3` (`fix(storage): mark all-denied batch-get summary as failed`)

Phase1 commit list in order:

1. `0b7cedd` implement phase1 write-path/access APIs.
2. `1fdab53` add runtime toggle `enable_new_write_path`.
3. `a2715e9` enforce per-item access semantics in batch write.
4. `b8cc309` align batch-set access path with per-item semantics.
5. `eeaa7a6` support per-item access filtering for batch get.
6. `555dea2` keep legacy batch-set semantics behind toggle.
7. `be4303f` logic runtime reload of write-path toggle.
8. `552c4a2` legacy batch-set denial summary audit.
9. `1f45630` keep legacy batch-get semantics behind toggle.
10. `4c12700` keep legacy batch-write semantics behind toggle.
11. `4df088e` batch-write item-level write-failure audit.
12. `0eb22bc` batch-set item-level write-failure audit.
13. `76fe1d3` full-deny batch-get summary reason fix.

## Verification Commands and Results

### Build + focused test suite

Command:

```bash
cmake --build --preset vcpkg-wsl-debug --target storage_engine_focus_tests -j8
./build-wsl/bin/storage_engine_focus_tests
```

Result (2026-03-01):

1. `86` tests ran.
2. `85` tests passed.
3. `1` test skipped (`RocksDBCacheP1Test.BenchmarkForEachScanIsolationPreservesBlockCacheHitRateVsBaseline`, benchmark-gated by env var).

### Migration boundary check

Command:

```bash
git diff --name-only d857439..76fe1d3 | rg '^migrations/' || true
```

Result:

1. No output (no migration files changed in Phase1 window).

## Gate-by-Gate Acceptance

1. API compatibility and rollback gate: **PASS**
   - Evidence: `enable_new_write_path` config + runtime tunable + logic reload path are merged and tested.
2. Contract behavior gate: **PASS**
   - Evidence: `StorageEnginePhase1ApiTest` suite includes write/get/set access-path semantics and audit assertions.
3. Legacy fallback gate: **PASS**
   - Evidence: dedicated tests verify legacy deny-fast semantics when toggle is disabled.
4. Schema migration gate: **PASS**
   - Evidence: no `migrations/` changes in accepted commit window.
5. Test gate: **PASS with benchmark skip**
   - Evidence: full `storage_engine_focus_tests` pass except one explicit benchmark skip.

## Deferred to Later Phases

1. V2 on-disk format with CRC and migration toolchain.
2. Tombstone retention and lifecycle recovery semantics.
3. Checkpoint/backup/restore operational tooling.
4. Encryption/audit governance hardening for production security phase.

## Sign-off

1. Phase1 contract docs landed: `docs/storage_engine_phase1_api_semantics.md`.
2. Acceptance record landed: this document.
3. Phase1 can be marked complete for implementation scope, with documentation and evidence archived in-repo.
