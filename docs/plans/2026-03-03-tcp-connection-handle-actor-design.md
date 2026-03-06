# TCP ConnectionHandle + Actor Design

**Date:** 2026-03-03  
**Status:** Approved (source: user-provided locked decisions in current session)

## Goal

Refactor TCP connection and session layers from multi-entry mutable connection state into a single-writer actor model to eliminate write-path race/crash domains (notably RC=139), unify close semantics, and enforce write-path observability.

## Scope

- In scope:
  - `TcpConnection`, `TcpClient`, `TcpSession` connection lifecycle and write path
  - New `ConnectionHandle` + internal `ConnectionActor`
  - Close/send result types and outbound frame abstraction
  - Prometheus write-path metrics and compatibility mapping
- Out of scope:
  - KCP main flow semantics
  - Logic server business handling semantics
  - Stage4 verdict rule changes
  - Protocol/schema changes

## Locked Decisions

1. Refactor range is connection layer + session layer.
2. API upgrade is allowed with compatibility window.
3. Delivery is phased.
4. Dual-track migration for one version.
5. Write-path observability is mandatory deliverable.

## Target Model

### Public/Semi-public API

- `ConnectionHandle`: thread-safe command entry and sole outward ownership.
- `ConnectionActor`: internal state machine owner running only on io/strand.
- `ConnectionCallbacks`: constructor-time callback bundle; no runtime callback replacement.
- `CloseReason`: close cause taxonomy.
- `SendResult`: send acceptance/rejection taxonomy.
- `OutboundFrame`: encoded payload + trace metadata.
- `LegacyTcpConnectionAdapter`: one-version compatibility wrapper for current `TcpConnection` API.

### Core Runtime Rules

1. All outbound writes enter via `ConnectionHandle::TrySend(...)`.
2. Actor owns queue and write state; remove recursive `DoWrite` and `queued_writes_` ownership split.
3. Write loop uses single-inflight `PumpWrite()` model.
4. Close transitions are strict `kOpen -> kClosing -> kClosed`.
5. `OnClosed` callback is exactly-once.
6. Close request is unified command: `RequestClose(reason, flush_policy)`.
7. `TcpSession` kick/close becomes explicit flush-required close behavior.
8. `TcpClient` lifecycle moves to handle+epoch model to close generation race window.
9. Callback binding is constructor-injected only.

## Observability Contract

Mandatory metrics:

- `network_tcp_outbound_queue_depth` (gauge)
- `network_tcp_outbound_inflight` (gauge)
- `network_tcp_send_rejected_total` (counter, `reason`)
- `network_tcp_close_total` (counter, `reason`)
- `network_tcp_write_handler_lag_ms` (histogram)
- `network_tcp_actor_command_backlog` (gauge)

Compatibility for one version:

- Keep `network.tcp.write_queue_full_total` behavior mapped from new reject reason.

## Phased Execution

- Phase A: foundation (`ConnectionHandle`/`Actor`/types + adapter + metrics), no business semantic changes.
- Phase B: migrate `TcpSession`, `TcpClient`, `NetworkManager`, `GatewayServer` callback binding model.
- Phase C: remove deprecated runtime callback APIs and legacy adapter, update docs/constraints.

## Validation Gates

Unit:

- Concurrent `Send + Close`: no crash/UAF, `OnClosed` once.
- Queue-full policy correctness.
- Write success/failure path and counter/state consistency.
- Flush-close vs drop-pending behavior.
- TcpClient reconnect epoch isolation.

Integration:

- `GatewayLogicPressureTest.FinalClassification` half-run stable.
- Half-run x3: RC=0, no RC=139.
- Full baseline x1 with report artifacts.
- `effective_qps_source` must be present.

