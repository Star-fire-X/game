#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ROOT_CMAKE="${ROOT_DIR}/CMakeLists.txt"
SERVER_CMAKE="${ROOT_DIR}/src/server/CMakeLists.txt"

if [[ ! -f "${ROOT_CMAKE}" || ! -f "${SERVER_CMAKE}" ]]; then
  echo "error: expected CMake files were not found" >&2
  exit 1
fi

status=0

legacy_refs="$(rg -n 'legacy/' "${SERVER_CMAKE}" || true)"
if [[ -z "${legacy_refs}" ]]; then
  echo "ok: no legacy sources referenced by src/server/CMakeLists.txt"
  exit 0
fi

# Compatibility mode for partially migrated branches:
# if legacy references still exist, they must be gated by LEGEND2_ENABLE_LEGACY
# and must not leak into default MIR2_SERVER_SOURCES.
if ! grep -qE '^[[:space:]]*option\(LEGEND2_ENABLE_LEGACY[[:space:]]' "${ROOT_CMAKE}"; then
  echo "error: LEGEND2_ENABLE_LEGACY option is missing in CMakeLists.txt" >&2
  status=1
fi
if ! grep -qE '^[[:space:]]*if\(LEGEND2_ENABLE_LEGACY\)' "${SERVER_CMAKE}"; then
  echo "error: legacy references exist but LEGEND2_ENABLE_LEGACY block is missing" >&2
  status=1
fi

while IFS= read -r hit; do
  if [[ -n "${hit}" ]]; then
    echo "error: legacy source appears in unconditional MIR2_SERVER_SOURCES block: ${hit}" >&2
    status=1
  fi
done < <(
  awk '
    /set\(MIR2_SERVER_SOURCES/ {in_set=1; next}
    in_set && /^[[:space:]]*\)/ {in_set=0}
    in_set {print}
  ' "${SERVER_CMAKE}" | grep -E 'legacy/' || true
)

while IFS= read -r hit; do
  if [[ -n "${hit}" ]]; then
    echo "error: legacy reference is outside LEGEND2_ENABLE_LEGACY block: ${hit}" >&2
    status=1
  fi
done < <(
  awk '
    /^[[:space:]]*if\(LEGEND2_ENABLE_LEGACY\)/ {in_legacy=1}
    in_legacy && /^[[:space:]]*endif\(\)/ {in_legacy=0; next}
    /legacy\// && !in_legacy {print NR ":" $0}
  ' "${SERVER_CMAKE}" || true
)

if [[ "${status}" -ne 0 ]]; then
  exit "${status}"
fi

echo "ok: legacy source gating validated"
