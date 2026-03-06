# TCP ConnectionHandle + Actor Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Replace TCP connection write/close internals with single-writer actor semantics while preserving protocol behavior and delivering mandatory write-path observability.

**Architecture:** Introduce `ConnectionHandle` (thread-safe command API) and internal `ConnectionActor` (strand-confined state owner). Keep a one-version compatibility layer (`LegacyTcpConnectionAdapter`) so existing callers remain operational during migration, then migrate `TcpSession/TcpClient` and remove old APIs.

**Tech Stack:** C++20, Asio strand/executor, GoogleTest/MockSocket, Prometheus metrics (or stub).

---

### Task 1: Add connection type primitives (Phase A, RED/GREEN)

**Files:**
- Create: `src/server/network/connection_types.h`
- Test: `tests/server/network/connection_actor_test.cc`

**Step 1: Write failing tests**
- Add compile-time/runtime tests asserting:
  - `CloseReason`, `SendResult`, `FlushPolicy`, `ConnectionState` enums exist.
  - `ConnectionCallbacks` can be constructed with read/closed hooks.
  - `OutboundFrame` holds payload and metadata fields.

**Step 2: Run test to verify it fails**
- Run: `./build-wsl/bin/legend2_tests --gtest_filter='ConnectionActorTest.TypesExist*'`
- Expected: compile or runtime fail for missing types.

**Step 3: Write minimal implementation**
- Define enums/structs with explicit names and initial values required by tests.

**Step 4: Run test to verify it passes**
- Same command, expected PASS.

**Step 5: Commit**
- `git add src/server/network/connection_types.h tests/server/network/connection_actor_test.cc`
- `git commit -m "feat(network): add connection actor type primitives"`

### Task 2: Implement ConnectionHandle + ConnectionActor core send/close FSM (Phase A)

**Files:**
- Create: `src/server/network/connection_handle.h`
- Create: `src/server/network/connection_handle.cc`
- Create: `src/server/network/connection_actor.h`
- Create: `src/server/network/connection_actor.cc`
- Test: `tests/server/network/connection_actor_test.cc`

**Step 1: Write failing tests**
- Add tests for:
  - concurrent `TrySend` + `RequestClose` does not crash and closes once.
  - queue overflow returns `SendResult::kRejectedQueueFull`.
  - closed handle returns `SendResult::kRejectedClosed`.
  - write error triggers `CloseReason::kWriteError`.
  - close callback invoked exactly once.

**Step 2: Run test to verify it fails**
- Run targeted new tests, expect FAIL with unimplemented actor behavior.

**Step 3: Write minimal implementation**
- Actor owns queue/state/inflight, handle posts commands.
- Pump loop one inflight write.
- State transitions `kOpen -> kClosing -> kClosed`.
- Exactly-once close callback guard.

**Step 4: Run tests to verify pass**
- Run new actor tests.

**Step 5: Refactor**
- Extract helper for close finalization and metric emission.

**Step 6: Re-run tests**
- Ensure same test set remains green.

**Step 7: Commit**
- `git add ...`
- `git commit -m "feat(network): introduce connection handle actor model"`

### Task 3: Add mandatory write-path metrics + compatibility mapping (Phase A)

**Files:**
- Modify: `src/server/network/connection_actor.cc`
- Modify: `src/server/monitor/metrics.h`
- Modify: `src/server/monitor/metrics.cc`
- Modify: `src/server/monitor/metrics_stub.cc`
- Test: `tests/server/network/connection_actor_test.cc`
- Test: `tests/server/logic/metrics_smoke_test.cc`

**Step 1: Write failing tests**
- Assert metrics update on:
  - queue depth/inflight transitions
  - send reject reasons
  - close reasons
  - write handler lag observation
  - actor backlog gauge
  - legacy `network.tcp.write_queue_full_total` increments on queue-full rejection

**Step 2: Run tests to verify fail**
- Run actor + metrics smoke subset.

**Step 3: Implement minimal metrics API and emissions**
- Add labeled counter/histogram helper or dedicated methods.
- Emit mandatory metrics in actor command/write callbacks.

**Step 4: Re-run tests**
- Verify metrics expectations and compatibility mapping.

**Step 5: Commit**
- `git add ...`
- `git commit -m "feat(network): add actor write-path observability metrics"`

### Task 4: Wire legacy TcpConnection API through adapter (Phase A)

**Files:**
- Create: `src/server/network/legacy_tcp_connection_adapter.h`
- Create: `src/server/network/legacy_tcp_connection_adapter.cc`
- Modify: `src/server/network/tcp_connection.h`
- Modify: `src/server/network/tcp_connection.cc`
- Modify: `src/server/CMakeLists.txt`
- Test: `tests/server/tcp_connection_test.cpp`

**Step 1: Write failing tests**
- Existing `TcpConnectionTest` should still pass unchanged.
- Add test verifying deprecated runtime handler API path still works through adapter.

**Step 2: Run tests to verify fail**
- Run `TcpConnectionTest.*`, expect failure until adapter wiring complete.

**Step 3: Implement adapter wiring**
- Keep old API surface.
- Internally delegate send/close/start through handle+actor.
- Keep one-version compatibility behavior.

**Step 4: Re-run tests**
- Ensure legacy tests pass.

**Step 5: Commit**
- `git add ...`
- `git commit -m "refactor(network): route TcpConnection through legacy adapter"`

### Task 5: Migrate TcpSession to ConnectionHandle semantics (Phase B)

**Files:**
- Modify: `src/server/network/tcp_session.h`
- Modify: `src/server/network/tcp_session.cc`
- Modify: `tests/server/tcp_connection_test.cpp`
- Modify: `tests/server/network/tcp_session_protocol_gate_test.cc`

**Step 1: Write failing tests**
- Add `flush_required` close policy behavior around `Kick`.
- Assert no post-kick business packets.

**Step 2: Run, fail, implement minimal changes**
- Replace direct connection write/close with handle operations.

**Step 3: Re-run and refactor**
- Keep external `session->Send(...)` signature stable.

### Task 6: Migrate TcpClient lifecycle model (Phase B)

**Files:**
- Modify: `src/server/network/tcp_client.h`
- Modify: `src/server/network/tcp_client.cc`
- Modify: `tests/server/network/tcp_client_test.cc`

**Step 1: Write failing tests**
- Reconnect epoch isolation test: stale callbacks do not affect new connection.

**Step 2: Implement**
- Replace connection+mutex+generation race-prone state with handle+epoch model.

**Step 3: Verify**
- Run TcpClient targeted tests.

### Task 7: Migrate NetworkManager/GatewayServer callback binding (Phase B)

**Files:**
- Modify: `src/server/network/network_manager.cc`
- Modify: `src/server/gateway/gateway_server.cc`
- Related tests in `tests/server/gateway_*`, `tests/integration/gateway_logic_*`

**Step 1: Write failing integration/unit tests**
- Ensure constructor-time callback binding path works for accepted connections.

**Step 2: Implement and verify**
- Remove runtime handler replacement usage in migrated path.

### Task 8: Remove deprecated APIs and adapter (Phase C)

**Files:**
- Modify/Delete: legacy runtime callback and old DoWrite queue code paths
- Update docs and constraints

**Step 1: Write failing compile/tests**
- Remove old API usage from code/tests.

**Step 2: Implement cleanup**
- Delete `SetReadHandler/SetDisconnectHandler` runtime mutation API.
- Remove adapter and old queue/write branches.

**Step 3: Verify full suite subset + pressure gates**
- Required: half-baseline x3 + full baseline x1.

