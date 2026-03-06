# StorageEngine Phase6 Batch6 Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Add a `mock client -> gateway -> logic -> StorageEngine -> PostgreSQL` end-to-end Phase6 drill that proves one real storage read and one real storage write on a healthy path.

**Architecture:** Reuse the existing Phase6 fault-driver seeding flow and logic-process drill patterns, but insert `mir2_gateway` plus a dedicated Phase6 mock-client binary into the orchestration. The business path is `login -> select_role -> move -> disconnect`, because it exercises a real account read on login and a real `char:<player_id>` write on disconnect after movement marks the entity dirty.

**Tech Stack:** Bash orchestration, GoogleTest shell tests, `mir2_gateway`, `mir2_logic`, `mir2_storage_admin`, Phase6 fault driver, new C++ mock client using existing packet/message codecs.

---

### Task 1: Add RED shell tests for gateway E2E gate/drill

**Files:**
- Create: `tests/server/storage_engine/phase6_gateway_e2e_shell_test.cc`
- Modify: `tests/CMakeLists.txt`

**Step 1: Write the failing tests**

Add tests that expect:
1. `run_storage_engine_phase6_gateway_e2e_gate.sh` can pass a healthy mock report.
2. `run_storage_engine_phase6_gateway_e2e_drill.sh` can record an acceptance row for `login_select_move_disconnect`.

**Step 2: Run tests to verify they fail**

Run:

```bash
cmake --build --preset vcpkg-wsl-debug --target storage_engine_focus_tests -j$(nproc)
./build-wsl/bin/storage_engine_focus_tests --gtest_filter='Phase6GatewayE2e*'
```

Expected: FAIL because the new gate/drill scripts do not exist yet.

**Step 3: Commit**

```bash
git add tests/server/storage_engine/phase6_gateway_e2e_shell_test.cc tests/CMakeLists.txt docs/plans/2026-03-06-storage-engine-phase6-batch6-gateway-e2e-design.md docs/plans/2026-03-06-storage-engine-phase6-batch6-implementation-plan.md
git commit -m "test(storage): add phase6 gateway e2e red tests"
```

### Task 2: Implement gateway E2E gate script

**Files:**
- Create: `scripts/run_storage_engine_phase6_gateway_e2e_gate.sh`
- Modify: `tests/server/storage_engine/phase6_gateway_e2e_shell_test.cc`

**Step 1: Write/expand failing gate expectations**

Add assertions for:
1. `login_rsp_code=ERR_OK`
2. `select_role_rsp_code=ERR_OK`
3. `move_rsp_code=ERR_OK`
4. `disconnect_sent=true`
5. `validate total_corrupted=0`

**Step 2: Run gate-focused test to verify it fails**

```bash
./build-wsl/bin/storage_engine_focus_tests --gtest_filter='Phase6GatewayE2eGateScriptTest.*'
```

Expected: FAIL because gate script logic is missing.

**Step 3: Write minimal implementation**

Implement the gate script to parse the mock client report plus validate output and emit one `phase6_gateway_e2e_gate_result` line.

**Step 4: Run test to verify it passes**

```bash
./build-wsl/bin/storage_engine_focus_tests --gtest_filter='Phase6GatewayE2eGateScriptTest.*'
```

Expected: PASS.

**Step 5: Commit**

```bash
git add scripts/run_storage_engine_phase6_gateway_e2e_gate.sh tests/server/storage_engine/phase6_gateway_e2e_shell_test.cc
git commit -m "test(storage): add phase6 gateway e2e gate"
```

### Task 3: Implement gateway E2E drill shell skeleton

**Files:**
- Create: `scripts/run_storage_engine_phase6_gateway_e2e_drill.sh`
- Modify: `tests/server/storage_engine/phase6_gateway_e2e_shell_test.cc`

**Step 1: Add failing drill expectations**

Require the drill to:
1. accept gateway/logic/mock-client/admin/fault-driver binary paths
2. write acceptance CSV
3. call the gate script

**Step 2: Run drill-focused test to verify it fails**

```bash
./build-wsl/bin/storage_engine_focus_tests --gtest_filter='Phase6GatewayE2eDrillScriptTest.*'
```

Expected: FAIL because drill orchestration is missing.

**Step 3: Write minimal shell implementation**

Implement the shell scaffold with mock-compatible orchestration and acceptance CSV output.

**Step 4: Run drill-focused tests**

```bash
./build-wsl/bin/storage_engine_focus_tests --gtest_filter='Phase6GatewayE2eDrillScriptTest.*'
```

Expected: PASS.

**Step 5: Commit**

```bash
git add scripts/run_storage_engine_phase6_gateway_e2e_drill.sh tests/server/storage_engine/phase6_gateway_e2e_shell_test.cc
git commit -m "test(storage): add phase6 gateway e2e drill shell"
```

### Task 4: Add real gateway mock-client binary

**Files:**
- Create: `src/server/apps/storage_engine_phase6_gateway_mock_client.cc`
- Create: `src/server/apps/storage_engine_phase6_gateway_mock_client.h`
- Create: `src/server/apps/storage_engine_phase6_gateway_mock_client_main.cc`
- Modify: `src/server/CMakeLists.txt`
- Modify: `CMakeLists.txt`

**Step 1: Write the failing integration-facing test or smoke harness expectation**

Extend shell tests or add a focused test expectation that the drill can launch a mock client binary and capture a report with login/select/move response codes.

**Step 2: Run targeted tests and observe failure**

```bash
./build-wsl/bin/storage_engine_focus_tests --gtest_filter='Phase6GatewayE2eDrillScriptTest.*'
```

Expected: FAIL because no real mock-client binary exists.

**Step 3: Write minimal implementation**

Implement a small TCP client using existing packet/message codecs that:
1. connects to gateway
2. sends `LoginReq`
3. waits for `LoginRsp`
4. sends `SelectRoleReq`
5. waits for `SelectRoleRsp`
6. sends `MoveReq`
7. waits for `MoveRsp`
8. disconnects
9. writes a one-line report

**Step 4: Build and verify**

```bash
cmake --build --preset vcpkg-wsl-debug --target mir2_storage_engine_phase6_gateway_mock_client -j$(nproc)
```

Expected: target builds successfully.

**Step 5: Commit**

```bash
git add src/server/apps/storage_engine_phase6_gateway_mock_client.* src/server/CMakeLists.txt CMakeLists.txt
git commit -m "feat(storage): add phase6 gateway mock client"
```

### Task 5: Wire real gateway + logic + PostgreSQL smoke

**Files:**
- Modify: `scripts/run_storage_engine_phase6_gateway_e2e_drill.sh`
- Modify: `scripts/run_storage_engine_phase6_gateway_e2e_gate.sh`
- Create: `docs/storage_engine_phase6_gateway_e2e_runbook.md`

**Step 1: Write/expand failing expectations**

Require the drill to:
1. seed account + character
2. launch logic
3. launch gateway
4. run mock client
5. wait for disconnect
6. validate `char:<player_id>` writeback

**Step 2: Run shell tests to verify expected failures**

```bash
./build-wsl/bin/storage_engine_focus_tests --gtest_filter='Phase6GatewayE2e*'
```

Expected: any remaining drill/gate assertions fail before implementation.

**Step 3: Write minimal integration implementation**

Make the drill produce:
1. client report
2. logic log
3. gateway log
4. validate file
5. acceptance CSV

**Step 4: Run real smoke**

```bash
cmake --build --preset vcpkg-wsl-debug --target mir2_gateway mir2_logic mir2_storage_admin mir2_storage_engine_phase6_fault_driver mir2_storage_engine_phase6_gateway_mock_client -j$(nproc)
bash scripts/run_storage_engine_phase6_gateway_e2e_drill.sh ...
```

Expected: `phase6_gateway_e2e_drill_result status=pass`.

**Step 5: Commit**

```bash
git add scripts/run_storage_engine_phase6_gateway_e2e_*.sh docs/storage_engine_phase6_gateway_e2e_runbook.md
git commit -m "feat(storage): add phase6 gateway e2e drill"
```

### Task 6: Final verification

**Files:**
- Modify: `docs/storage_engine_phase6_gateway_e2e_runbook.md`

**Step 1: Run targeted suite**

```bash
./build-wsl/bin/storage_engine_focus_tests --gtest_filter='Phase6GatewayE2e*'
```

**Step 2: Run storage focus suite**

```bash
./build-wsl/bin/storage_engine_focus_tests
```

**Step 3: Syntax-check scripts**

```bash
bash -n scripts/run_storage_engine_phase6_gateway_e2e_gate.sh
bash -n scripts/run_storage_engine_phase6_gateway_e2e_drill.sh
```

**Step 4: Update runbook with verified commands**

Document the real PostgreSQL smoke command and expected outputs.

**Step 5: Commit**

```bash
git add docs/storage_engine_phase6_gateway_e2e_runbook.md
git commit -m "docs(storage): document phase6 gateway e2e drill"
```
