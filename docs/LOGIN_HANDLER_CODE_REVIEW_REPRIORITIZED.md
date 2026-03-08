# Login Handler Code Review (Reprioritized)

Date: 2026-02-13  
Scope: `src/server/logic/handlers/login/login_handler.cc`, `src/server/logic/handlers/login/login_handler.h`

## Executive Summary

This document reprioritizes the submitted review findings based on repository evidence.
The two highest-priority issues are:

1. Coroutine lifetime race between async callback and destroyed coroutine handle.
2. Rate-limiter config/comment mismatch causing weaker-than-documented protection.

## Priority Matrix

### P0 (Must Fix Immediately)

1. Coroutine lifetime race may resume destroyed coroutine (Valid)
- Original item: 1.1
- Evidence:
  - Task destruction destroys coroutine handle: `src/server/logic/task.h:206`, `src/server/logic/task.h:208`
  - Await state stores non-owning continuation and resumes later: `src/server/logic/handlers/login/login_handler.cc:52`, `src/server/logic/handlers/login/login_handler.cc:86`
- Risk: Use-after-destroy / undefined behavior.
- Action:
  - Add cancellation/liveness handshake between awaiter and callback (stop-token or explicit shared liveness state that prevents `resume` after coroutine teardown).
  - Add regression test for disconnect/cancel during pending login callback.

2. Rate limiter behavior does not match comment (Valid)
- Original item: 1.2
- Evidence:
  - Comment claims "refills 1 per 12 seconds": `src/server/logic/handlers/login/login_handler.h:66`
  - Config is `refill_rate = 1`: `src/server/logic/handlers/login/login_handler.h:69`
  - Implementation refills per elapsed second: `src/server/security/rate_limiter.cc:42`, `src/server/security/rate_limiter.cc:44`
- Risk: Security expectation gap (actual refill is much faster than documented).
- Action:
  - Either fix config to match intended policy or correct comment.
  - Prefer time-based config semantics to remove ambiguity.

### P1 (Should Fix)

1. Track called before validating account_id causes possible stale tracked client (Valid)
- Original item: 2.1
- Evidence: `client_registry_.Track` happens before `account_id != 0` check:
  - `src/server/logic/handlers/login/login_handler.cc:177`
  - `src/server/logic/handlers/login/login_handler.cc:178`
- Risk: Client may remain tracked even when login success payload is inconsistent.
- Action: Move `Track` into the `account_id != 0` success branch.

2. Exception boundary in handler flow is weak (Valid)
- Original item: 3.4
- Evidence:
  - No local try/catch around login flow in `HandleMessage`/`HandleLogin`: `src/server/logic/handlers/login/login_handler.cc:127`, `src/server/logic/handlers/login/login_handler.cc:160`
- Risk: Unexpected exception can terminate coroutine path without deterministic error response.
- Action: Add top-level try/catch in handler methods, log and send fallback error response.

3. Username logged in debug line (Valid, policy-dependent severity)
- Original item: 5.1
- Evidence: `src/server/logic/handlers/login/login_handler.cc:194`
- Risk: Potential PII in logs.
- Action: Remove or hash username; prefer client/account identifiers.

4. Error-code mapping granularity is coarse (Valid)
- Original item: 3.3
- Evidence:
  - Any non-OK codec status maps to one common error: `src/server/logic/handlers/handler_error_utils.h:14`
- Risk: Reduced diagnosability and weaker protocol error signaling.
- Action: Introduce finer common error codes and mapping strategy.

### P2 (Nice to Have / Optimization)

1. `await_resume` returns by copy (Valid optimization)
- Original item: 3.2
- Evidence: `return *state_->result;` in `src/server/logic/handlers/login/login_handler.cc:103`
- Action: Return moved value to reduce copy cost.

2. Mutex in await state may be heavier than necessary (Valid optimization, not correctness bug)
- Original item: 3.1
- Evidence: `std::mutex` + `optional` in await state:
  - `src/server/logic/handlers/login/login_handler.cc:53`
  - `src/server/logic/handlers/login/login_handler.cc:97`
- Action: Simplify synchronization if one-shot completion guarantees remain explicit.

3. `AwaitLogin` missing `[[nodiscard]]` (Valid style improvement)
- Original item: 6 (`[[nodiscard]]`)
- Evidence: declaration in `src/server/logic/handlers/login/login_handler.h:58`

## Findings Reclassified as Not Valid or Not Proven

1. "Need explicit unbind before BindClientAccount to avoid conflict" (Not proven)
- Original item: 2.2
- Evidence:
  - `BindClientAccount` is overwrite assignment: `src/server/logic/services/session_role_store.cc:14`
- Notes: Could still add explicit unbind for clarity, but current code does not show conflict bug by itself.

2. "Synchronous callback causes await_suspend UB in current code" (Not valid for current implementation)
- Original item: 2.3
- Evidence:
  - Callback resumes via `asio::post` instead of immediate resume: `src/server/logic/handlers/login/login_handler.cc:85`
- Notes: Keeping an explanatory comment is still useful.

3. "Fallback encode may still fail and send empty packet" (Not valid under current codec rules)
- Original item: 2.4
- Evidence:
  - Login response validation only rejects `ERR_OK` with missing fields: `src/common/protocol/message_codec.cpp:76`
  - Fallback response uses non-OK error and empty token/account 0: `src/server/logic/handlers/login/login_handler.cc:40`
  - Encoder returns payload for valid fallback: `src/common/protocol/message_codec.cpp:176`

4. "Missing include for handler_error_utils.h" (Not valid)
- Original item: 6 include note
- Evidence: included at `src/server/logic/handlers/login/login_handler.cc:18`

5. "C++20 designated initializer may fail on C++17 project" (Out of scope / not applicable)
- Original item: 6 C++17 compatibility note
- Evidence: project baseline is C++20.

## Recommended Execution Order

1. Fix P0-1 coroutine lifetime race and add regression test.
2. Fix P0-2 rate-limit semantics/comment mismatch and add unit test for refill behavior.
3. Fix P1-1 `Track` sequencing.
4. Add handler-level exception boundary (P1-2).
5. Apply logging/privacy and error-mapping improvements (P1-3/4).
6. Apply P2 cleanup optimizations.

