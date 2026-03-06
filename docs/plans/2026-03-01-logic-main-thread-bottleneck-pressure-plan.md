# Logic Main Thread Bottleneck Pressure Verdict Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Implement a benchmark-only integration pressure suite that outputs `PROVED/FALSIFIED/INCONCLUSIVE` for logic main-thread hard bottleneck using control-vs-business workloads and fixed thresholds.

**Architecture:** Extend `tests/integration/gateway_logic_test.cc` with a dedicated pressure fixture and reusable pressure-engine helpers (workload generator, Prometheus sampler, threshold engine, report writer). Reuse existing gateway+logic integration boot path and session bind helpers; add pressure-only login branch for deterministic authenticated role binding at scale. Keep production APIs unchanged.

**Tech Stack:** C++20, GoogleTest, Asio HTTP client, existing message codec helpers, existing benchmark-only convention (`LEGEND2_BENCHMARK_ONLY`).

---

### Task 1: Add pressure test skeleton (RED)

**Files:**
- Modify: `tests/integration/gateway_logic_test.cc`

**Step 1: Write failing pressure test skeleton**
Add new fixture/class `GatewayLogicPressureTest` and tests:
- `ControlCeilingHeartbeat`
- `MixedGameplayVerdict`
- `FinalClassification`

All tests must guard with benchmark-only skip.

**Step 2: Run targeted tests to verify RED**
Run: `LEGEND2_BENCHMARK_ONLY=1 ./build-wsl/bin/legend2_tests --gtest_filter='GatewayLogicPressureTest.*'`
Expected: build or runtime failure due missing pressure engine helpers.

### Task 2: Implement pressure engine (GREEN)

**Files:**
- Modify: `tests/integration/gateway_logic_test.cc`

**Step 1: Add core pressure data types**
Implement test-local structs:
- `PressureStepResult`
- `WorkloadVerdict`
- `PressureMetricSnapshot`
- `WorkloadDefinition`

**Step 2: Add client swarm + deterministic authenticated binding**
- Add batch connect/login helpers.
- Add deterministic username->character mapping.
- Reuse `BindAuthenticatedSession` and `UpsertOnlineCharacter` through logic login hook branch.

**Step 3: Add token-bucket workload sender**
- Support W0/W1/W2 message ratio.
- Track per-second offered count.

**Step 4: Add Prometheus pull + parser**
- Poll `127.0.0.1:9091/metrics` once per second.
- Parse required underscore metric names.

**Step 5: Add threshold and verdict engine**
Implement:
- `ComputeHealth`
- `ComputeProved`
- `ComputeFalsified`
- final tri-state selector

**Step 6: Add report writer**
Output:
- `docs/STAGE4-LOGIC-BOTTLENECK-REPORT.csv`
- `docs/STAGE4-LOGIC-BOTTLENECK-REPORT.md`

### Task 3: Update overload tuning doc

**Files:**
- Modify: `docs/logic_overload_tuning_guide.md`

**Step 1: Add “硬瓶颈判定” section**
Document:
- workload definitions
- key formulas
- health thresholds
- PROVED/FALSIFIED/INCONCLUSIVE semantics
- release-gate requirement

### Task 4: Verification and execution guidance

**Files:**
- Modify: `tests/integration/gateway_logic_test.cc` (if fixes are needed)

**Step 1: Build tests**
Run: `cmake --build --preset vcpkg-wsl-debug --target legend2_tests -j8`
Expected: build success.

**Step 2: Benchmark-only smoke run**
Run:
`LEGEND2_BENCHMARK_ONLY=1 ./build-wsl/bin/legend2_tests --gtest_filter='GatewayLogicPressureTest.*'`
Expected: report generated with a final tri-state verdict.

**Step 3: Non-benchmark run behavior**
Run:
`./build-wsl/bin/legend2_tests --gtest_filter='GatewayLogicPressureTest.*'`
Expected: tests skip with benchmark-only hint.
