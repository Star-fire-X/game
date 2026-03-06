# StorageEngine Phase6 Batch2 Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Extend the existing single-node Phase6 crash drill into a kill-point matrix for `durable_async` and `tombstone_gc`, with real acceptance outputs and release-gate coverage.

**Architecture:** Reuse the Phase6 batch1 fault driver, drill script, and release gate as the foundation. Add explicit `kill_point` support end to end so the same two scenarios can be exercised at finer interruption boundaries without introducing new recovery semantics or pulling `mir2_logic` into scope.

**Tech Stack:** C++20, StorageEngine, RocksDB, GoogleTest, bash scripts, existing `mir2_storage_admin`

---

### Task 1: Add failing shell tests for kill-point matrix

**Files:**
- Modify: `tests/server/storage_engine/phase6_shell_script_test.cc`
- Test: `tests/server/storage_engine/phase6_shell_script_test.cc`

**Step 1: Write the failing test**

Add tests that require:

1. `run_storage_engine_phase6_release_gate.sh` to accept `kill_point`
2. `run_storage_engine_phase6_single_node_drill.sh` to persist `kill_point` into acceptance CSV
3. release gate to reject a bad `recover_wait` tombstone summary

**Step 2: Run test to verify it fails**

Run:

```bash
./build-wsl/bin/storage_engine_focus_tests --gtest_filter='Phase6ReleaseGateScriptTest.*KillPoint*:Phase6SingleNodeDrillScriptTest.*KillPoint*'
```

Expected: FAIL because kill-point arguments and acceptance recording are not implemented yet.

**Step 3: Write minimal implementation**

Do not implement yet in this task.

**Step 4: Re-run the failing test**

Confirm the failure is caused by missing script behavior, not by malformed tests.

**Step 5: Commit**

```bash
git add tests/server/storage_engine/phase6_shell_script_test.cc
git commit -m "test(storage): add phase6 kill-point red tests"
```

### Task 2: Add kill-point support to Phase6 release gate

**Files:**
- Modify: `scripts/run_storage_engine_phase6_release_gate.sh`
- Modify: `tests/server/storage_engine/phase6_shell_script_test.cc`

**Step 1: Write the failing test**

Require gate behavior for:

1. `--kill-point recover_wait` accepted and echoed into report
2. scenario-specific reject reasons still evaluated under the given kill point

**Step 2: Run test to verify it fails**

Run:

```bash
./build-wsl/bin/storage_engine_focus_tests --gtest_filter='Phase6ReleaseGateScriptTest.*KillPoint*'
```

Expected: FAIL with unknown argument or missing report fields.

**Step 3: Write minimal implementation**

Update the gate script to:

1. accept `--kill-point`
2. include it in pass/fail result lines
3. keep existing scenario semantics unchanged

**Step 4: Run test to verify it passes**

Run:

```bash
./build-wsl/bin/storage_engine_focus_tests --gtest_filter='Phase6ReleaseGateScriptTest.*'
```

Expected: PASS.

**Step 5: Commit**

```bash
git add scripts/run_storage_engine_phase6_release_gate.sh tests/server/storage_engine/phase6_shell_script_test.cc
git commit -m "feat(storage): add phase6 release gate kill-point support"
```

### Task 3: Add kill-point support to single-node drill orchestration

**Files:**
- Modify: `scripts/run_storage_engine_phase6_single_node_drill.sh`
- Modify: `tests/server/storage_engine/phase6_shell_script_test.cc`

**Step 1: Write the failing test**

Require:

1. `--kill-point` argument accepted by drill script
2. acceptance CSV stores `kill_point`
3. report includes `kill_point`

**Step 2: Run test to verify it fails**

Run:

```bash
./build-wsl/bin/storage_engine_focus_tests --gtest_filter='Phase6SingleNodeDrillScriptTest.*KillPoint*'
```

Expected: FAIL because drill script schema and invocation are missing kill-point handling.

**Step 3: Write minimal implementation**

Update the drill script to:

1. accept `--kill-point`
2. forward it into driver and release gate
3. record it into acceptance CSV/report

**Step 4: Run test to verify it passes**

Run:

```bash
./build-wsl/bin/storage_engine_focus_tests --gtest_filter='Phase6SingleNodeDrillScriptTest.*'
```

Expected: PASS.

**Step 5: Commit**

```bash
git add scripts/run_storage_engine_phase6_single_node_drill.sh tests/server/storage_engine/phase6_shell_script_test.cc
git commit -m "feat(storage): record phase6 drill kill points"
```

### Task 4: Extend fault driver with recover_wait kill-point matrix

**Files:**
- Modify: `src/server/apps/storage_engine_phase6_fault_driver.cc`
- Modify: `src/server/apps/storage_engine_phase6_fault_driver.h`
- Modify: `tests/server/storage_engine/phase6_shell_script_test.cc`

**Step 1: Write the failing test**

Require the driver to:

1. accept `--kill-point`
2. emit `kill_point=` in its summary
3. support at least `recover_wait` for both scenarios

**Step 2: Run test to verify it fails**

Run:

```bash
./build-wsl/bin/storage_engine_focus_tests --gtest_filter='Phase6SingleNodeDrillScriptTest.*KillPoint*'
```

Expected: FAIL because driver summary does not expose kill-point-aware behavior.

**Step 3: Write minimal implementation**

In the driver:

1. parse `--kill-point`
2. for `recover_wait`, create a deterministic wait window before success is reported
3. emit `kill_point=<value>` in the summary line

**Step 4: Run test to verify it passes**

Run:

```bash
./build-wsl/bin/storage_engine_focus_tests --gtest_filter='Phase6*'
```

Expected: PASS.

**Step 5: Commit**

```bash
git add src/server/apps/storage_engine_phase6_fault_driver.h src/server/apps/storage_engine_phase6_fault_driver.cc tests/server/storage_engine/phase6_shell_script_test.cc
git commit -m "feat(storage): add phase6 driver kill-point matrix"
```

### Task 5: Add real smoke coverage for kill-point matrix

**Files:**
- Modify: `scripts/run_storage_engine_phase6_single_node_drill.sh`
- Modify: `scripts/run_storage_engine_phase6_release_gate.sh`
- Modify: `docs/storage_engine_phase6_single_node_drill_runbook.md` (create if missing)

**Step 1: Write the failing test**

Use shell tests to require:

1. `durable_async + recover_wait` pass path
2. `tombstone_gc + recover_wait` pass path

**Step 2: Run test to verify it fails**

Run:

```bash
./build-wsl/bin/storage_engine_focus_tests --gtest_filter='Phase6*'
```

Expected: FAIL until orchestration fully forwards kill points.

**Step 3: Write minimal implementation**

Ensure drill + gate + driver behave consistently for `recover_wait`.

**Step 4: Run test to verify it passes**

Run:

```bash
./build-wsl/bin/storage_engine_focus_tests --gtest_filter='Phase6*'
```

Expected: PASS.

Then run real smoke:

```bash
bash scripts/run_storage_engine_phase6_single_node_drill.sh --scenario durable_async --kill-point recover_wait --driver-bin ./build-wsl/bin/mir2_storage_engine_phase6_fault_driver --admin-bin ./build-wsl/bin/mir2_storage_admin --release-gate-script scripts/run_storage_engine_phase6_release_gate.sh --db-path /tmp/mir2_phase6_batch2_durable_async_db --backend-state-path /tmp/mir2_phase6_batch2_durable_async_backend_state.txt --report-file /tmp/mir2_phase6_batch2_durable_async.report.txt --acceptance-csv /tmp/mir2_phase6_batch2_durable_async.acceptance.csv

bash scripts/run_storage_engine_phase6_single_node_drill.sh --scenario tombstone_gc --kill-point recover_wait --driver-bin ./build-wsl/bin/mir2_storage_engine_phase6_fault_driver --admin-bin ./build-wsl/bin/mir2_storage_admin --release-gate-script scripts/run_storage_engine_phase6_release_gate.sh --db-path /tmp/mir2_phase6_batch2_tombstone_gc_db --backend-state-path /tmp/mir2_phase6_batch2_tombstone_gc_backend_state.txt --report-file /tmp/mir2_phase6_batch2_tombstone_gc.report.txt --acceptance-csv /tmp/mir2_phase6_batch2_tombstone_gc.acceptance.csv
```

**Step 5: Commit**

```bash
git add scripts/run_storage_engine_phase6_single_node_drill.sh scripts/run_storage_engine_phase6_release_gate.sh docs/storage_engine_phase6_single_node_drill_runbook.md
git commit -m "docs(storage): add phase6 kill-point drill coverage"
```

### Task 6: Final verification

**Files:**
- Modify: `docs/storage_engine_phase6_single_node_drill_runbook.md`

**Step 1: Run focused suite**

```bash
./build-wsl/bin/storage_engine_focus_tests --gtest_filter='Phase6*'
```

Expected: PASS.

**Step 2: Run full storage-engine focus suite**

```bash
./build-wsl/bin/storage_engine_focus_tests
```

Expected: PASS except the known benchmark-gated skip.

**Step 3: Verify shell syntax**

```bash
bash -n scripts/run_storage_engine_phase6_single_node_drill.sh
bash -n scripts/run_storage_engine_phase6_release_gate.sh
```

Expected: PASS.

**Step 4: Confirm runbook examples**

Ensure the runbook covers:

1. both scenarios
2. `recover_wait` kill point
3. acceptance CSV fields including `kill_point`

**Step 5: Commit**

```bash
git add docs/storage_engine_phase6_single_node_drill_runbook.md
git commit -m "test(storage): verify phase6 batch2 kill-point matrix"
```
