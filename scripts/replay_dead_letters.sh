#!/usr/bin/env bash
set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

BIN_PATH="${MIR2_DEAD_LETTER_REPLAY_BIN:-${PROJECT_ROOT}/build-wsl/bin/mir2_dead_letter_replay}"
BUILD_PRESET="${MIR2_BUILD_PRESET:-vcpkg-wsl-debug}"
AUTO_BUILD=true

usage() {
  cat <<'USAGE'
Usage: scripts/replay_dead_letters.sh [wrapper options] -- --db-path <path> [replay options]

Wrapper options:
  --bin <path>          Replay binary path (default: build-wsl/bin/mir2_dead_letter_replay)
  --build-preset <name> CMake preset used when auto building binary
  --no-build            Fail if binary is missing
  -h, --help            Show this help

Replay options (forwarded to mir2_dead_letter_replay):
  --db-path <path>      RocksDB path (required)
  --prefix <key_prefix> Only replay keys with this prefix
  --start-ms <ts_ms>    Include rows recorded at or after ts
  --end-ms <ts_ms>      Include rows recorded at or before ts
  --limit <n>           Max dead-letter rows scanned (0 = all)
  --dry-run             Match and print without replay
  --keep-dead-letter    Keep dead-letter rows after replay
  --verbose             Print matched rows
USAGE
}

forward_args=()
while [[ $# -gt 0 ]]; do
  case "$1" in
    --bin)
      BIN_PATH="$2"
      shift 2
      ;;
    --build-preset)
      BUILD_PRESET="$2"
      shift 2
      ;;
    --no-build)
      AUTO_BUILD=false
      shift
      ;;
    --)
      shift
      forward_args+=("$@")
      break
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      forward_args+=("$1")
      shift
      ;;
  esac
done

if [[ ! -x "${BIN_PATH}" ]]; then
  if [[ "${AUTO_BUILD}" != "true" ]]; then
    echo "Replay binary not found: ${BIN_PATH}" >&2
    echo "Build with: cmake --build --preset ${BUILD_PRESET} --target mir2_dead_letter_replay" >&2
    exit 1
  fi

  if ! command -v cmake >/dev/null 2>&1; then
    echo "cmake is required to auto-build mir2_dead_letter_replay" >&2
    exit 1
  fi

  echo "Building mir2_dead_letter_replay with preset ${BUILD_PRESET}..."
  cmake --build --preset "${BUILD_PRESET}" --target mir2_dead_letter_replay -j"$(nproc)"
fi

exec "${BIN_PATH}" "${forward_args[@]}"
