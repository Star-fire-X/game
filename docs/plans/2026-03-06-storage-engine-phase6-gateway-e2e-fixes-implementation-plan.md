# StorageEngine Phase6 Gateway E2E Fixes Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Land the final P1/P2 gateway E2E fixes so disconnect cleanup flushes at most once per cleanup batch and Phase6 verify uses pre-move baseline capture plus 5-second alternating L2/PostgreSQL validation.

**Architecture:** Keep `CharacterEntityManager` responsible for per-role disconnect save semantics, but move cleanup-level flush orchestration into `LogicServer` so one cleanup batch can aggregate dirty-before-save roles and decide whether to flush once. For the E2E drill, keep L2 inspection inside `StorageEnginePhase6FaultDriver`, add a lightweight snapshot decoder to `mir2_storage_admin`, and let the drill script own baseline capture, retry sequencing, PostgreSQL fallback, and final diagnostics.

**Tech Stack:** C++20 server code, GoogleTest, Bash drill/gate scripts, RocksDB-backed `StorageEngine`, PostgreSQL `kv_store`, FlatBuffers character snapshot codec.

---

### Task 1: P1 red tests for disconnect result aggregation

**Files:**
- Modify: `tests/server/ecs/character_entity_manager_test.cpp`
- Modify: `tests/server/logic/player_mailbox_causal_test.cc`

**Step 1: Write the failing tests**

Add tests that require:
1. `CharacterEntityManager::OnDisconnect()` to report whether the role was dirty before the save path ran.
2. `CharacterEntityManager::OnDisconnect()` to stop flushing the async outbox itself.
3. `LogicServer::CleanupAllClientSessions()` to flush once for a cleanup batch with multiple dirty roles.

**Step 2: Run tests to verify they fail**

Run:

```bash
cmake --build --preset vcpkg-wsl-debug --target storage_engine_focus_tests -j$(nproc)
./build-wsl/bin/storage_engine_focus_tests --gtest_filter='CharacterEntityManagerDirtyTest.*:PlayerMailboxCausalTest.*'
```

Expected: FAIL because `OnDisconnect()` still returns `void` and still flushes per role.

### Task 2: P1 minimal implementation

**Files:**
- Modify: `src/server/ecs/character_entity_manager.h`
- Modify: `src/server/ecs/character_entity_manager.cc`
- Modify: `src/server/logic/logic_server.h`
- Modify: `src/server/logic/logic_server.cc`

**Step 1: Write minimal implementation**

Implement:
1. `DisconnectPersistResult { was_dirty_before_save, save_result }`
2. `OnDisconnect()` returning that result without flushing
3. cleanup aggregation in `LogicServer` with one `StorageEngine::Flush(2000)` after the loop when any role was dirty before save
4. one cleanup log line with `cleanup_dirty_count`, `cleanup_flush_attempted`, and `cleanup_flush_success`

**Step 2: Run targeted tests**

Run:

```bash
./build-wsl/bin/storage_engine_focus_tests --gtest_filter='CharacterEntityManagerDirtyTest.*:PlayerMailboxCausalTest.*'
```

Expected: PASS.

### Task 3: P2 red tests for barrier, decode helper, and verify semantics

**Files:**
- Modify: `tests/server/storage_engine/phase6_gateway_mock_client_test.cc`
- Modify: `tests/server/storage_engine/storage_admin_tool_test.cc`
- Modify: `tests/server/storage_engine/phase6_gateway_e2e_shell_test.cc`

**Step 1: Write the failing tests**

Add tests that require:
1. the mock client to emit a pre-move ready file with dynamic `player_id` and wait for a continue file before sending `MoveReq`
2. `mir2_storage_admin decode-character-snapshot --hex ... --expected-x ... --expected-y ...` to emit `actual_x`, `actual_y`, and `position_matches`
3. the gateway E2E path to accept only when `snapshot_version >= baseline_version + 1`, `version_delta >= 1`, and coordinates match
4. coverage for both `verify_stage=l2` and `verify_stage=postgres_fallback`

**Step 2: Run tests to verify they fail**

Run:

```bash
./build-wsl/bin/storage_engine_focus_tests --gtest_filter='Phase6GatewayMockClientTest.*:StorageAdminToolTest.*:Phase6GatewayE2e*'
```

Expected: FAIL because the current mock client, admin tool, and scripts still use the old persistence check flow.

### Task 4: P2 minimal implementation

**Files:**
- Modify: `src/server/apps/storage_engine_phase6_gateway_mock_client.cc`
- Modify: `src/server/apps/storage_engine_phase6_fault_driver.cc`
- Modify: `src/server/storage_engine/utils/storage_admin_tool.h`
- Modify: `src/server/storage_engine/utils/storage_admin_tool.cc`
- Modify: `scripts/run_storage_engine_phase6_gateway_e2e_drill.sh`
- Modify: `scripts/run_storage_engine_phase6_gateway_e2e_gate.sh`

**Step 1: Write minimal implementation**

Implement:
1. pre-move barrier options and ready-file player-id reporting in the mock client
2. L2 verify output from the fault driver that includes `snapshot_version` and snapshot payload hex
3. snapshot decode helper in `mir2_storage_admin`
4. drill-side baseline capture before `MoveReq`, 25x/200ms alternating L2 and PostgreSQL verify, and final diagnostics
5. gate-side pass/fail checks for the new verify fields

**Step 2: Run targeted tests**

Run:

```bash
./build-wsl/bin/storage_engine_focus_tests --gtest_filter='Phase6GatewayMockClientTest.*:StorageAdminToolTest.*:Phase6GatewayE2e*'
```

Expected: PASS.

### Task 5: Final verification

**Files:**
- Modify: `docs/storage_engine_phase6_gateway_e2e_runbook.md`

**Step 1: Run focused verification**

```bash
./build-wsl/bin/storage_engine_focus_tests --gtest_filter='CharacterEntityManagerDirtyTest.*:PlayerMailboxCausalTest.*:Phase6GatewayMockClientTest.*:StorageAdminToolTest.*:Phase6GatewayE2e*'
```

**Step 2: Run broader storage verification**

```bash
cmake --build --preset vcpkg-wsl-debug --target storage_engine_focus_tests -j$(nproc)
./build-wsl/bin/storage_engine_focus_tests
```

**Step 3: Syntax-check scripts**

```bash
bash -n scripts/run_storage_engine_phase6_gateway_e2e_drill.sh
bash -n scripts/run_storage_engine_phase6_gateway_e2e_gate.sh
```

**Step 4: Update runbook**

Document the pre-move barrier, baseline capture timing, and the new verify output fields.
