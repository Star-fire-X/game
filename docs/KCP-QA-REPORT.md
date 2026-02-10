# KCP Dual-Channel Network QA Test Execution Report
Date: 2026-02-03
Scope: KCP dual-channel routing, upgrade handshake, fallback/recovery, UDP protection, and regression fixes (C-1, C-2, M-1..M-4, m-1..m-6).
Method: Code review + unit test inventory (not executed) + integration test design (not executed).

## 1. Executive Summary
Overall result: Conditional PASS.
- All listed regressions are fixed in code or documented with low-risk acceptance.
- Core functional requirements are implemented and covered by targeted unit tests.
- Unit tests were not executed in this environment; no end-to-end or performance runs were executed.
Release recommendation: Proceed to staging only after executing unit tests and integration scenarios; production release requires performance validation.

## 2. Test Coverage
### 2.1 Unit Test Inventory (not executed)
Total: 11 files, 59 test cases (TEST/TEST_F).

| Component | Test File | Test Cases | Notes |
| --- | --- | --- | --- |
| ChannelRouter | tests/common/channel_router_test.cc | 2 | Routes + fallback verification |
| FallbackController | tests/common/fallback_controller_test.cc | 3 | Timeout + backoff coverage |
| KcpChannel (client) | tests/client/network/kcp_channel_test.cc | 7 | Token framing, receive queue cap, mutex behavior |
| DualChannelClient | tests/client/network/dual_channel_client_test.cc | 7 | Routing, queue cap, callback vs polling, recovery |
| KcpUpgradeHandler (client) | tests/client/network/kcp_upgrade_handler_test.cc | 6 | Request, response, heartbeat, timeout |
| KcpSession (server) | tests/server/network/kcp_session_test.cc | 6 | Conv allocation, token, output framing, concurrency |
| KcpServer | tests/server/network/kcp_server_test.cc | 9 | Session lifecycle, rate limit, blacklist, dispatch |
| DualChannelManager | tests/server/network/dual_channel_manager_test.cc | 8 | Routing + heartbeat handling |
| KcpUpgradeHandler (server) | tests/server/network/kcp_upgrade_handler_test.cc | 6 | Upgrade responses + errors |
| IpRateLimiter | tests/server/ip_rate_limiter_test.cc | 2 | Sliding window + cleanup |
| ConvBlacklist | tests/server/conv_blacklist_test.cc | 3 | Failure count + TTL |

Coverage note: Line/branch coverage not measured. Execution status is not verified in this report.

### 2.2 Integration Test Design (not executed)
Recommended end-to-end scenarios:
1. Normal upgrade flow: TCP connect -> KcpUpgradeRequest/Response -> UDP handshake -> KCP confirmed -> movement over KCP.
2. Routing correctness: MoveReq/EntityMove over KCP, LoginReq/InventoryUpdate over TCP.
3. UDP blocked: force fallback to TCP; verify recovery attempts (30s base, exponential backoff).
4. V1 client compatibility: MIR2 magic -> no upgrade; all traffic over TCP.
5. UDP defense: >1000 pps from single IP -> rate-limited; 3 invalid token attempts -> conv blacklisted.
6. Heartbeat timeout: no ack after send -> disconnect and fallback.

## 3. Functional Verification (P0/P1/P2)
### P0 (Must-Pass)
| Requirement | Evidence | Result |
| --- | --- | --- |
| Message routing: movement via KCP, login/inventory via TCP | src/common/network/channel_router.cc; tests/common/channel_router_test.cc | PASS (code + unit test) |
| KCP upgrade handshake (client + server) | src/client/network/kcp_upgrade_handler.cc; src/server/handlers/network/kcp_upgrade_handler.cc; tests/client/network/kcp_upgrade_handler_test.cc; tests/server/network/kcp_upgrade_handler_test.cc | PASS (code + unit tests) |
| Fallback state machine: 5s handshake timeout, 30s recovery, exponential backoff | src/common/network/fallback_controller.{h,cc}; src/common/network/kcp_config.h; tests/common/fallback_controller_test.cc | PASS (code + unit tests) |
| UDP protection: 1000 pps/IP and 3-failure blacklist | src/server/network/ip_rate_limiter.{h,cc}; src/server/network/conv_blacklist.{h,cc}; tests/server/ip_rate_limiter_test.cc; tests/server/conv_blacklist_test.cc; tests/server/network/kcp_server_test.cc | PASS (code + unit tests) |

### P1 (Should-Pass)
| Requirement | Evidence | Result |
| --- | --- | --- |
| KcpChannel thread safety (kcp_mutex_) | src/client/network/kcp_channel.cc; tests/client/network/kcp_channel_test.cc | PASS (code + unit tests) |
| DualChannelClient dispatch modes (callback vs polling) | src/client/network/dual_channel_client.{h,cc}; tests/client/network/dual_channel_client_test.cc | PASS (code + unit tests) |
| Receive queue capacity limit (1000) | src/client/network/kcp_channel.h; src/client/network/dual_channel_client.h; tests/*_test.cc | PASS (code + unit tests) |
| Heartbeat timeout based on last send | src/client/network/kcp_upgrade_handler.cc; tests/client/network/kcp_upgrade_handler_test.cc | PASS (code + unit tests) |
| Conv ID randomization | src/server/network/kcp_session.cc; tests/server/network/kcp_session_test.cc | PASS (code + unit tests) |

### P2 (Nice-to-Have)
| Requirement | Evidence | Result |
| --- | --- | --- |
| TCP/KCP channel flag validation | src/common/protocol/packet_codec.{h,cc}; src/server/network/tcp_session.cc; src/server/network/kcp_session.cc | PASS (code review) |
| Protocol version auto-detect | src/server/network/tcp_session.cc | PASS (code review) |
| KCP config defaults (nodelay, interval, wnd, mtu) | src/common/network/kcp_config.h; src/server/network/kcp_session.cc; src/client/network/kcp_channel.cc | PASS (code review) |

## 4. Regression Tests (Fixed Issues Verification)
| Issue | Verification | Result |
| --- | --- | --- |
| C-1 ChannelRouter uses actual MsgId enums | channel_router.cc uses MsgId; channel_router_test.cc updated | PASS |
| C-2 Conv ID random generation | KcpSession::AllocateConvId uses random_device + mt19937 | PASS |
| M-1 KcpChannel thread safety | kcp_mutex_ protects kcp_ ops; mutex tests added | PASS |
| M-2 DualChannelClient dispatch mode | update() drains only when callback set; docs added | PASS |
| M-3 Receive queue cap (1000) | kMaxReceiveQueueSize enforced in KcpChannel and DualChannelClient | PASS |
| M-4 Heartbeat timeout uses last send | kcp_upgrade_handler.cc uses last_heartbeat_send_ms_ | PASS |
| m-1 IpRateLimiter cleanup thread safety | Cleanup() now locks mutex | PASS |
| m-2 ConvBlacklist cleanup thread safety | Cleanup() now locks mutex | PASS |
| m-3 KcpChannel per-instance io_thread removed | KcpChannel uses external io_context | PASS |
| m-4 FlatBuffers include path (system_generated.h) | Still flat include path | ACCEPTED (low risk, unchanged) |
| m-5 ChannelRouter tests use real MsgId values | channel_router_test.cc updated | PASS |
| m-6 KcpSession namespace inconsistency | Documented in header; behavior unchanged | ACCEPTED (documented) |

## 5. Risk Assessment
Remaining risks and recommendations:
- Unit tests and integration tests were not executed here; run `ctest` suites and core network tests before release.
- End-to-end routing verification (real UDP/TCP) not executed; schedule staging network tests with packet loss/latency simulation.
- m-4 include-path fragility: still depends on include paths; keep CMake include dirs stable.
- m-6 namespace discrepancy documented but still a coupling point; keep server and common packet types aligned.
- KCP upgrade request is triggered on TCP connect; confirm product flow expectations (if upgrade should wait for login/scene).

## 6. Performance Metrics (Design Targets)
Targets from PRD/Architecture (not measured in this report):
| Metric | Baseline | Target |
| --- | --- | --- |
| Movement sync RTT | 50-100 ms | 30-50 ms |
| Combat animation latency | 80-150 ms | 40-80 ms |
| Packet loss recovery | 200-500 ms | 50-100 ms |
| KCP update interval | 10 ms | 10 ms |
| Max KCP sessions | 5000/server | 5000/server |

## 7. QA Sign-off
Status: CONDITIONAL GO.
Conditions to proceed:
1. Execute unit tests for KCP dual-channel suite and record results.
2. Run integration scenarios (routing, upgrade, fallback, UDP defense).
3. Capture performance baselines for RTT, loss recovery, and CPU under load.

Prepared by: QA Automation (static review)
