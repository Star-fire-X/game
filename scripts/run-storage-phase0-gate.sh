#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'USAGE'
Usage: scripts/run-storage-phase0-gate.sh [build_dir] [test_bin]

Defaults:
  build_dir: build-wsl
  test_bin:  <build_dir>/bin/storage_engine_focus_tests

Environment:
  LEGEND2_STORAGE_PHASE0_GATE_FILTER  Override gtest filter for the phase0 gate suite.
USAGE
}

if [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
  usage
  exit 0
fi

BUILD_DIR="${1:-build-wsl}"
TEST_BIN="${2:-${BUILD_DIR}/bin/storage_engine_focus_tests}"

DEFAULT_FILTER="StoragePhase0GateTest.*:StorageEngineTest.HealthMetrics:StorageEngineTtlIsolationTest.StartupRecoveryRepairsPersistentTierWithoutScanningTtlTier:StorageEngineDurableOutboxTest.ReplaysPendingWritesAfterRestartWhenBackendRecovers:AsyncPersistenceQueueOutboxMetricTest.RejectedCriticalCounterIncrementsWhenOutboxFull"
GATE_FILTER="${LEGEND2_STORAGE_PHASE0_GATE_FILTER:-${DEFAULT_FILTER}}"

if [[ ! -d "${BUILD_DIR}" ]]; then
  echo "error: build directory not found: ${BUILD_DIR}" >&2
  exit 1
fi

if [[ ! -x "${TEST_BIN}" ]]; then
  echo "error: test binary not found or not executable: ${TEST_BIN}" >&2
  exit 1
fi

echo "[storage-phase0-gate] Running phase0 gate suite"
echo "[storage-phase0-gate]   binary: ${TEST_BIN}"
echo "[storage-phase0-gate]   filter: ${GATE_FILTER}"

"${TEST_BIN}" \
  --gtest_filter="${GATE_FILTER}" \
  --gtest_color=no \
  --gtest_brief=1

echo "[storage-phase0-gate] Gate suite passed."
