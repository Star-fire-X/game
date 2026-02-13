#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'USAGE'
Usage: scripts/run-coroutine-sanitizer-gate.sh [build_dir] [test_bin] [repeat]

Defaults:
  build_dir: build-wsl
  test_bin:  <build_dir>/bin/legend2_tests
  repeat:    20

Environment:
  LEGEND2_SANITIZER_FILTER  Override gtest filter.
  LEGEND2_SANITIZER_REPEAT  Override repeat count.
USAGE
}

if [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
  usage
  exit 0
fi

BUILD_DIR="${1:-build-wsl}"
TEST_BIN="${2:-${BUILD_DIR}/bin/legend2_tests}"
DEFAULT_FILTER="CoroutineExecutorTest.RepeatedStartStopDrainWithInFlightTasks:CoroutineExecutorTest.QueuedCallbackAfterExecutorDestructionDoesNotTouchDanglingState:CoroutineExecutorTest.TimeoutDoesNotCancelBackgroundWorkCompletion:CoroutineExecutorTest.OverLimitRejectionsDoNotCorruptRunningCount:CoroutineExecutorCounterRaceTest.*:PlayerMailboxCausalTest.MailboxSpawnRejectCleansStateAndLaterEventsStillRun:PlayerMailboxCausalTest.GlobalMailboxHardLimitDropsEventAndSignalsBackpressure:PlayerMailboxCausalTest.PrewarmSpawnRejectStillSendsLogicReady"
TEST_FILTER="${LEGEND2_SANITIZER_FILTER:-${DEFAULT_FILTER}}"
REPEAT_RAW="${3:-${LEGEND2_SANITIZER_REPEAT:-20}}"

if [[ ! -d "${BUILD_DIR}" ]]; then
  echo "error: build directory not found: ${BUILD_DIR}" >&2
  exit 1
fi

if [[ ! -x "${TEST_BIN}" ]]; then
  echo "error: test binary not found or not executable: ${TEST_BIN}" >&2
  exit 1
fi

if ! [[ "${REPEAT_RAW}" =~ ^[0-9]+$ ]]; then
  echo "error: repeat must be a non-negative integer, got: ${REPEAT_RAW}" >&2
  exit 1
fi

REPEAT="${REPEAT_RAW}"
if [[ "${REPEAT}" -eq 0 ]]; then
  REPEAT=1
fi

echo "[sanitizer-gate] Running sanitizer soak"
echo "[sanitizer-gate]   binary: ${TEST_BIN}"
echo "[sanitizer-gate]   filter: ${TEST_FILTER}"
echo "[sanitizer-gate]   repeat: ${REPEAT}"

"${TEST_BIN}" \
  --gtest_filter="${TEST_FILTER}" \
  --gtest_repeat="${REPEAT}" \
  --gtest_break_on_failure \
  --gtest_color=no \
  --gtest_brief=1

echo "[sanitizer-gate] Sanitizer soak passed."
