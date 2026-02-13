# Stage 3 Performance Report

Generated: 2026-02-13 19:30:19

## PRD Success Metrics
| Metric | Target | Measured | Pass |
| --- | --- | --- | --- |
| 移动同步 RTT (KCP) | p95 < 50ms, p99 < 80ms | p95=11.0884, p99=11.1797 | PASS |
| 战斗动画延迟 | p95 < 80ms | p95=11.1183 | PASS |
| 丢包恢复时间 | p95 < 100ms | p95=40.0642 | PASS |
| 吞吐量 | > 1000 msg/s | 12289.8 msg/s | PASS |
| 并发连接成功率 | > 95% (1000 connections) | 99.3% | PASS |
| 服务端 CPU 使用率 | < 80% | 19.260307% | PASS |
| 服务端内存占用 | < 500 MB | 215.750000 MB | PASS |

## Benchmark Details

### RttBenchmark
| Metric | Value (ms) |
| --- | --- |
| Min | 9.00537 |
| Max | 11.1858 |
| Avg | 10.1215 |
| P50 | 10.0074 |
| P95 | 11.0884 |
| P99 | 11.1797 |

Targets:
- p95 < 50ms
- p99 < 80ms

### CombatLatencyBenchmark
| Metric | Value (ms) |
| --- | --- |
| Min | 8.93714 |
| Max | 13.3026 |
| Avg | 10.1522 |
| P50 | 10.0218 |
| P95 | 11.1183 |
| P99 | 11.2307 |

Targets:
- p95 < 80ms

### PacketLossRecoveryBenchmark
| Metric | Value (ms) |
| --- | --- |
| Min | 9.52886 |
| Max | 40.9834 |
| Avg | 13.8074 |
| P50 | 10.0205 |
| P95 | 40.0642 |
| P99 | 40.98 |

Targets:
- p95 < 100ms

### ThroughputBenchmark
- Throughput: 12289.8 msg/s
- Data Rate: 0.281292 MB/s
- Target: > 1000 msg/s

### ConcurrentConnectionsStressTest
- Clients: 1000
- Handshake Success Rate: 99.3%
- CPU Usage: 19.2603% (target < 80%)
- Memory Usage: 215.75 MB (target < 500 MB)

Raw metrics are available in the CSV report generated alongside this file.
