#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'USAGE'
Usage: scripts/run-coroutine-release-gate.sh [build_dir] [test_bin]

Defaults:
  build_dir: build-wsl
  test_bin:  <build_dir>/bin/legend2_tests

Environment:
  LEGEND2_COROUTINE_GATE_P0_FILTER  Override P0 gtest filter.
  LEGEND2_COROUTINE_GATE_P1_FILTER  Override P1 gtest filter.
USAGE
}

if [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
  usage
  exit 0
fi

BUILD_DIR="${1:-build-wsl}"
TEST_BIN="${2:-${BUILD_DIR}/bin/legend2_tests}"

P0_DEFAULT_FILTER="CoroutineExecutorTest.StopAcceptingRejectsSpawnAndDrainCompletes:CoroutineExecutorTest.SpawnOrDropReportsNotAcceptingReason:CoroutineExecutorTest.TrySpawnReturnsOverLimitWhenMaxRunningReached:CoroutineExecutorTest.RepeatedStartStopDrainWithInFlightTasks:CoroutineExecutorTest.OverLimitRejectionsDoNotCorruptRunningCount:CoroutineExecutorCounterRaceTest.*:StorageLoginServiceTest.LoginReturnsOverloadedWhenExecutorIsAtLimit:StorageLoginServiceTest.LoginReturnsMaintenanceWhenExecutorStopsAccepting:PlayerMailboxCausalTest.MailboxSpawnRejectCleansStateAndLaterEventsStillRun:PlayerMailboxCausalTest.GlobalMailboxHardLimitDropsEventAndSignalsBackpressure:PlayerMailboxCausalTest.PrewarmSpawnRejectStillSendsLogicReady"
P1_DEFAULT_FILTER="CoroutineExecutorTest.AsyncWithTimeoutStopTokenCancelsAwaiterEarly:CoroutineExecutorTest.SleepForStopTokenCancelsAwaiterEarly:CoroutineExecutorTest.PreCancelledAsyncWithTimeoutSkipsBlockingWorkDispatch:CoroutineExecutorTest.QueuedCallbackAfterExecutorDestructionDoesNotTouchDanglingState:CoroutineExecutorTest.TimeoutDoesNotCancelBackgroundWorkCompletion:CoroutineExecutorTest.AwaitedUnhandledExceptionInvokesReporterOnly:CoroutineExecutorTest.DetachedUnhandledExceptionInvokesTerminalReporter:CoroutineExecutorTest.WhenAllWaitsForAllTasks:CoroutineExecutorTest.WhenAnyReturnsFirstCompletedTaskIndex:CoroutineExecutorTest.WhenAllSpawnRejectUsesCallbackWithoutThrowing:CoroutineExecutorTest.SnapshotTracksCoroutineMetadataAndLifecycle:CoroutineExecutorTest.DetectHungCoroutinesReportsOnlyOncePerSuspensionWindow:CoroutineExecutorTest.TraceExportWritesCoroutineEventsToJson"

P0_FILTER="${LEGEND2_COROUTINE_GATE_P0_FILTER:-${P0_DEFAULT_FILTER}}"
P1_FILTER="${LEGEND2_COROUTINE_GATE_P1_FILTER:-${P1_DEFAULT_FILTER}}"

if [[ ! -d "${BUILD_DIR}" ]]; then
  echo "error: build directory not found: ${BUILD_DIR}" >&2
  exit 1
fi

if [[ ! -x "${TEST_BIN}" ]]; then
  echo "error: test binary not found or not executable: ${TEST_BIN}" >&2
  exit 1
fi

run_suite() {
  local suite_name="$1"
  local filter="$2"
  echo "[release-gate] Running ${suite_name} suite"
  echo "[release-gate]   binary: ${TEST_BIN}"
  echo "[release-gate]   filter: ${filter}"
  "${TEST_BIN}" \
    --gtest_filter="${filter}" \
    --gtest_color=no \
    --gtest_brief=1
}

run_suite "P0" "${P0_FILTER}"
run_suite "P1" "${P1_FILTER}"

echo "[release-gate] P0/P1 suites passed."
