# Stage 3 Performance Report

Generated: 2026-02-13 20:48:43

## PRD Success Metrics
| Metric | Target | Measured | Pass |
| --- | --- | --- | --- |
| 移动同步 RTT (KCP) | p95 < 50ms, p99 < 80ms | p95=11.0945, p99=11.1686 | PASS |
| 战斗动画延迟 | p95 < 80ms | p95=20.5534 | PASS |
| 丢包恢复时间 | p95 < 100ms | p95=40.8649 | PASS |
| 吞吐量 | > 1000 msg/s | 12284.5 msg/s | PASS |
| 并发连接成功率 | > 95% (1000 connections) | 99% | PASS |
| 服务端 CPU 使用率 | < 80% | 19.432080% | PASS |
| 服务端内存占用 | < 500 MB | 215.703125 MB | PASS |

## Benchmark Details

### RttBenchmark
| Metric | Value (ms) |
| --- | --- |
| Min | 9.88604 |
| Max | 11.1861 |
| Avg | 10.1428 |
| P50 | 10.0166 |
| P95 | 11.0945 |
| P99 | 11.1686 |

Targets:
- p95 < 50ms
- p99 < 80ms

### CombatLatencyBenchmark
| Metric | Value (ms) |
| --- | --- |
| Min | 9.88483 |
| Max | 21.129 |
| Avg | 16.9553 |
| P50 | 19.8985 |
| P95 | 20.5534 |
| P99 | 20.9985 |

Targets:
- p95 < 80ms

### PacketLossRecoveryBenchmark
| Metric | Value (ms) |
| --- | --- |
| Min | 8.98757 |
| Max | 41.2175 |
| Avg | 14.2247 |
| P50 | 10.0566 |
| P95 | 40.8649 |
| P99 | 41.0279 |

Targets:
- p95 < 100ms

### ThroughputBenchmark
- Throughput: 12284.5 msg/s
- Data Rate: 0.28117 MB/s
- Target: > 1000 msg/s

### ConcurrentConnectionsStressTest
- Clients: 1000
- Handshake Success Rate: 99%
- CPU Usage: 19.4321% (target < 80%)
- Memory Usage: 215.703 MB (target < 500 MB)

Raw metrics are available in the CSV report generated alongside this file.
