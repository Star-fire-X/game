# Stage 4 Logic Main Thread Bottleneck Report

Generated: 2026-03-04 22:45:21

Final Verdict: **FALSIFIED**

## Environment
- tick_interval_ms: 50
- warmup/sample/cooldown (s): 0/1/0
- repeats: 1
- swarm_clients: 8
- qps_scale: 0.020
- prometheus_available: true

## Workload Verdicts
| Workload | Ceiling QPS | Verdict |
| --- | ---: | --- |
| W0-Control | 320.00 | INCONCLUSIVE |
| W1-MixedGameplay | 400.00 | FALSIFIED |
| W2-WriteHeavy | 400.00 | FALSIFIED |

### W0-Control
| Step | Target | Offered | Effective | Elasticity | Tick p99 | Source | Overrun | Util | Queue Slope | Overflow/s | HardBP/s | SvcDisc(g/l)/s | Healthy |
| ---: | ---: | ---: | ---: | ---: | --- | ---: | ---: | ---: | ---: | ---: | ---: | --- | --- |
| 0 | 40.00 | 39.00 | 0.00 | 1.000 | 0.000 | unavailable | 0.000 | 0.000 | 0.000 | 0.000 | 0.000 | 0.000/0.000 | PASS |
| 1 | 80.00 | 79.00 | 0.00 | 0.000 | 0.000 | unavailable | 0.000 | 0.000 | 0.000 | 0.000 | 0.000 | 0.000/0.000 | PASS |
| 2 | 120.00 | 119.00 | 0.00 | 0.000 | 0.000 | unavailable | 0.000 | 0.000 | 0.000 | 0.000 | 0.000 | 0.000/0.000 | PASS |
| 3 | 160.00 | 159.00 | 0.00 | 0.000 | 0.000 | unavailable | 0.000 | 0.000 | 0.000 | 0.000 | 0.000 | 0.000/0.000 | PASS |
| 4 | 200.00 | 199.00 | 0.00 | 0.000 | 0.000 | unavailable | 0.000 | 0.000 | 0.000 | 0.000 | 0.000 | 0.000/0.000 | PASS |
| 5 | 240.00 | 239.00 | 0.00 | 0.000 | 0.000 | unavailable | 0.000 | 0.000 | 0.000 | 0.000 | 0.000 | 0.000/0.000 | PASS |
| 6 | 280.00 | 279.00 | 0.00 | 0.000 | 0.000 | unavailable | 0.000 | 0.000 | 0.000 | 0.000 | 0.000 | 0.000/0.000 | PASS |
| 7 | 320.00 | 319.00 | 0.00 | 0.000 | 0.000 | unavailable | 0.000 | 0.000 | 0.000 | 0.000 | 0.000 | 0.000/0.000 | PASS |

Reasons: Control workload ceiling computed.

### W1-MixedGameplay
| Step | Target | Offered | Effective | Elasticity | Tick p99 | Source | Overrun | Util | Queue Slope | Overflow/s | HardBP/s | SvcDisc(g/l)/s | Healthy |
| ---: | ---: | ---: | ---: | ---: | --- | ---: | ---: | ---: | ---: | ---: | ---: | --- | --- |
| 0 | 160.00 | 159.00 | 0.00 | 1.000 | 0.000 | unavailable | 0.000 | 0.000 | 0.000 | 0.000 | 0.000 | 0.000/0.000 | PASS |
| 1 | 208.00 | 207.00 | 190.00 | 3.958 | 0.000 | logic_routed_processed_total | 0.000 | 0.000 | 0.000 | 0.000 | 0.000 | 0.000/0.000 | PASS |
| 2 | 256.00 | 255.00 | 229.00 | 0.812 | 0.000 | logic_routed_processed_total | 0.000 | 0.000 | 0.000 | 0.000 | 0.000 | 0.000/0.000 | PASS |
| 3 | 304.00 | 303.00 | 281.00 | 1.083 | 0.000 | logic_routed_processed_total | 0.000 | 0.000 | 0.000 | 0.000 | 0.000 | 0.000/0.000 | PASS |
| 4 | 352.00 | 351.00 | 303.00 | 0.458 | 0.000 | logic_routed_processed_total | 0.000 | 0.000 | 0.000 | 0.000 | 0.000 | 0.000/0.000 | PASS |
| 5 | 400.00 | 399.00 | 377.00 | 1.542 | 0.000 | logic_routed_processed_total | 0.000 | 0.000 | 0.000 | 0.000 | 0.000 | 0.000/0.000 | PASS |

Reasons: FALSIFIED-1: highest step remains elastic and healthy.

### W2-WriteHeavy
| Step | Target | Offered | Effective | Elasticity | Tick p99 | Source | Overrun | Util | Queue Slope | Overflow/s | HardBP/s | SvcDisc(g/l)/s | Healthy |
| ---: | ---: | ---: | ---: | ---: | --- | ---: | ---: | ---: | ---: | ---: | ---: | --- | --- |
| 0 | 160.00 | 159.00 | 157.00 | 1.000 | 0.000 | logic_routed_processed_total | 0.000 | 0.000 | 0.000 | 0.000 | 0.000 | 0.000/0.000 | PASS |
| 1 | 208.00 | 207.00 | 205.00 | 1.000 | 0.000 | logic_routed_processed_total | 0.000 | 0.000 | 0.000 | 0.000 | 0.000 | 0.000/0.000 | PASS |
| 2 | 256.00 | 255.00 | 252.00 | 0.979 | 0.000 | logic_routed_processed_total | 0.000 | 0.000 | 0.000 | 0.000 | 0.000 | 0.000/0.000 | PASS |
| 3 | 304.00 | 303.00 | 301.00 | 1.021 | 0.000 | logic_routed_processed_total | 0.000 | 0.000 | 0.000 | 0.000 | 0.000 | 0.000/0.000 | PASS |
| 4 | 352.00 | 351.00 | 347.00 | 0.958 | 0.000 | logic_routed_processed_total | 0.000 | 0.000 | 0.000 | 0.000 | 0.000 | 0.000/0.000 | PASS |
| 5 | 400.00 | 399.00 | 392.00 | 0.938 | 0.000 | logic_routed_processed_total | 0.000 | 0.000 | 0.000 | 0.000 | 0.000 | 0.000/0.000 | PASS |

Reasons: FALSIFIED-1: highest step remains elastic and healthy.

## Threshold Hits

## Conclusion
Final classification: **FALSIFIED**.
