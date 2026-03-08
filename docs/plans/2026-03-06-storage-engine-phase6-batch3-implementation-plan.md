# StorageEngine Phase6 Batch3 Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Add a startup-validation-and-corruption drill path to Phase6 so single-node pre-release rehearsals can verify fail-closed startup behavior on corrupted L2 data.

**Architecture:** Reuse the existing Phase6 fault driver, single-node drill script, and release gate. Add a dedicated `startup_validation` scenario where the prepare phase writes data then corrupts the L2 payload on disk, and the recover phase attempts startup with `startup_fail_on_validation_error=true`. The release gate treats a startup refusal plus corruption detection as the expected pass condition for this drill.

**Tech Stack:** C++20, StorageEngine, RocksDB, GoogleTest, bash scripts, `mir2_storage_admin`

---

### Task 1: Add RED shell tests for startup validation corruption drill

**Files:**
- Modify: `tests/server/storage_engine/phase6_shell_script_test.cc`
- Test: `tests/server/storage_engine/phase6_shell_script_test.cc`

**Step 1: Write the failing test**

Add tests that require:

1. `run_storage_engine_phase6_release_gate.sh` to accept `scenario=startup_validation`
2. the gate to pass when:
   - driver summary reports `status=init_failed`
   - validate summary reports `total_corrupted=1`
3. `run_storage_engine_phase6_single_node_drill.sh` to accept `scenario=startup_validation`

**Step 2: Run test to verify it fails**

Run:

```bash
./build-wsl/bin/storage_engine_focus_tests --gtest_filter='Phase6ReleaseGateScriptTest.StartupValidation*:Phase6SingleNodeDrillScriptTest.StartupValidation*'
```

Expected: FAIL because neither the gate nor the drill supports the new scenario yet.

**Step 3: Write minimal implementation**

Do not implement yet in this task.

**Step 4: Re-run test to verify failure mode**

Confirm failure is due to missing scenario handling, not malformed tests.

**Step 5: Commit**

```bash
git add tests/server/storage_engine/phase6_shell_script_test.cc
git commit -m "test(storage): add phase6 startup validation red tests"
```

### Task 2: Extend Phase6 release gate for startup validation

**Files:**
- Modify: `scripts/run_storage_engine_phase6_release_gate.sh`
- Modify: `tests/server/storage_engine/phase6_shell_script_test.cc`

**Step 1: Write the failing test**

Require:

1. `--scenario startup_validation` accepted
2. gate passes only when:
   - driver summary `status=init_failed`
   - validate summary `total_corrupted>0`

**Step 2: Run test to verify it fails**

Run:

```bash
./build-wsl/bin/storage_engine_focus_tests --gtest_filter='Phase6ReleaseGateScriptTest.StartupValidation*'
```

Expected: FAIL with invalid scenario or missing scenario-specific gate handling.

**Step 3: Write minimal implementation**

Update the release gate to:

1. accept `startup_validation`
2. apply scenario-specific pass criteria:
   - `driver_status==init_failed`
   - `validate_total_corrupted>0`

**Step 4: Run test to verify it passes**

Run:

```bash
./build-wsl/bin/storage_engine_focus_tests --gtest_filter='Phase6ReleaseGateScriptTest.*'
```

Expected: PASS.

**Step 5: Commit**

```bash
git add scripts/run_storage_engine_phase6_release_gate.sh tests/server/storage_engine/phase6_shell_script_test.cc
git commit -m "feat(storage): add phase6 startup validation gate"
```

### Task 3: Extend fault driver with corruption injection and startup validation scenario

**Files:**
- Modify: `src/server/apps/storage_engine_phase6_fault_driver.cc`
- Modify: `src/server/apps/storage_engine_phase6_fault_driver.h`

**Step 1: Write the failing test**

Require the real drill to support:

1. `startup_validation_prepare`
2. `startup_validation_recover`
3. prepare phase writes valid data, then corrupts raw L2 payload
4. recover phase exits with `init_failed` when startup validation gate is enabled

**Step 2: Run test to verify it fails**

Use the shell tests from Task 1 after the release gate supports the scenario.

Expected: FAIL because the driver does not yet support corruption injection.

**Step 3: Write minimal implementation**

In the driver:

1. add `startup_validation_prepare`
2. add raw L2 corruption helper (reuse the same RocksDB open/corrupt pattern already used by tests)
3. add `startup_validation_recover` with:
   - `startup_fail_on_validation_error=true`
   - summary output `status=init_failed` on expected validation refusal

**Step 4: Run test to verify it passes**

Run:

```bash
./build-wsl/bin/storage_engine_focus_tests --gtest_filter='Phase6*'
```

Expected: PASS for the new startup validation tests.

**Step 5: Commit**

```bash
git add src/server/apps/storage_engine_phase6_fault_driver.h src/server/apps/storage_engine_phase6_fault_driver.cc
git commit -m "feat(storage): add phase6 startup validation corruption driver"
```

### Task 4: Extend single-node drill orchestration for startup validation

**Files:**
- Modify: `scripts/run_storage_engine_phase6_single_node_drill.sh`
- Modify: `tests/server/storage_engine/phase6_shell_script_test.cc`

**Step 1: Write the failing test**

Require drill behavior for:

1. `--scenario startup_validation`
2. no health-file dependency before startup succeeds
3. acceptance CSV records the scenario and `fail_closed` outcome

**Step 2: Run test to verify it fails**

Run:

```bash
./build-wsl/bin/storage_engine_focus_tests --gtest_filter='Phase6SingleNodeDrillScriptTest.StartupValidation*'
```

Expected: FAIL due to unsupported scenario in drill orchestration.

**Step 3: Write minimal implementation**

Update the drill script to:

1. accept `startup_validation`
2. call prepare + kill + recover
3. still run `storage_admin validate`
4. skip `health` if startup did not succeed and only validate is meaningful
5. feed results into the release gate

**Step 4: Run test to verify it passes**

Run:

```bash
./build-wsl/bin/storage_engine_focus_tests --gtest_filter='Phase6SingleNodeDrillScriptTest.*'
```

Expected: PASS.

**Step 5: Commit**

```bash
git add scripts/run_storage_engine_phase6_single_node_drill.sh tests/server/storage_engine/phase6_shell_script_test.cc
git commit -m "feat(storage): add phase6 startup validation drill"
```

### Task 5: Add real smoke for corrupted-startup fail-closed path

**Files:**
- Modify: `docs/storage_engine_phase6_single_node_drill_runbook.md`

**Step 1: Run real drill**

```bash
bash scripts/run_storage_engine_phase6_single_node_drill.sh \
  --scenario startup_validation \
  --driver-bin ./build-wsl/bin/mir2_storage_engine_phase6_fault_driver \
  --admin-bin ./build-wsl/bin/mir2_storage_admin \
  --release-gate-script scripts/run_storage_engine_phase6_release_gate.sh \
  --db-path /tmp/mir2_phase6_batch3_startup_validation_db \
  --backend-state-path /tmp/mir2_phase6_batch3_startup_validation_backend_state.txt \
  --report-file /tmp/mir2_phase6_batch3_startup_validation.report.txt \
  --acceptance-csv /tmp/mir2_phase6_batch3_startup_validation.acceptance.csv
```

Expected:

1. driver summary reports `status=init_failed`
2. release gate passes for fail-closed behavior
3. validate output reports `total_corrupted>0`

**Step 2: Update runbook**

Add:

1. new scenario `startup_validation`
2. exact smoke command
3. expected artifacts and pass conditions

**Step 3: Commit**

```bash
git add docs/storage_engine_phase6_single_node_drill_runbook.md
git commit -m "docs(storage): document phase6 startup validation drill"
```

### Task 6: Final verification

**Files:**
- Modify: `docs/storage_engine_phase6_single_node_drill_runbook.md`

**Step 1: Run Phase6-focused suite**

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

**Step 4: Verify real smoke artifacts**

Check acceptance CSV, report, validate output, and gate report for the startup-validation scenario.

**Step 5: Commit**

```bash
git add docs/storage_engine_phase6_single_node_drill_runbook.md
git commit -m "test(storage): verify phase6 batch3 startup validation drill"
```
