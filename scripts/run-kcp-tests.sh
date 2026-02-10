#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'USAGE'
Usage: scripts/run-kcp-tests.sh [build_dir] [test_bin] [gtest_filter]

Defaults:
  build_dir: build-wsl
  test_bin:  <build_dir>/bin/legend2_tests
  gtest_filter:
    KcpSessionStaticTest.*:KcpSessionTest.*:KcpServerTest.*:KcpUpgradeHandlerTest.*:
    DualChannelManagerTest.*:KcpHandshakeIntegrationTest.*:
    KcpHandshakeFailureIntegrationTest.*:KcpRoutingIntegrationTest.*:
    KcpFallbackIntegrationTest.*:KcpFloodProtectionTest.*:
    KcpHeartbeatTimeoutTest.*:KcpPerformanceTest.*:KcpConcurrencyPerformanceTest.*:
    KcpLossyPerformanceTest.*

Environment:
  LEGEND2_KCP_FILTER   Override gtest filter.
USAGE
}

if [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
  usage
  exit 0
fi

BUILD_DIR="${1:-build-wsl}"
TEST_BIN="${2:-${BUILD_DIR}/bin/legend2_tests}"
DEFAULT_FILTER="KcpSessionStaticTest.*:KcpSessionTest.*:KcpServerTest.*:KcpUpgradeHandlerTest.*:DualChannelManagerTest.*:KcpHandshakeIntegrationTest.*:KcpHandshakeFailureIntegrationTest.*:KcpRoutingIntegrationTest.*:KcpFallbackIntegrationTest.*:KcpFloodProtectionTest.*:KcpHeartbeatTimeoutTest.*:KcpPerformanceTest.*:KcpConcurrencyPerformanceTest.*:KcpLossyPerformanceTest.*"
TEST_FILTER="${3:-${LEGEND2_KCP_FILTER:-${DEFAULT_FILTER}}}"

if [[ ! -d "${BUILD_DIR}" ]]; then
  echo "error: build directory not found: ${BUILD_DIR}" >&2
  exit 1
fi

if [[ ! -x "${TEST_BIN}" ]]; then
  echo "error: test binary not found or not executable: ${TEST_BIN}" >&2
  exit 1
fi

echo "Running KCP regression suite..."
echo "  binary: ${TEST_BIN}"
echo "  filter: ${TEST_FILTER}"
"${TEST_BIN}" --gtest_filter="${TEST_FILTER}"
