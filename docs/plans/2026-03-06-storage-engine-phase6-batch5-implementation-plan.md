# StorageEngine Phase6 Batch5 Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Lift the Phase6 pre-release rehearsal from StorageEngine-only binaries to `mir2_logic` process startup drills backed by a real PostgreSQL instance.

**Architecture:** Reuse the existing Phase6 single-node assets to prepare the underlying storage state, then add a new `mir2_logic` process drill wrapper that generates a temporary `logic.yaml`, launches `mir2_logic --config ...`, and judges startup success/failure from process exit code, log output, and `mir2_storage_admin validate/health`. Keep scope limited to startup-path scenarios: `startup_validation` and `checkpoint_restore`.

**Tech Stack:** C++20, bash scripts, `mir2_logic`, `mir2_storage_admin`, StorageEngine, RocksDB, PostgreSQL

---

### Task 1: Add RED shell tests for logic-process Phase6 drill

**Files:**
- Create: `tests/server/storage_engine/phase6_logic_process_shell_test.cc`
- Modify: `tests/CMakeLists.txt`

**Step 1: Write the failing test**

Add shell tests that require:

1. `run_storage_engine_phase6_logic_process_gate.sh` to accept:
   - `startup_validation`
   - `checkpoint_restore`
2. `run_storage_engine_phase6_logic_process_drill.sh` to:
   - generate acceptance CSV
   - launch a configurable logic binary
   - capture stdout/stderr log file

Use mock shell binaries in the test fixture to simulate:

1. `mir2_logic` startup failure with corruption
2. `mir2_logic` startup success on restored DB
3. `mir2_storage_admin validate/health`

**Step 2: Run test to verify it fails**

Run:

```bash
./build-wsl/bin/storage_engine_focus_tests --gtest_filter='Phase6LogicProcess*'
```

Expected: FAIL because the new logic-process scripts do not exist yet.

**Step 3: Write minimal implementation**

Do not implement yet in this task.

**Step 4: Re-run test to confirm real RED**

Failure must be due to missing scripts or unsupported scenario handling.

**Step 5: Commit**

```bash
git add tests/server/storage_engine/phase6_logic_process_shell_test.cc tests/CMakeLists.txt
git commit -m "test(storage): add phase6 logic-process red tests"
```

### Task 2: Add logic-process release gate

**Files:**
- Create: `scripts/run_storage_engine_phase6_logic_process_gate.sh`
- Modify: `tests/server/storage_engine/phase6_logic_process_shell_test.cc`

**Step 1: Write the failing test**

Require gate behavior for:

1. `startup_validation`
   - logic process exits non-zero
   - validate summary `total_corrupted>0`
2. `checkpoint_restore`
   - logic process stays up / exits 0 depending on wrapper
   - validate summary `total_corrupted=0`

**Step 2: Run test to verify it fails**

Run:

```bash
./build-wsl/bin/storage_engine_focus_tests --gtest_filter='Phase6LogicProcessGateScriptTest.*'
```

Expected: FAIL because the gate script does not exist.

**Step 3: Write minimal implementation**

Create the gate script to:

1. accept `--scenario`
2. accept `--logic-log-file`, `--health-file`, `--validate-file`
3. parse the summaries and log text
4. emit a single `phase6_logic_process_gate_result` line

**Step 4: Run test to verify it passes**

Run:

```bash
./build-wsl/bin/storage_engine_focus_tests --gtest_filter='Phase6LogicProcessGateScriptTest.*'
```

Expected: PASS.

**Step 5: Commit**

```bash
git add scripts/run_storage_engine_phase6_logic_process_gate.sh tests/server/storage_engine/phase6_logic_process_shell_test.cc
git commit -m "feat(storage): add phase6 logic-process gate"
```

### Task 3: Add logic-process drill wrapper

**Files:**
- Create: `scripts/run_storage_engine_phase6_logic_process_drill.sh`
- Modify: `tests/server/storage_engine/phase6_logic_process_shell_test.cc`

**Step 1: Write the failing test**

Require drill behavior for:

1. generate temporary config file
2. override:
   - `storage_engine.l2_path`
   - PostgreSQL connection fields
   - log path
3. launch a configurable logic binary
4. capture logic stdout/stderr
5. invoke `mir2_storage_admin health` and `validate`
6. invoke the new logic-process gate
7. write acceptance CSV

**Step 2: Run test to verify it fails**

Run:

```bash
./build-wsl/bin/storage_engine_focus_tests --gtest_filter='Phase6LogicProcessDrillScriptTest.*'
```

Expected: FAIL because the drill wrapper does not exist.

**Step 3: Write minimal implementation**

Create the drill wrapper to:

1. accept `--scenario`, `--logic-bin`, `--admin-bin`, `--config-template`
2. write a temp config with overridden DB/L2/log paths
3. start the logic process
4. collect logs, health, validate
5. feed everything into the gate
6. write acceptance CSV

**Step 4: Run test to verify it passes**

Run:

```bash
./build-wsl/bin/storage_engine_focus_tests --gtest_filter='Phase6LogicProcess*'
```

Expected: PASS.

**Step 5: Commit**

```bash
git add scripts/run_storage_engine_phase6_logic_process_drill.sh tests/server/storage_engine/phase6_logic_process_shell_test.cc
git commit -m "feat(storage): add phase6 logic-process drill"
```

### Task 4: Add real startup_validation logic-process smoke

**Files:**
- Modify: `docs/storage_engine_phase6_logic_process_runbook.md` (create)

**Step 1: Run real startup_validation drill**

Run with real PostgreSQL and real `mir2_logic`:

```bash
bash scripts/run_storage_engine_phase6_logic_process_drill.sh \
  --scenario startup_validation \
  --logic-bin ./build-wsl/bin/mir2_logic \
  --admin-bin ./build-wsl/bin/mir2_storage_admin \
  --config-template config/logic.yaml \
  --db-host 127.0.0.1 \
  --db-port 5432 \
  --db-user mir2 \
  --db-password mir2_password \
  --db-name mir2_game \
  --db-path /tmp/mir2_phase6_logic_startup_validation_db \
  --report-file /tmp/mir2_phase6_logic_startup_validation.report.txt \
  --acceptance-csv /tmp/mir2_phase6_logic_startup_validation.acceptance.csv
```

Expected:

1. logic process fails closed
2. validate shows `total_corrupted>0`
3. gate passes because failure was expected

**Step 2: Document runbook**

Create:

`docs/storage_engine_phase6_logic_process_runbook.md`

Include:

1. PostgreSQL prerequisites
2. startup_validation command
3. expected logs and pass criteria

**Step 3: Commit**

```bash
git add docs/storage_engine_phase6_logic_process_runbook.md
git commit -m "docs(storage): add phase6 logic startup validation runbook"
```

### Task 5: Add real checkpoint_restore logic-process smoke

**Files:**
- Modify: `docs/storage_engine_phase6_logic_process_runbook.md`

**Step 1: Run real checkpoint_restore drill**

Run with real PostgreSQL and real `mir2_logic`:

```bash
bash scripts/run_storage_engine_phase6_logic_process_drill.sh \
  --scenario checkpoint_restore \
  --logic-bin ./build-wsl/bin/mir2_logic \
  --admin-bin ./build-wsl/bin/mir2_storage_admin \
  --config-template config/logic.yaml \
  --db-host 127.0.0.1 \
  --db-port 5432 \
  --db-user mir2 \
  --db-password mir2_password \
  --db-name mir2_game \
  --db-path /tmp/mir2_phase6_logic_checkpoint_restore_db \
  --report-file /tmp/mir2_phase6_logic_checkpoint_restore.report.txt \
  --acceptance-csv /tmp/mir2_phase6_logic_checkpoint_restore.acceptance.csv
```

Expected:

1. logic process starts successfully
2. validate shows `total_corrupted=0`
3. gate passes

**Step 2: Update runbook**

Add:

1. `checkpoint_restore` command
2. expected artifacts
3. expected startup-success criteria

**Step 3: Commit**

```bash
git add docs/storage_engine_phase6_logic_process_runbook.md
git commit -m "docs(storage): add phase6 logic checkpoint restore runbook"
```

### Task 6: Final verification

**Files:**
- Modify: `docs/storage_engine_phase6_logic_process_runbook.md`

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
bash -n scripts/run_storage_engine_phase6_logic_process_gate.sh
bash -n scripts/run_storage_engine_phase6_logic_process_drill.sh
```

Expected: PASS.

**Step 4: Verify real smoke artifacts**

Check acceptance CSV, logic stdout/stderr log, health summary, validate summary, and gate report for both scenarios.

**Step 5: Commit**

```bash
git add docs/storage_engine_phase6_logic_process_runbook.md
git commit -m "test(storage): verify phase6 logic-process drills"
```
