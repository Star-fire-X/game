# Logic Mailbox & Coroutine Improvement Plan (2026-02-19)

## 1. Scope and Goals

This document converts the identified coroutine/mailbox risks into an implementation plan.

Primary goals:

1. Prevent player mailbox runner crash from causing permanent message starvation.
2. Make client logout/disconnect and in-flight coroutine behavior deterministic.
3. Improve observability for coroutine failures and mailbox lifecycle.
4. Optimize high-fanout chat dispatch path without over-scheduling coroutine tasks.

Out-of-scope for first delivery:

1. Preemptive cancellation of blocking I/O already running in thread pool.
2. Deep redesign of `EntityLaneScheduler`.

---

## 2. Risk Validation Summary

| ID | Risk | Validation | Priority |
|---|---|---|---|
| R1 | `RunPlayerMailbox` has no top-level exception guard | Confirmed. Can terminate runner and leave mailbox state inconsistent | P0 |
| R2 | Cancellation is incomplete (`Async`, `WhenAll`) | Partially confirmed. Missing cooperative cancellation propagation | P2/P3 |
| R3 | Chat dispatch sends sequentially and may become O(n) hot path | Confirmed as CPU/send-loop concern; not classic async blocking | P2 |
| R4 | Player exit cleanup vs in-flight coroutine consistency | Partially confirmed. Queue cleanup exists, in-flight early-exit semantics missing | P1 |
| R5 | Unhandled exception logs lack business context | Confirmed | P0 |
| R6 | Most handlers have no local exception guard | Confirmed | P1 |
| R7 | Lane lock permanent hang under process-fatal termination | Theoretical only | P3 (low) |

---

## 3. Phased Delivery Plan

### P0: Stability Stop-Loss (must do first)

Target:

1. Mailbox runner never silently dies without cleanup.
2. Failure has actionable logs and deterministic fallback.

Changes:

1. Add top-level `try/catch` in `LogicServer::RunPlayerMailbox`.
2. On catch:
   - Log `client_id`, last `msg_id`, event type, trace context.
   - Trigger `CleanupClientSession(client_id, "mailbox_exception")`.
   - Send kick (reuse mailbox overflow kick pattern or dedicated reason).
3. Ensure runner finalization path always normalizes:
   - `executing` state
   - `mailbox_active_runners_`
   - queue metrics publication
4. Add structured log helper for mailbox coroutine failures.

Primary files:

1. `src/server/logic/logic_server.cc`
2. `src/server/logic/logic_server.h` (if helper declaration added)

Acceptance:

1. Injected throw in a handler does not leave mailbox stuck.
2. New messages for same `client_id` still get consumed after recovery or cleanup.
3. Error logs include sufficient context for incident triage.

---

### P1: Lifecycle and Cooperative Exit

Target:

1. Client cleanup and in-flight runner behavior are consistent.

Changes:

1. Add mailbox cancellation state (`cancelled_clients_` or mailbox-local cancel flag).
2. Implement `CancelMailbox(client_id)`:
   - Set cancel flag.
   - Drop queued events and release var payload refs.
3. In `RunPlayerMailbox` loop:
   - Check cancel flag before dequeue and before dispatch.
   - Exit quickly with metrics/log.
4. In `ExecuteQueuedEvent`:
   - Optional fast cancel check at function start.
5. Add local `try/catch` in high-risk handler boundaries (JSON decode/service calls) where specific response is useful.

Primary files:

1. `src/server/logic/logic_server.cc`
2. `src/server/logic/handlers/*` (targeted, not blanket changes)

Acceptance:

1. Disconnect/logout causes mailbox loop to stop within 1 tick budget.
2. No post-logout side effects from stale queued events.

---

### P2: Throughput and Scheduling Optimization

Target:

1. Reduce per-message overhead for large chat fanout.

Changes:

1. Add batch send API in `ResponseSender`, e.g.:
   - `SendMany(const std::vector<uint64_t>& client_ids, uint16_t msg_id, const std::vector<uint8_t>& payload)`
2. Refactor `ChatHandler::SendChatDispatches` to:
   - group identical payload dispatches where possible
   - avoid creating massive coroutine fanout (`WhenAll`) for send loops
3. Add metrics:
   - batch size
   - send loop latency
   - dropped/failed sends

Primary files:

1. `src/server/logic/response_sender.h`
2. `src/server/logic/response_sender.cc`
3. `src/server/logic/handlers/chat/chat_handler.cc`

Acceptance:

1. Lower tick jitter under high-fanout world chat benchmark.
2. No correctness regression in channel-specific delivery.

---

### P3: Executor Capability Enhancements (optional/harder)

Target:

1. Improve cancellation semantics for long-running async branches.

Changes:

1. Add cooperative cancellation wiring to `Async` awaiter state.
2. Add optional fail-fast mode for `WhenAll`:
   - first fatal branch marks shared cancel state
   - unfinished branches skip resume-side work when possible
3. Keep semantics explicit:
   - running blocking I/O may finish in background
   - awaiter resumes early with cancellation/timeout result

Primary files:

1. `src/server/logic/coroutine_executor.h`
2. `src/server/logic/coroutine_executor.cc` (if needed)

Acceptance:

1. Cancellation behavior is deterministic and covered by tests.
2. No underflow/leak in suspended/running/pending callback counters.

---

## 4. Implementation Notes by Risk

### R1 Mailbox runner exception guard

Recommended skeleton:

1. Wrap full loop in `try`.
2. Catch `std::exception` and unknown.
3. Use one cleanup helper to avoid duplicate state mutations.

### R2 Cancellation

Design rule:

1. Use cooperative cancellation only.
2. Never assume hard-stop for already running blocking task.

### R3 Chat broadcast

Design rule:

1. Prefer fewer send API invocations over more coroutine task objects.
2. Avoid `WhenAll` fanout for thousands of tiny sends.

### R4 Logout cleanup

Design rule:

1. Queue cleanup + in-flight cancellation check both required.
2. Cleanup paths must be idempotent.

### R5/R6 Logging and handler protection

Design rule:

1. Log once at top-level with context.
2. Catch locally only when converting to business response adds value.

### R7 Lane scheduler

Current decision:

1. No immediate code change.
2. Add lane occupancy/wait duration observability if needed.

---

## 5. Test Plan

Unit/integration additions:

1. Mailbox exception survival:
   - Inject throw in a test handler.
   - Verify runner cleanup and no permanent starvation.
2. Disconnect cancellation:
   - Enqueue multiple events, trigger cleanup, verify no further handler execution.
3. Chat fanout regression:
   - Validate recipient correctness for world/private/team/area/guild.
   - Measure dispatch cost before/after batch send.
4. Executor cancellation semantics (if P3):
   - timeout/cancel race cases
   - counter consistency (`running/suspended/pending callbacks`)

Suggested test files:

1. `tests/server/logic/player_mailbox_causal_test.cc`
2. `tests/server/logic/logic_server_test.cc`
3. `tests/server/logic/chat_handler_test.cc`
4. `tests/server/logic/coroutine_executor_test.cc`

---

## 6. Rollout Strategy

1. Deliver P0 behind no flag (safety fix).
2. Deliver P1 with conservative defaults.
3. Deliver P2 behind config gate if needed (`chat_batch_send_enabled`).
4. Track production metrics after each phase:
   - mailbox active runners
   - mailbox pending depth
   - coroutine unhandled exceptions
   - tick duration p95/p99

Rollback:

1. P0/P1 rollback by reverting mailbox guard/cancel changes.
2. P2 rollback by disabling batch send path in config.
3. P3 rollout only after dedicated soak and canary.

---

## 7. Final Prioritized Backlog

1. P0: mailbox top-level exception guard + context logs + deterministic cleanup.
2. P1: cancel mailbox semantics + in-flight early exit checks.
3. P2: chat send batching and metrics.
4. P3: executor cooperative cancellation enhancements.

