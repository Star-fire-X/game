# StorageEngine Phase6 Batch4 Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Add a `checkpoint_restore` drill to Phase6 so single-node pre-release rehearsals can validate offline checkpoint creation, restore, and post-restore data integrity.

**Architecture:** Reuse the existing Phase6 fault driver, single-node drill script, and release gate as the orchestration shell. The prepare phase writes a deterministic key to the source DB, the drill script kills the prepare process, invokes `mir2_storage_admin checkpoint-create` and `checkpoint-restore`, then runs a dedicated recover phase against the restored DB path and validates the restored DB with `storage_admin validate`.

**Tech Stack:** C++20, StorageEngine, RocksDB, GoogleTest, bash scripts, `mir2_storage_admin`

---

### Task 1: Add RED tests for checkpoint_restore shell flow

**Files:**
- Modify: `tests/server/storage_engine/phase6_shell_script_test.cc`
- Test: `tests/server/storage_engine/phase6_shell_script_test.cc`

**Step 1: Write the failing test**

Add shell tests that require:

1. `run_storage_engine_phase6_release_gate.sh` to accept `scenario=checkpoint_restore`
2. the release gate to pass when:
   - driver summary reports `restored_key_present=true`
   - checkpoint-create summary reports `status=ok`
   - checkpoint-restore summary reports `status=ok`
   - validate summary reports `total_corrupted=0`
3. `run_storage_engine_phase6_single_node_drill.sh` to accept `scenario=checkpoint_restore`

**Step 2: Run test to verify it fails**

Run:

```bash
./build-wsl/bin/storage_engine_focus_tests --gtest_filter='Phase6ReleaseGateScriptTest.*CheckpointRestore*:Phase6SingleNodeDrillScriptTest.*CheckpointRestore*'
```

Expected: FAIL because the gate and drill do not handle `checkpoint_restore` yet.

**Step 3: Write minimal implementation**

Do not implement yet in this task.

**Step 4: Re-run test to verify failure mode**

Confirm failure is caused by missing scenario support, not malformed tests.

**Step 5: Commit**

```bash
git add tests/server/storage_engine/phase6_shell_script_test.cc
git commit -m "test(storage): add phase6 checkpoint restore red tests"
```

### Task 2: Extend Phase6 release gate for checkpoint restore

**Files:**
- Modify: `scripts/run_storage_engine_phase6_release_gate.sh`
- Modify: `tests/server/storage_engine/phase6_shell_script_test.cc`

**Step 1: Write the failing test**

Require:

1. `--scenario checkpoint_restore` accepted
2. optional `--checkpoint-create-file` and `--checkpoint-restore-file`
3. gate pass criteria:
   - `restored_key_present=true`
   - checkpoint-create summary `status=ok`
   - checkpoint-restore summary `status=ok`
   - validate summary `total_corrupted=0`

**Step 2: Run test to verify it fails**

Run:

```bash
./build-wsl/bin/storage_engine_focus_tests --gtest_filter='Phase6ReleaseGateScriptTest.*CheckpointRestore*'
```

Expected: FAIL with invalid scenario or missing summary-file support.

**Step 3: Write minimal implementation**

Update the release gate to:

1. accept `checkpoint_restore`
2. parse checkpoint-create and checkpoint-restore summaries
3. apply checkpoint-restore-specific pass/fail reasons

**Step 4: Run test to verify it passes**

Run:

```bash
./build-wsl/bin/storage_engine_focus_tests --gtest_filter='Phase6ReleaseGateScriptTest.*'
```

Expected: PASS.

**Step 5: Commit**

```bash
git add scripts/run_storage_engine_phase6_release_gate.sh tests/server/storage_engine/phase6_shell_script_test.cc
git commit -m "feat(storage): add phase6 checkpoint restore gate"
```

### Task 3: Extend fault driver with checkpoint prepare/recover

**Files:**
- Modify: `src/server/apps/storage_engine_phase6_fault_driver.cc`
- Modify: `src/server/apps/storage_engine_phase6_fault_driver.h`

**Step 1: Write the failing test**

Require the driver to support:

1. `checkpoint_restore_prepare`
2. `checkpoint_restore_recover`
3. prepare phase writes a deterministic key/value into the source DB
4. recover phase opens the restored DB path and reports `restored_key_present=true`

**Step 2: Run test to verify it fails**

Use the checkpoint_restore shell tests from Task 1 after the gate accepts the scenario.

Expected: FAIL because the driver does not yet support checkpoint_restore.

**Step 3: Write minimal implementation**

In the driver:

1. add scenario name parsing for `checkpoint_restore_prepare` / `checkpoint_restore_recover`
2. prepare phase writes the sample key and emits ready
3. recover phase opens the restored DB and reports whether the key is readable

**Step 4: Run test to verify it passes**

Run:

```bash
./build-wsl/bin/storage_engine_focus_tests --gtest_filter='Phase6*'
```

Expected: PASS for the new checkpoint-restore tests.

**Step 5: Commit**

```bash
git add src/server/apps/storage_engine_phase6_fault_driver.h src/server/apps/storage_engine_phase6_fault_driver.cc
git commit -m "feat(storage): add phase6 checkpoint restore driver"
```

### Task 4: Extend single-node drill orchestration for checkpoint restore

**Files:**
- Modify: `scripts/run_storage_engine_phase6_single_node_drill.sh`
- Modify: `tests/server/storage_engine/phase6_shell_script_test.cc`

**Step 1: Write the failing test**

Require drill behavior for:

1. `--scenario checkpoint_restore`
2. a generated checkpoint path and restore DB path
3. invocation of `mir2_storage_admin checkpoint-create`
4. invocation of `mir2_storage_admin checkpoint-restore`
5. acceptance CSV stores the scenario and pass result

**Step 2: Run test to verify it fails**

Run:

```bash
./build-wsl/bin/storage_engine_focus_tests --gtest_filter='Phase6SingleNodeDrillScriptTest.*CheckpointRestore*'
```

Expected: FAIL because the drill does not orchestrate checkpoint restore yet.

**Step 3: Write minimal implementation**

Update the drill script to:

1. accept `checkpoint_restore`
2. create a checkpoint after killing prepare
3. restore to a new DB path
4. run recover against the restored DB
5. feed checkpoint summaries into the release gate

**Step 4: Run test to verify it passes**

Run:

```bash
./build-wsl/bin/storage_engine_focus_tests --gtest_filter='Phase6SingleNodeDrillScriptTest.*'
```

Expected: PASS.

**Step 5: Commit**

```bash
git add scripts/run_storage_engine_phase6_single_node_drill.sh tests/server/storage_engine/phase6_shell_script_test.cc
git commit -m "feat(storage): add phase6 checkpoint restore drill"
```

### Task 5: Add real smoke for checkpoint restore drill

**Files:**
- Modify: `docs/storage_engine_phase6_single_node_drill_runbook.md`

**Step 1: Run real drill**

```bash
bash scripts/run_storage_engine_phase6_single_node_drill.sh \
  --scenario checkpoint_restore \
  --driver-bin ./build-wsl/bin/mir2_storage_engine_phase6_fault_driver \
  --admin-bin ./build-wsl/bin/mir2_storage_admin \
  --release-gate-script scripts/run_storage_engine_phase6_release_gate.sh \
  --db-path /tmp/mir2_phase6_batch4_checkpoint_restore_db \
  --backend-state-path /tmp/mir2_phase6_batch4_checkpoint_restore_backend_state.txt \
  --report-file /tmp/mir2_phase6_batch4_checkpoint_restore.report.txt \
  --acceptance-csv /tmp/mir2_phase6_batch4_checkpoint_restore.acceptance.csv
```

Expected:

1. checkpoint-create summary `status=ok`
2. checkpoint-restore summary `status=ok`
3. driver summary `restored_key_present=true`
4. validate summary `total_corrupted=0`

**Step 2: Update runbook**

Add:

1. new scenario `checkpoint_restore`
2. exact smoke command
3. pass conditions and artifact expectations

**Step 3: Commit**

```bash
git add docs/storage_engine_phase6_single_node_drill_runbook.md
git commit -m "docs(storage): document phase6 checkpoint restore drill"
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

Check checkpoint-create summary, checkpoint-restore summary, recover summary, validate summary, and acceptance CSV.

**Step 5: Commit**

```bash
git add docs/storage_engine_phase6_single_node_drill_runbook.md
git commit -m "test(storage): verify phase6 batch4 checkpoint restore drill"
```
