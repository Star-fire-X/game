#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${ROOT_DIR}"

# Enforce on active server sources. Old tests are migrated separately.
SCAN_DIRS=(
  "src/server"
)

OLD_INCLUDE_PATTERN='^[[:space:]]*#include[[:space:]]*[<"]((src/)?server/handlers/|handlers/)[^">]*[">]'
OLD_NAMESPACE_PATTERN='(^|[^[:alnum:]_])legend2::handlers([^[:alnum:]_]|$)'

old_include_hits="$(rg -n --glob '*.[ch]' --glob '*.cc' --glob '*.cpp' --glob '*.hpp' "${OLD_INCLUDE_PATTERN}" "${SCAN_DIRS[@]}" || true)"
if [[ -n "${old_include_hits}" ]]; then
  echo "error: detected old handlers include paths:" >&2
  echo "${old_include_hits}" >&2
  exit 1
fi

old_namespace_hits="$(rg -n --glob '*.[ch]' --glob '*.cc' --glob '*.cpp' --glob '*.hpp' "${OLD_NAMESPACE_PATTERN}" "${SCAN_DIRS[@]}" || true)"
if [[ -n "${old_namespace_hits}" ]]; then
  echo "error: detected old legend2::handlers namespace usage:" >&2
  echo "${old_namespace_hits}" >&2
  exit 1
fi

echo "ok: no old server handlers path/namespace references detected in src/server"
