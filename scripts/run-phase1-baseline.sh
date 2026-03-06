#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-build-wsl}"
TEST_BIN="${TEST_BIN:-${ROOT_DIR}/${BUILD_DIR}/bin/legend2_tests}"
OUT_DIR="${1:-${ROOT_DIR}/docs/perf/baseline/$(date +%F)}"
RUN_STAGE4_PRESSURE="${RUN_STAGE4_PRESSURE:-0}"

mkdir -p "${OUT_DIR}"

log() {
  printf '[phase1-baseline] %s\n' "$*"
}

run_and_capture() {
  local name="$1"
  shift
  log "run: ${name}"
  "$@" 2>&1 | tee "${OUT_DIR}/${name}.log"
}

run_gtest_gate() {
  local name="$1"
  local filter="$2"

  run_and_capture "${name}" \
    "${TEST_BIN}" --gtest_filter="${filter}"

  if grep -Eq "No tests matched|0 tests from 0 test suites ran" \
      "${OUT_DIR}/${name}.log"; then
    log "gate '${name}' failed: gtest filter matched no tests"
    return 1
  fi
}

run_and_capture build_tests \
  cmake --build --preset vcpkg-wsl-debug --target legend2_tests -j"$(nproc)"

run_gtest_gate unit_gate \
  'DualChannelManagerTest.*:TcpConnectionTest.WriteBatchCoalescesQueuedPacketsWhenEnabled:PlayerMailboxCausalTest.LegacyDispatch*:PlayerMailboxCausalTest.DispatchHotEventsBatchReusesScratchSetsAcrossCalls:GatewayLogicIntegrationTest.PerformanceTargets'

if [[ "${RUN_STAGE4_PRESSURE}" == "1" ]]; then
  run_gtest_gate pressure_gate \
    'GatewayLogicPressureTest.*'
else
  log "skip: pressure_gate (RUN_STAGE4_PRESSURE=${RUN_STAGE4_PRESSURE})"
fi

if [[ -x "${ROOT_DIR}/${BUILD_DIR}/bin/handler_registry_dispatch_benchmark" ]]; then
  run_and_capture handler_registry_bench \
    "${ROOT_DIR}/${BUILD_DIR}/bin/handler_registry_dispatch_benchmark" \
      --benchmark_out="${OUT_DIR}/handler_registry_dispatch_benchmark.json" \
      --benchmark_out_format=json
fi

if [[ -x "${ROOT_DIR}/${BUILD_DIR}/bin/coroutine_executor_stress_benchmark" ]]; then
  run_and_capture coroutine_executor_bench \
    "${ROOT_DIR}/${BUILD_DIR}/bin/coroutine_executor_stress_benchmark" \
      --benchmark_out="${OUT_DIR}/coroutine_executor_stress_benchmark.json" \
      --benchmark_out_format=json
fi

if compgen -G "${OUT_DIR}/*.json" >/dev/null; then
  run_and_capture benchmark_gate \
    python3 "${ROOT_DIR}/scripts/check_benchmark.py" "${OUT_DIR}"/*.json
fi

log "done. artifacts: ${OUT_DIR}"
