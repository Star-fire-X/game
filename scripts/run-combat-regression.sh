#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${1:-build-wsl}"
LEGEND2_FILTER="${2:-CombatSystemTest.*}"

if [[ "${BUILD_DIR}" != /* ]]; then
  BUILD_DIR="${ROOT_DIR}/${BUILD_DIR}"
fi

COMBAT_CORE_BIN="${BUILD_DIR}/bin/combat_core_test"
LEGEND2_TESTS_BIN="${BUILD_DIR}/bin/legend2_tests"

if [[ ! -x "${COMBAT_CORE_BIN}" ]]; then
  echo "error: combat_core_test not found or not executable: ${COMBAT_CORE_BIN}" >&2
  echo "hint: cmake --build --preset vcpkg-wsl-debug --target combat_core_test" >&2
  exit 1
fi

if [[ ! -x "${LEGEND2_TESTS_BIN}" ]]; then
  echo "error: legend2_tests not found or not executable: ${LEGEND2_TESTS_BIN}" >&2
  echo "hint: cmake --build --preset vcpkg-wsl-debug --target legend2_tests" >&2
  exit 1
fi

echo "[combat-regression] running: ${COMBAT_CORE_BIN}"
"${COMBAT_CORE_BIN}"

echo "[combat-regression] running: ${LEGEND2_TESTS_BIN} --gtest_filter=${LEGEND2_FILTER}"
"${LEGEND2_TESTS_BIN}" --gtest_filter="${LEGEND2_FILTER}"

echo "[combat-regression] all checks passed"
