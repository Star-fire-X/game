#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SERVER_CMAKE="${ROOT_DIR}/src/server/CMakeLists.txt"
BUILD_DIR="${1:-${ROOT_DIR}/build-wsl}"

if [[ ! -f "${SERVER_CMAKE}" ]]; then
  echo "error: missing file: ${SERVER_CMAKE}" >&2
  exit 1
fi

status=0

# 1) Static audit: default mir2_server_lib source list must not include legacy files.
while IFS= read -r hit; do
  if [[ -n "${hit}" ]]; then
    echo "error: legacy source detected in MIR2_SERVER_SOURCES: ${hit}" >&2
    status=1
  fi
done < <(
  awk '
    /set\(MIR2_SERVER_SOURCES/ {in_set=1; next}
    in_set && /^[[:space:]]*\)/ {in_set=0}
    in_set {print}
  ' "${SERVER_CMAKE}" | grep -E 'legacy/' || true
)

# 2) Optional runtime audit: inspect built archives when available.
if command -v ar >/dev/null 2>&1; then
  archives=(
    "${BUILD_DIR}/lib/libmir2_server_lib.a"
    "${BUILD_DIR}/lib/libmir2_storage_engine.a"
    "${BUILD_DIR}/lib/liblegend2_common.a"
  )

  for archive in "${archives[@]}"; do
    if [[ -f "${archive}" ]]; then
      hits="$(ar -t "${archive}" | rg -n 'legacy' || true)"
      if [[ -n "${hits}" ]]; then
        echo "error: legacy object found in archive ${archive}:" >&2
        echo "${hits}" >&2
        status=1
      fi
    fi
  done
fi

if [[ "${status}" -ne 0 ]]; then
  exit "${status}"
fi

if [[ -d "${BUILD_DIR}" ]]; then
  echo "ok: link target audit passed (source + available archives)"
else
  echo "ok: link target audit passed (source-only check)"
fi
